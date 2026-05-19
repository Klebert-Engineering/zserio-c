# Quick reference

**Status:** informative summary of the zserio schema language as
accepted by `zserio-c`. The normative source is
[`schema-language.md`](schema-language.md).

**Origin:** Adapted from upstream zserio's
`ZserioQuickReference.md` (BSD-3-Clause, © Navigation Data
Standard e.v.) at pinned version v2.18.1, restricted to the
subset zserio-c accepts (no `service`, no `pubsub`, no
`sql_table`/`sql_database`).

**Audience:** schema authors who need a cheat sheet.

---

## Built-in types

### Signed integers and bit fields

```zserio
int8
int16
int32
int64
varint16
varint32
varint64
varint
int:n          // n is a compile-time constant 1..63
int<expr>      // expr evaluates to 1..63 at runtime
```

### Unsigned integers and bit fields

```zserio
uint8
uint16
uint32
uint64
varuint16
varuint32
varuint64
varuint
varsize        // capped at 2³¹ − 1
bit:n          // n is a compile-time constant 1..63
bit<expr>      // expr evaluates to 1..63 at runtime
```

### Floating point

```zserio
float16
float32
float64
```

### Boolean

```zserio
bool
```

### Strings and blobs

```zserio
string         // UTF-8, varsize length prefix + bytes
extern         // opaque bit blob, varsize bit-length prefix + bits
bytes          // opaque byte blob, varsize length prefix + bytes
```

## Constants

```zserio
const bit:1 FALSE = 0;
```

## Enumerations

```zserio
enum bit:3 Color
{
    NONE = 000b,
    RED  = 1,
    BLUE,
    BLACK = 111b
};
```

## Bitmasks

```zserio
bitmask bit:3 Permission
{
    READ,
    WRITE,
    EXECUTE
};
```

## Structures

```zserio
struct MyStructure
{
    bit:4 a;
    uint8 b;
    bit:4 c;
};
```

## Unions

Discriminated; tag is encoded on the wire as a varsize.

```zserio
union SimpleUnion
{
    bit:4   fieldBit4;
    uint8   fieldUInt8;
    uint16  fieldUInt16;
};
```

## Choices

Parameter-driven; no tag on wire.

```zserio
choice VarCoordXY(uint8 width) on width
{
    case 8:  CoordXY8  coord8;
    case 16: CoordXY16 coord16;
    case 24: CoordXY24 coord24;
    case 32: CoordXY32 coord32;
};
```

## Extended members

`extend` adds backward-compatible fields at the end of a
top-level struct. See [`schema-evolution.md`](schema-evolution.md).

```zserio
struct TopLevelBlob
{
    uint32 data[];                     // last field in version 1
    extend ExtendedData extendedData;  // added in version 2
    extend bool additionalFlag;        // added in version 3
};
```

## Constraints

Constraint expressions are evaluated on read; failure returns
`ZSERIO_ERR_CONSTRAINT_VIOLATED`.

```zserio
struct GraphicControlExtension
{
    uint8 byteCount  : byteCount == 4;
    uint8 terminator : terminator == 0;
};
```

## Optional and conditional members

```zserio
struct ItemCount
{
    uint8  count8;
    uint16 count16 if count8 == 0xFF;  // conditional
};

struct Container
{
    int16          item1;
    optional int32 item2;              // explicit on-wire presence bit
};
```

## Functions

Side-effect-free, used in length expressions, offsets, and
client-side accessors.

```zserio
struct ItemCount
{
    uint8  count8;
    uint16 count16 if count8 == 0xFF;

    function uint16 getValue()
    {
        return (count8 == 0xFF) ? count16 : count8;
    }
};
```

## Arrays

```zserio
struct ClassicArrayExample
{
    uint8   header[256];          // fixed length, N is compile-time constant
    int16   numItems;
    Element list[numItems];       // length-expression
};

struct AutoArrayExample
{
    Element list[];               // auto-length, on-wire varsize prefix

    function uint32 getNumItems()
    {
        return lengthof(list);
    }
};
```

Packed variant:

```zserio
struct PackedExample
{
    packed uint32 deltas[];       // delta + zigzag encoding across elements
};
```

## Alignments

`align(N):` advances the bitstream to the next multiple of `N`
bits before encoding the following field.

```zserio
struct AlignmentExample
{
    bit:11 a;

align(32):
    uint32 b;
};
```

## Offsets

`<expr>:` asserts that the following field starts at bit
position `<expr>`. Mismatch on read returns
`ZSERIO_ERR_OFFSET_MISMATCH`.

```zserio
struct Tile
{
    TileHeader  header;
    uint32      stringOffset;
    uint16      numFeatures;

stringOffset:
    StringTable stringTable;
};
```

## Indexed offsets

```zserio
struct IndexedInt32Array
{
    uint32  offsets[10];
    bit:1   spacer;
offsets[@index]:
    int32   data[10];
};
```

The runtime patches `offsets[]` during write; the schema author
declares the storage but does not populate it.

## Parameterised types

```zserio
struct Header
{
    uint32 version;
    uint16 numItems;
};

struct Message
{
    Header   header;
    Item(header) items[header.numItems];
};

struct Item(Header header)
{
    uint16 p;
    uint32 q if header.version >= 10;
};
```

## Subtypes

Transparent on the wire.

```zserio
subtype uint16 BlockIndex;

struct Block
{
    BlockIndex index;
    BlockData  data;
};
```

## Templates and instantiate

Templates emit no code; only `instantiate` and direct uses do.

```zserio
struct Field<T>
{
    T value;
};

instantiate Field<uint32> Field32;

struct Compound<T>
{
    T value;
};

struct StructTemplatedTemplateArgument
{
    Field<Compound<uint32>> compoundField;
};
```

---

## Out of scope

`zserio-c` does **not** accept the following upstream
constructs:

- `service` and `pubsub` declarations (RPC / messaging).
- `sql_table`, `sql_database` (SQLite extension).
- `import_zs` from the JSON extension.

The parser rejects each with a defined diagnostic; see
[`schema-language.md`](schema-language.md) § Out of scope.

## See also

- [`schema-language.md`](schema-language.md) — normative
  language reference.
- [`wire-format.md`](wire-format.md) — normative encoding.
- [`schema-evolution.md`](schema-evolution.md) — forward and
  backward compatibility constructs.
- [`types-mapping.md`](types-mapping.md) — schema → C type
  table.
- [`docs/user-guide/generated-code.md`](../user-guide/generated-code.md)
  — what the generator emits.
