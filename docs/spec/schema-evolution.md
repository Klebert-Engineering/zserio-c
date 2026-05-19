# Schema evolution

**Status:** normative for zserio-c. Discharges
[REQ-DM-006](../../requirements/domain/REQ-DM-006.md).

**Origin:** This document is adapted from upstream zserio's
`ZserioSchemaEvolutionGuide.md` (BSD-3-Clause, © Navigation Data
Standard e.v.) at pinned version v2.18.1.

**Audience:** schema authors evolving a deployed schema; safety
integrators reviewing whether a schema change is wire-compatible.

---

## Why evolution is limited

Zserio does not add per-field tags or self-description to the
bitstream. This makes the wire format compact (roughly half the
size of equivalent protobuf) but means **a schema change is a
wire-format change** unless it uses an explicit evolution
construct.

The four constructs that permit non-breaking evolution are:

| Construct | Direction | Mechanism |
|---|---|---|
| Top-level structure extension | both | Reader stops at schema end; bytes beyond are skipped or appear via `extend`. |
| `extend` keyword | backward | Reader checks end-of-stream before decoding; defaults to zero if absent. |
| `@removed` enum item | both | Removed item keeps its numeric value; new encoders reject it on serialize. |
| Choice case addition (no default) | backward | Old encoders can't produce the new case; new readers handle all old cases. |

Anything outside these — adding a non-extend field in the
middle of a struct, reordering fields, changing a type — is
breaking. The integrator owns ensuring the same schema version
is deployed on both ends of any channel
([H-05](../safety-case/hazard-analysis.md)).

## Forward compatibility

> *Forward compatibility means: a new schema can produce
> bitstreams that an old reader (compiled against the old
> schema) still decodes successfully.*

### Top-level structure extension

If a new schema adds fields at the **end** of a top-level
structure, an old reader stops decoding once it has consumed
all of *its* schema's fields. The trailing bytes from the new
schema are left in the bitstream and ignored.

```zserio
struct TopLevelStructure
{
    uint32  fieldInVersion1;
    varuint newFieldInVersion2;  // old reader stops before this field
};
```

This works **automatically** when:

- The new field is added at the end of a *top-level* struct.
  Adding to the middle, or adding inside a nested struct, breaks
  this.
- The old reader operates over a buffer of known length (so it
  knows where to stop). Streamed reads with no end boundary
  cannot use this property reliably.

### Enumeration item removal

Removing an enum item is forward-compatible if marked
`@removed` rather than physically deleted:

```zserio
enum bit:8 DarkColor
{
    NONE,
    DARK_RED,
    @removed DARK_BLUE,  // old reader will never encounter this in v2 traffic
    DARK_GREEN
};
```

The `@removed` annotation:

- Preserves the numeric value of subsequent items (so
  `DARK_GREEN` stays at its v1 ordinal).
- Tells the v2 encoder to refuse serializing the removed item
  (a programming error in v2 application code).
- Lets v2 readers still *decode* the removed item from a v1
  bitstream, because the numeric value is still known.

Without `@removed`, deleting an item shifts all subsequent
ordinals, breaking every encoded bitstream that contains them.

## Backward compatibility

> *Backward compatibility means: an old encoder produces
> bitstreams that a new reader (compiled against the new
> schema) still decodes successfully.*

### Top-level structure extension via `extend`

The `extend` keyword marks a field as a backward-compatible
addition. A v2 reader decoding a v1 bitstream stops short of
the `extend` field; the field defaults to its zero value and is
marked not-present.

```zserio
struct TopLevelStructure
{
    uint32        fieldInVersion1;
    extend varuint newFieldInVersion2;  // v2 reader checks bytes-remaining here
};
```

Read-side behaviour per
[REQ-FN-025](../../requirements/functional/REQ-FN-025.md):

- If the bitstream has bytes remaining, decode `newFieldInVersion2`
  normally, set its presence sentinel.
- If the bitstream is exhausted, leave `newFieldInVersion2` in
  its zero state, sentinel cleared. Return `ZSERIO_OK`.

The new application accesses the field through its `_present`
sentinel and falls back to a default when absent.

Write-side behaviour: the encoder **always** emits the `extend`
field at its declared position, byte-aligned. There is no
"skip if zero" path on write. Forward compatibility (old reader
on new bytes) is then provided by the top-level structure
extension property above.

### Choice case addition (no default)

Adding a new case to a `choice` is backward-compatible **only
when** the choice has no `default:` arm:

```zserio
choice UInt16Choice(uint16 selector) on selector
{
    case 1:
        VariantA a;

    case 2:
        VariantB b;

    case 3:
        VariantC newInVersion2;  // new readers handle this; old v1 traffic never selects it

    // default case must be either omitted or empty
};
```

Old encoders never produce `selector == 3` (their schema has
only cases 1 and 2). New readers handle all three. If a v2
encoder produces case 3 and a v1 reader decodes it, the v1
reader fails to match any case — this is **not** backward
compatible, hence the "forward-direction" use only.

Adding a `default:` arm changes the contract: a v1 reader
falls into `default:` on the v2-only case, silently consuming
bytes meant for the new variant. Schemas that anticipate future
case additions should avoid `default:` accordingly.

### Enumeration item addition

Adding a new enum item is backward-compatible **only** when it
takes a fresh ordinal that no v1 encoder will produce:

```zserio
enum bit:8 DarkColor
{
    NONE,
    DARK_RED,
    DARK_BLUE,
    DARK_GREEN,
    DARK_NEW_IN_VERSION2  // v1 encoder never produces this ordinal
};
```

v2 readers handle the new item. v1 traffic never contains the
new ordinal; v1 readers (which we are NOT supporting reading v2
bytes for this case) would reject the new ordinal as
`ZSERIO_ERR_INVALID_ENUM`.

This is asymmetric with forward compatibility: a new item is
*backward*-compatible (old encoder ⇒ new reader) but
*forward-breaking* (new encoder ⇒ old reader).

### Enumeration item removal with `@removed`

`@removed` is also a backward-compatible construct. The numeric
value is preserved; the new reader still parses the removed
item from old bitstreams.

```zserio
enum bit:8 DarkColor
{
    NONE,
    DARK_RED,
    @removed DARK_BLUE,
    DARK_GREEN
};
```

New encoders refuse to *produce* the removed item; new readers
still *consume* it without error.

## Compatibility matrix

| Change | Forward (new schema → old reader) | Backward (old schema → new reader) |
|---|:---:|:---:|
| Append field at end of top-level struct (no `extend`) | ✓ auto | ✗ |
| Append field at end of top-level struct (with `extend`) | ✓ auto | ✓ via `extend` |
| Append field in nested struct | ✗ | ✗ |
| Reorder fields | ✗ | ✗ |
| Rename field | ✓ (wire-equivalent) | ✓ (wire-equivalent) |
| Change field type | ✗ | ✗ |
| Add `optional` field at end | ✗ (presence bit changes layout) | ✓ (reader sees absent) |
| Add enum item | ✗ | ✓ |
| Remove enum item without `@removed` | ✗ | ✗ |
| Remove enum item with `@removed` | ✓ | ✓ |
| Add choice case (no default) | ✗ | ✓ |
| Add choice case (with default) | ✗ (silently miscategorised) | ✓ |

## Integration consequence

Zserio is not a schema-evolution framework in the protobuf
sense. The integrator's contract is:

1. Pin one schema version per channel.
2. Use `extend` deliberately when you anticipate adding
   backward-compatible fields.
3. Use `@removed` rather than deleting enum items.
4. Avoid `default:` in choices you may extend.
5. Treat any other schema change as a coordinated deployment.

Hazard analysis: [H-05](../safety-case/hazard-analysis.md).
Wire format: [`wire-format.md`](wire-format.md).
