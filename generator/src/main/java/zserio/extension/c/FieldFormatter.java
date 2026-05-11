/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.math.BigInteger;

import zserio.ast.ArrayInstantiation;
import zserio.ast.BitmaskType;
import zserio.ast.BooleanType;
import zserio.ast.BytesType;
import zserio.ast.CompoundType;
import zserio.ast.DynamicBitFieldInstantiation;
import zserio.ast.DynamicBitFieldType;
import zserio.ast.EnumType;
import zserio.ast.Expression;
import zserio.ast.ExternType;
import zserio.ast.Field;
import zserio.ast.FixedBitFieldType;
import zserio.ast.FloatType;
import zserio.ast.PackageName;
import zserio.ast.ParameterizedTypeInstantiation;
import zserio.ast.StdIntegerType;
import zserio.ast.StringType;
import zserio.ast.Subtype;
import zserio.ast.TypeInstantiation;
import zserio.ast.TypeReference;
import zserio.ast.VarIntegerType;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/**
 * Lowers one zserio struct {@link Field} to its C99 representation.
 *
 * Produces {@link FieldData}: the C member type plus the per-field
 * {@code read} / {@code write} / {@code free} statements that go into
 * the generated function bodies, and an optional include path for the
 * struct header.
 *
 * Phase 2 batch 4 scope: integer primitives, {@code bool}, {@code float*},
 * {@code string}, {@code bytes}, {@code extern}, fixed-width bit fields
 * ({@code bit:N} / {@code int:N}), all variable-length integer flavours
 * ({@code varuintN} / {@code varintN} / {@code varuint} / {@code varint}
 * / {@code varsize}), enums, bitmasks, subtypes, and nested compound
 * types (struct / choice / union — they wire to the C functions emitted
 * by their respective emitters).
 *
 * Out of scope until later batches: dynamic-width bit fields
 * ({@code bit<n>}), arrays, optionals, parameterised types.
 */
public final class FieldFormatter
{
    private FieldFormatter()
    {
        // utility class
    }

    /**
     * Holder where parent state — parameters, count members, and
     * presence flags — lives, given a field-data {@code holderExpr}.
     * Inside a Choice or Union case the field's data sits in an
     * anonymous member union ({@code "self->u."}) but counts and
     * presence flags hang off the parent compound at {@code "self->"};
     * this strip-{@code "u."} convention keeps both layouts inside
     * one generator without threading a second holder through every
     * helper.
     */
    static String parentHolder(String holderExpr)
    {
        if (holderExpr.endsWith("u."))
        {
            return holderExpr.substring(0, holderExpr.length() - 2);
        }
        return holderExpr;
    }

    /**
     * Lower one field. {@code holderExpr} is the C expression that
     * yields the struct holding the field — typically {@code "self->"}
     * for top-level structs or {@code "self->u."} for the cases of
     * a union (where each case lives inside an anonymous member union).
     * Calls the no-owner overload; same-compound function calls in
     * field-level expressions can't be resolved without an owner.
     */
    public static FieldData format(Field field, String holderExpr) throws ZserioExtensionException
    {
        return format(field, holderExpr, null, OffsetContext.empty());
    }

    /**
     * Same as {@link #format(Field, String)} but with the compound
     * that owns the field — used as the implicit receiver for
     * same-compound function calls in the field's constraint /
     * optional-clause / array-length / parameter-arg expressions.
     */
    public static FieldData format(Field field, String holderExpr, CompoundType owner)
            throws ZserioExtensionException
    {
        return format(field, holderExpr, owner, OffsetContext.empty());
    }

    /**
     * Carries every piece of offset-source metadata that
     * {@link FieldFormatter#format} needs to wrap a field's read /
     * write with offset semantics. Built once by the compound emitter
     * (one OffsetContext per compound) and reused across every field
     * in the compound.
     *
     * <p>Three pieces of information:
     * <ul>
     *   <li>{@code atomicSourceBits} — sibling-field names referenced
     *       by some atomic {@code <fieldOffset>:} directive, mapped to
     *       the source field's bit width. The source field's writer
     *       emission records its pre-write bit position so the
     *       anchored sibling can back-patch.</li>
     *   <li>{@code indexedSourceBits} — sibling-array-field names
     *       referenced by some indexed {@code <arr>[@index]:}
     *       directive, mapped to the array element's bit width. The
     *       source array's writer emission captures the bit position
     *       of its first element so the anchored array's per-element
     *       back-patch can derive element[i]'s wire position as
     *       {@code start + i * elemBits}.</li>
     *   <li>{@code indexedAnchors} — anchored-field names that carry
     *       an {@code <arr>[@index]:} directive, mapped to the
     *       per-element wrap info (source name, element bit width,
     *       cast type) needed to emit the inside-loop back-patch.</li>
     * </ul>
     */
    public static final class OffsetContext
    {
        private final java.util.Map<String, Integer> atomicSourceBits;
        private final java.util.Map<String, Integer> indexedSourceBits;
        private final java.util.Map<String, IndexedAnchorInfo> indexedAnchors;

        public OffsetContext(java.util.Map<String, Integer> atomicSourceBits,
                java.util.Map<String, Integer> indexedSourceBits,
                java.util.Map<String, IndexedAnchorInfo> indexedAnchors)
        {
            this.atomicSourceBits = atomicSourceBits;
            this.indexedSourceBits = indexedSourceBits;
            this.indexedAnchors = indexedAnchors;
        }

        public static OffsetContext empty()
        {
            return new OffsetContext(java.util.Collections.<String, Integer>emptyMap(),
                    java.util.Collections.<String, Integer>emptyMap(),
                    java.util.Collections.<String, IndexedAnchorInfo>emptyMap());
        }

        public boolean isEmpty()
        {
            return atomicSourceBits.isEmpty()
                    && indexedSourceBits.isEmpty()
                    && indexedAnchors.isEmpty();
        }
    }

    /**
     * Per-element info for an array field carrying an indexed offset
     * directive ({@code <src>[@index]:}). The source field is named by
     * its zserio identifier; the C access expression for source[i] is
     * derived as {@code <holder><lowerSnake(name)>[<iter>]}.
     */
    public static final class IndexedAnchorInfo
    {
        private final String sourceZserioName;
        private final int sourceElemBits;

        public IndexedAnchorInfo(String sourceZserioName, int sourceElemBits)
        {
            this.sourceZserioName = sourceZserioName;
            this.sourceElemBits = sourceElemBits;
        }
    }

    /**
     * Format with offset-source metadata. The caller pre-computes the
     * {@link OffsetContext} once per compound and reuses it across
     * every field. The format method applies up to four offset wraps:
     *
     * <ul>
     *   <li>If the field's name is in {@code atomicSourceBits}, its
     *       write statement is prefixed with a
     *       {@code const size_t _<name>_offset_write_pos = …;} local
     *       so an anchored sibling can back-patch it.</li>
     *   <li>If the field's name is in {@code indexedSourceBits}, the
     *       array writer emission is post-processed to capture a
     *       {@code size_t _<name>_offset_data_start = …;} marker that
     *       points at the first element's bit position.</li>
     *   <li>If the field carries an atomic offset directive
     *       (simple sibling-field reference), read / write are wrapped
     *       with byte alignment plus verify (read) / back-patch
     *       (write).</li>
     *   <li>If the field carries an indexed offset directive
     *       ({@code <arr>[@index]:}), the array's per-element read /
     *       write is wrapped inside the loop with the same alignment +
     *       verify / back-patch pattern, indexing the source array by
     *       the same iterator variable.</li>
     * </ul>
     *
     * Unsupported offset shapes (dot paths, mixed expressions) fall
     * through without a wrap so discovery / compile keep passing,
     * matching the prior behavior. The schema's emitted wire output
     * does not honor the directive in that case — tracked separately.
     */
    public static FieldData format(Field field, String holderExpr, CompoundType owner,
            OffsetContext offsetContext)
            throws ZserioExtensionException
    {
        FieldData data = formatBase(field, holderExpr, owner);

        // `align(N):` (and the per-field `align(N) T x;` shape) tells
        // the bitstream cursor to skip forward to the next N-bit
        // boundary before reading or writing this field. NDS Live's
        // ExternData / DataLayer / DataLayerDefinition rely on
        // align(8) to byte-align embedded extern blobs.
        if (field.getAlignmentExpr() != null)
        {
            final String formattedAlign = CExpressionFormatter.format(
                    field.getAlignmentExpr(), holderExpr, owner);
            data = wrapAlignment(data, formattedAlign);
        }

        // `<expr>:` (offset directive) — byte-align the cursor before
        // this field and back-patch the offset source with the actual
        // byte position. Apply BEFORE constraint / optional so the
        // alignment / back-patch only run when the field is actually
        // present (matches upstream initializeOffsets+write semantics:
        // an absent optional field does not anchor its offset).
        //
        // Three shapes recognised here. Everything else (a dot path,
        // `a[@index].b`, an arbitrary expression) falls through
        // without a wrap so discovery / compile keep passing — the
        // wire bytes won't honor the directive but the gap remains
        // visible separately.
        final Expression offExprPeek = field.getOffsetExpr();
        if (offExprPeek != null)
        {
            if (isAtomicFieldRef(offExprPeek))
            {
                data = wrapOffset(field, data, holderExpr, owner,
                        offsetContext.atomicSourceBits);
            }
            else
            {
                final IndexedAnchorInfo info = offsetContext.indexedAnchors.get(field.getName());
                if (info != null)
                {
                    data = wrapIndexedOffsetAnchored(data, field, info, holderExpr);
                }
            }
        }

        // Constraint check sits between the value lowering and the
        // optional wrapper so an absent field doesn't fail validation.
        if (field.getConstraintExpr() != null)
        {
            final String formatted =
                    CExpressionFormatter.format(field.getConstraintExpr(), holderExpr, owner);
            data = wrapConstraint(data, formatted);
        }

        if (field.isOptional())
        {
            if (field.getOptionalClauseExpr() != null)
            {
                final String formattedClause =
                        CExpressionFormatter.format(
                                field.getOptionalClauseExpr(), holderExpr, owner);
                data = wrapIfOptional(data, formattedClause, holderExpr);
            }
            else
            {
                data = wrapAutoOptional(data, holderExpr);
            }
        }

        // `extend T x;` (backward-compatibility field added in a later
        // schema version) — upstream cpp byte-aligns the cursor BEFORE
        // any optional presence check so the bits land at a known byte
        // boundary that older readers can detect via end-of-stream.
        // Apply OUTSIDE the optional wrap so the byte align happens
        // before the optional 1-bit prefix, mirroring upstream's
        // {@code alignTo(8); if (hasValue()) writeBool(true) … else
        // writeBool(false);} order.
        //
        // Caveat: the "could be missing" semantics (extend field
        // entirely absent because the wire ends earlier) is not
        // implemented; we always write the field. Round-trips work as
        // long as both ends speak the same schema version.
        if (field.isExtended())
        {
            data = wrapAlignment(data, "8");
        }

        // If this field is an atomic offset source for some other field
        // in the same compound, record the writer's bit position before
        // writing it so the anchored field's back-patch can seek back
        // to it. The local is declared at function scope (above the
        // field's existing writeStmt) and is consumed later in the
        // same body.
        final Integer offSrcBits = offsetContext.atomicSourceBits.get(field.getName());
        if (offSrcBits != null)
        {
            data = wrapOffsetSource(data, field.getName(), offSrcBits);
        }

        // If this field is the source ARRAY for an indexed offset
        // ({@code <name>[@index]:} referenced on a later sibling),
        // capture the bit position of element[0] in a function-scope
        // {@code _<name>_offset_data_start} marker. The anchored
        // sibling derives element[i]'s wire position from
        // {@code _data_start + i * elemBits}.
        final Integer indexedSrcBits = offsetContext.indexedSourceBits.get(field.getName());
        if (indexedSrcBits != null)
        {
            data = wrapIndexedOffsetSource(data, field.getName());
        }
        return data;
    }

    /** True iff the expression is a single, atomic identifier
     *  resolving to a {@link Field}. */
    private static boolean isAtomicFieldRef(Expression e)
    {
        return e.getExprSymbolObject() instanceof Field
                && e.op1() == null && e.op2() == null;
    }

    /**
     * Wrap a {@link FieldData} with a bitstream alignment call. The
     * runtime advances the cursor to the next N-bit boundary; a
     * power-of-two N is required (the runtime checks). For align(8)
     * (the common case in NDS Live's ExternData blobs) this byte-
     * aligns the cursor before the inner field's read / write.
     */
    private static FieldData wrapAlignment(FieldData inner, String formattedAlign)
    {
        final String read =
                "ZSERIO_TRY(zserio_bitstream_reader_align(bs, (size_t)("
                + formattedAlign + ")));\n    " + inner.readStmt;
        final String write =
                "ZSERIO_TRY(zserio_bitstream_writer_align(bs, (size_t)("
                + formattedAlign + ")));\n    " + inner.writeStmt;
        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                read, write, inner.freeStmt,
                inner.owned, inner.extraInclude, inner.extraMember);
    }

    /**
     * Wrap a {@link FieldData} carrying an offset directive
     * ({@code <expr>: T x;}). The directive demands that the field
     * start at a byte boundary whose byte position is recorded in
     * {@code expr} (which must reference a sibling integer field).
     *
     * <p><b>Read side:</b> byte-align the reader and verify the
     * resulting byte position matches the stored offset value; return
     * {@code ZSERIO_ERR_OFFSET_MISMATCH} on disagreement.
     *
     * <p><b>Write side (single-pass back-patch):</b> byte-align the
     * writer, compute the current byte position, store it back into
     * the offset source field, then seek to the offset source's earlier
     * write position, rewrite it with the corrected value, and seek
     * back. The earlier write position is captured by
     * {@link #wrapOffsetSource} on the offset source field's emission.
     *
     * <p>Only simple-{@link Field}-reference offset expressions are
     * supported today (the offset source must resolve to a fixed-width
     * sibling). Array-element-indexed offsets ({@code offsets[@index]:})
     * and arbitrary expressions throw.
     */
    private static FieldData wrapOffset(Field field, FieldData inner, String holderExpr,
            CompoundType owner, java.util.Map<String, Integer> offsetSourceBitWidths)
            throws ZserioExtensionException
    {
        final Expression offExpr = field.getOffsetExpr();
        final Object sym = offExpr.getExprSymbolObject();
        if (!(sym instanceof Field))
        {
            throw new ZserioExtensionException(
                    "zserio-c: offset directive on field '" + field.getName()
                            + "' references " + (sym == null ? "an unresolved symbol"
                                    : sym.getClass().getSimpleName())
                            + " — only simple sibling-field references are supported "
                            + "(array-element-indexed offsets are not yet implemented)");
        }
        final Field offSrc = (Field)sym;
        final Integer bitsObj = offsetSourceBitWidths.get(offSrc.getName());
        if (bitsObj == null)
        {
            // Two reasons this can happen: the caller did not register the
            // source (only the no-map FieldFormatter.format overload was
            // used — unusual for a struct that contains offsets), or the
            // source's type isn't fixed-width and we couldn't compute a
            // bit count. Either way, fail loudly so the gap is visible.
            throw new ZserioExtensionException(
                    "zserio-c: offset source field '" + offSrc.getName()
                            + "' (referenced from '" + field.getName()
                            + "') has no resolvable fixed-width — variable-width "
                            + "offset sources are not yet supported");
        }
        final int bits = bitsObj.intValue();
        final String offValueExpr =
                CExpressionFormatter.format(offExpr, holderExpr, owner);
        final String posLocal =
                "_" + CIdentifierFormatter.toLowerSnake(offSrc.getName()) + "_offset_write_pos";

        // Read: align to byte boundary, then verify the stored offset
        // value matches the actual byte position.
        final String read =
                "ZSERIO_TRY(zserio_bitstream_reader_align(bs, (size_t)8));\n"
                + "    if ((zserio_bitstream_reader_position(bs) / 8u) != (size_t)("
                + offValueExpr + ")) {\n"
                + "        return ZSERIO_ERR_OFFSET_MISMATCH;\n"
                + "    }\n    "
                + inner.readStmt;

        // Write: align, capture new byte position into the offset source,
        // back-patch its earlier wire position, seek forward, then write
        // the anchored field. The block braces scope the temporary so
        // back-to-back offset directives in the same compound do not
        // alias their `_saved_pos` locals.
        final String write =
                "ZSERIO_TRY(zserio_bitstream_writer_align(bs, (size_t)8));\n"
                + "    " + offValueExpr + " = ("
                + offsetCastType(bits) + ")(zserio_bitstream_writer_position(bs) / 8u);\n"
                + "    {\n"
                + "        const size_t _saved_pos = zserio_bitstream_writer_position(bs);\n"
                + "        ZSERIO_TRY(zserio_bitstream_writer_seek(bs, " + posLocal + "));\n"
                + "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, " + bits
                + ", (uint64_t)(" + offValueExpr + ")));\n"
                + "        ZSERIO_TRY(zserio_bitstream_writer_seek(bs, _saved_pos));\n"
                + "    }\n    "
                + inner.writeStmt;

        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                read, write, inner.freeStmt,
                inner.owned, inner.extraInclude, inner.extraMember);
    }

    /**
     * Wrap the source array of an indexed offset directive
     * ({@code <name>[@index]:} on some later sibling). The array's
     * {@code writeStmt} is post-processed in two places:
     *
     * <ul>
     *   <li>A function-scope {@code size_t _<name>_offset_data_start = 0u;}
     *       is prepended so the value is visible to the anchored
     *       array's per-element back-patch.</li>
     *   <li>Inside the array's outer block, immediately before the
     *       per-element {@code for} loop, the marker is assigned to
     *       the writer's current bit position — which is the start of
     *       element[0]'s bits (after the count varsize prefix, if
     *       any).</li>
     * </ul>
     *
     * The read path is unchanged: the anchored array verifies offsets
     * against the source array's already-read values rather than
     * computing positions on the fly.
     */
    private static FieldData wrapIndexedOffsetSource(FieldData inner, String zserioName)
    {
        final String snake = CIdentifierFormatter.toLowerSnake(zserioName);
        final String marker = "_" + snake + "_offset_data_start";
        // Anchor on `for (size_t <name>_i ` to locate the per-element
        // loop. variableArray and fixedArray both emit this header
        // verbatim with the field's lower-snake name.
        final String forAnchor = "for (size_t " + snake + "_i ";
        final String w = inner.writeStmt;
        final int forIdx = w.indexOf(forAnchor);
        if (forIdx < 0)
        {
            // Zero-length fixed source array: no for-loop emitted.
            // Skip the marker entirely — the anchored array's loop is
            // also empty so the back-patch never fires either, and an
            // unused size_t local trips `-Werror=unused-variable`.
            return inner;
        }
        final int lineStart = w.lastIndexOf('\n', forIdx) + 1;
        final String indent = w.substring(lineStart, forIdx);
        final String injection =
                marker + " = zserio_bitstream_writer_position(bs);\n" + indent;
        final String newBody = w.substring(0, forIdx) + injection + w.substring(forIdx);
        final String write = "size_t " + marker + " = 0u;\n    " + newBody;
        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                inner.readStmt, write, inner.freeStmt,
                inner.owned, inner.extraInclude, inner.extraMember);
    }

    /**
     * Wrap the anchored array of an indexed offset directive
     * ({@code <src>[@index]:} on this field). The array's read and
     * write statements are post-processed: a per-element wrap is
     * injected immediately after the per-element loop's opening
     * brace, before the inner element body.
     *
     * <p>Write-side wrap (single-pass back-patch using the source's
     * recorded data-start marker):
     * <pre>
     * ZSERIO_TRY(zserio_bitstream_writer_align(bs, 8));
     * &lt;src&gt;[i] = (uint32_t)(zserio_bitstream_writer_position(bs) / 8u);
     * {
     *     const size_t _saved_pos = …;
     *     ZSERIO_TRY(zserio_bitstream_writer_seek(bs,
     *             _&lt;src&gt;_offset_data_start + i * elemBits));
     *     ZSERIO_TRY(zserio_bitstream_write_bits(bs, elemBits,
     *             (uint64_t)(&lt;src&gt;[i])));
     *     ZSERIO_TRY(zserio_bitstream_writer_seek(bs, _saved_pos));
     * }
     * </pre>
     *
     * <p>Read-side wrap (verify against the source array's stored
     * value):
     * <pre>
     * ZSERIO_TRY(zserio_bitstream_reader_align(bs, 8));
     * if ((zserio_bitstream_reader_position(bs) / 8u) != (size_t)(&lt;src&gt;[i]))
     *     return ZSERIO_ERR_OFFSET_MISMATCH;
     * </pre>
     */
    private static FieldData wrapIndexedOffsetAnchored(FieldData inner, Field anchored,
            IndexedAnchorInfo info, String holderExpr)
    {
        final String anchoredSnake = CIdentifierFormatter.toLowerSnake(anchored.getName());
        final String srcSnake = CIdentifierFormatter.toLowerSnake(info.sourceZserioName);
        final String iterVar = anchoredSnake + "_i";
        final String srcAccess = holderExpr + srcSnake + "[" + iterVar + "]";
        final String marker = "_" + srcSnake + "_offset_data_start";
        final int elemBits = info.sourceElemBits;
        final String castType = offsetCastType(elemBits);
        final String backPatchPos = marker + " + " + iterVar
                + " * (size_t)" + elemBits + "u";

        final String writeWrap = ""
                + "ZSERIO_TRY(zserio_bitstream_writer_align(bs, (size_t)8));\n"
                + "                " + srcAccess + " = (" + castType
                + ")(zserio_bitstream_writer_position(bs) / 8u);\n"
                + "                {\n"
                + "                    const size_t _saved_pos = zserio_bitstream_writer_position(bs);\n"
                + "                    ZSERIO_TRY(zserio_bitstream_writer_seek(bs, "
                + backPatchPos + "));\n"
                + "                    ZSERIO_TRY(zserio_bitstream_write_bits(bs, "
                + elemBits + ", (uint64_t)(" + srcAccess + ")));\n"
                + "                    ZSERIO_TRY(zserio_bitstream_writer_seek(bs, _saved_pos));\n"
                + "                }\n"
                + "                ";
        final String readWrap = ""
                + "ZSERIO_TRY(zserio_bitstream_reader_align(bs, (size_t)8));\n"
                + "                if ((zserio_bitstream_reader_position(bs) / 8u) != (size_t)("
                + srcAccess + ")) {\n"
                + "                    return ZSERIO_ERR_OFFSET_MISMATCH;\n"
                + "                }\n"
                + "                ";

        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                injectAfterLoopOpen(inner.readStmt, anchoredSnake, readWrap),
                injectAfterLoopOpen(inner.writeStmt, anchoredSnake, writeWrap),
                inner.freeStmt,
                inner.owned, inner.extraInclude, inner.extraMember);
    }

    /** Inject {@code wrap} immediately after the {@code for (size_t
     *  <name>_i …) &#123;} opening brace, before the inner element
     *  body. Used by {@link #wrapIndexedOffsetAnchored} to slot
     *  per-element offset semantics into an array's emitted loop. */
    private static String injectAfterLoopOpen(String stmt, String snakeName, String wrap)
    {
        final String forAnchor = "for (size_t " + snakeName + "_i ";
        final int forIdx = stmt.indexOf(forAnchor);
        if (forIdx < 0)
        {
            // Zero-length fixed array: no for-loop emitted at all
            // (see `fixedArray`'s zero-length special case). The
            // indexed-offset wrap has nothing to inject into; the
            // anchored array contributes zero bits on the wire, so
            // skipping the wrap is correct.
            return stmt;
        }
        final int braceIdx = stmt.indexOf('{', forIdx);
        if (braceIdx < 0)
        {
            throw new IllegalStateException(
                    "indexed offset: cannot locate '{' after for-loop for '" + snakeName + "'");
        }
        final int newlineIdx = stmt.indexOf('\n', braceIdx);
        if (newlineIdx < 0)
        {
            return stmt;
        }
        // Insert AFTER the newline that follows '{' so the wrap text
        // appears as the next statement(s) in the loop body, before
        // the existing element body.
        return stmt.substring(0, newlineIdx + 1)
                + "                " + wrap
                + stmt.substring(newlineIdx + 1);
    }

    /**
     * Record this field's writer bit position as a function-scope local
     * named {@code _<name>_offset_write_pos}. Consumed by any sibling
     * field whose offset directive references this one (see
     * {@link #wrapOffset}). The local is declared before the inner
     * write statement so the recorded position is the START of the
     * offset source's bits, which is exactly where the back-patch needs
     * to seek to.
     */
    private static FieldData wrapOffsetSource(FieldData inner, String zserioName, int bits)
    {
        // (bits is captured for documentation / future asserts; the
        //  back-patch knows the width from the same map.)
        final String posLocal =
                "_" + CIdentifierFormatter.toLowerSnake(zserioName) + "_offset_write_pos";
        final String write =
                "const size_t " + posLocal
                + " = zserio_bitstream_writer_position(bs);\n    "
                + inner.writeStmt;
        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                inner.readStmt, write, inner.freeStmt,
                inner.owned, inner.extraInclude, inner.extraMember);
    }

    /** Choose the C cast type for back-patching a {@code bits}-wide
     *  offset source's stored value. Mirrors the type a {@code uintN} /
     *  {@code bit:N} field maps to in our generated struct. */
    private static String offsetCastType(int bits)
    {
        if (bits <= 8) return "uint8_t";
        if (bits <= 16) return "uint16_t";
        if (bits <= 32) return "uint32_t";
        return "uint64_t";
    }

    /**
     * Wrap a {@link FieldData} with a runtime constraint check. The
     * generated code returns {@code ZSERIO_ERR_CONSTRAINT_VIOLATED}
     * if the constraint expression evaluates to false at read or
     * write time.
     */
    private static FieldData wrapConstraint(FieldData inner, String formattedExpr)
    {
        final String violated =
                "if (!(" + formattedExpr + ")) {\n"
                + "        return ZSERIO_ERR_CONSTRAINT_VIOLATED;\n"
                + "    }";
        // Read: validate after the value is in place. Write: validate
        // before any wire bytes are emitted, so a violated write
        // aborts cleanly with the buffer cursor unchanged.
        final String read  = inner.readStmt + "\n    " + violated;
        final String write = violated + "\n    " + inner.writeStmt;
        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                read, write, inner.freeStmt,
                inner.owned, inner.extraInclude, inner.extraMember);
    }

    /**
     * Wrap a {@link FieldData} for an {@code optional T x if expr;}
     * field. Presence is derived from the clause expression rather
     * than a 1-bit wire prefix: read evaluates the expression and
     * conditionally reads; write evaluates the expression again and
     * conditionally writes; free uses the stored {@code _present}
     * flag (set by read) so it's safe even if parameters changed
     * between read and free.
     */
    private static FieldData wrapIfOptional(FieldData inner, String formattedClause, String h)
    {
        final String present = parentHolder(h) + inner.name + "_present";
        final String read =
                "if (" + formattedClause + ") {\n"
                + "        " + present + " = true;\n"
                + "        " + inner.readStmt + "\n"
                + "    } else {\n"
                + "        " + present + " = false;\n"
                + "    }";
        final String write =
                "if (" + formattedClause + ") {\n"
                + "        " + inner.writeStmt + "\n"
                + "    }";
        final String free = inner.freeStmt.isEmpty() ? ""
                : "if (" + present + ") {\n"
                + "        " + inner.freeStmt + "\n"
                + "    }";
        final String combinedExtra = "bool " + inner.name + "_present;"
                + (inner.extraMember.isEmpty() ? "" : "\n    " + inner.extraMember);
        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                read, write, free,
                inner.owned, inner.extraInclude,
                combinedExtra);
    }

    /** The non-optional core lowering. */
    private static FieldData formatBase(Field field, String holderExpr, CompoundType owner)
            throws ZserioExtensionException
    {
        final String name = CIdentifierFormatter.toLowerSnake(field.getName());
        final TypeInstantiation typeInst = field.getTypeInstantiation();

        if (typeInst instanceof ArrayInstantiation)
        {
            return arrayField(name, (ArrayInstantiation)typeInst, holderExpr,
                    field.getName(), owner);
        }

        final TypeReference ref = typeInst.getTypeReference();
        final ZserioType direct = ref.getType();

        // C member type: prefer the user-facing typedef (subtype /
        // enum / bitmask / nested compound) over the raw integer width,
        // and remember any header that needs to be #included for it.
        final TypeBinding b = bindType(direct);

        // Compounds wire through the child's emitted *_read/_write/_free
        // helpers. They don't touch the runtime read/write tables, so
        // they short-circuit before the underlying-type dispatch. The
        // compound may be wrapped in one or more subtype layers — the
        // C side stores the field under the subtype's typedef name
        // (held by `b.cType`) but the read/write call routes through
        // the underlying compound's emitted helpers.
        final CompoundType underlyingCompound = compoundUnderlying(direct);
        if (underlyingCompound != null)
        {
            return compound(name, underlyingCompound, typeInst, b, holderExpr, owner);
        }

        final ZserioType under = underlyingType(direct);

        if (under instanceof BooleanType)
        {
            return scalar(name, b, "bool", holderExpr);
        }
        if (under instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)under;
            return scalar(name, b, intSuffix(std.getBitSize(), std.isSigned()), holderExpr);
        }
        if (under instanceof FloatType)
        {
            return scalar(name, b, "float" + ((FloatType)under).getBitSize(), holderExpr);
        }
        if (under instanceof VarIntegerType)
        {
            return scalar(name, b, varintSuffix((VarIntegerType)under), holderExpr);
        }
        if (under instanceof FixedBitFieldType)
        {
            final FixedBitFieldType bf = (FixedBitFieldType)under;
            return bitField(name, b, bf.getBitSize(), bf.isSigned(), holderExpr);
        }
        if (under instanceof DynamicBitFieldType)
        {
            final DynamicBitFieldInstantiation dyn =
                    findDynamicBitFieldInstantiation(direct, typeInst);
            if (dyn != null)
            {
                final boolean signed = ((DynamicBitFieldType)under).isSigned();
                final String formattedLen =
                        CExpressionFormatter.format(dyn.getLengthExpression(), holderExpr, owner);
                return dynamicBitField(name, b, formattedLen, signed, holderExpr);
            }
        }
        if (under instanceof StringType)
        {
            return string(name, holderExpr);
        }
        if (under instanceof BytesType)
        {
            return bytes(name, holderExpr);
        }
        if (under instanceof ExternType)
        {
            return externField(name, holderExpr);
        }
        throw new ZserioExtensionException(
                "zserio-c: struct field '" + field.getName() + "': "
                        + under.getClass().getSimpleName()
                        + " is not yet supported as a field type");
    }

    /**
     * Wrap a non-optional {@link FieldData} so that read / write are
     * gated by a 1-bit presence flag and free runs only when present.
     * Adds a {@code bool <name>_present;} sibling member so callers
     * can query whether the field is set.
     */
    private static FieldData wrapAutoOptional(FieldData inner, String h)
    {
        final String present = parentHolder(h) + inner.name + "_present";
        final String read =
                "{\n"
                + "        uint64_t " + inner.name + "_presence = 0u;\n"
                + "        ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1, &" + inner.name + "_presence));\n"
                + "        " + present + " = (" + inner.name + "_presence != 0u);\n"
                + "        if (" + present + ") {\n"
                + "            " + inner.readStmt + "\n"
                + "        }\n"
                + "    }";
        final String write =
                "if (" + present + ") {\n"
                + "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1, 1u));\n"
                + "        " + inner.writeStmt + "\n"
                + "    } else {\n"
                + "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1, 0u));\n"
                + "    }";
        // Free: only worth gating the inner free if it actually frees
        // anything. For primitive optional fields the inner free is
        // empty, so the outer free degrades to no-op too.
        final String free = inner.freeStmt.isEmpty() ? ""
                : "if (" + present + ") {\n"
                + "        " + inner.freeStmt + "\n"
                + "    }";
        // Stack the presence flag in front of any extraMember the inner
        // had (e.g. an array's count member): the outer presence flag
        // gates the whole storage, count and value alike.
        final String combinedExtra = "bool " + inner.name + "_present;"
                + (inner.extraMember.isEmpty() ? "" : "\n    " + inner.extraMember);
        return new FieldData(
                inner.name, inner.nativeType, inner.declaration,
                read, write, free,
                inner.owned, inner.extraInclude,
                combinedExtra);
    }

    /* -------- arrays -------- */

    /**
     * Lower an array-typed field.
     *
     * <ul>
     *   <li>variable-length ({@code T[]}): a varsize length prefix on
     *       the wire; storage is {@code size_t <name>_count;
     *       <eltType> *<name>;} with allocator ownership of the buffer.
     *   <li>fixed-length ({@code T[N]} where N is a compile-time
     *       integer): no length prefix; storage is a C array
     *       {@code <eltType> <name>[N];}.
     * </ul>
     *
     * Element types: integer primitives, {@code bool}, {@code float*}
     * (passed through the matching {@code zserio_read_*} helper);
     * {@code string} / {@code bytes} / {@code extern} (allocator-owned,
     * per-element free); non-parameterised compound types (call the
     * child's {@code _read} / {@code _write} / {@code _free}).
     *
     * Out of scope: implicit arrays ({@code implicit T[]}), packed
     * arrays ({@code packed T[]}), parameterised compound elements,
     * non-literal length expressions, alignment / offset annotations.
     */
    private static FieldData arrayField(String name, ArrayInstantiation ai, String h,
            String origName, CompoundType owner) throws ZserioExtensionException
    {
        if (ai.isImplicit())
        {
            throw new ZserioExtensionException(
                    "zserio-c: implicit arrays are not yet supported (field '" + origName + "')");
        }

        final TypeInstantiation eltInst = ai.getElementTypeInstantiation();
        final ZserioType eltDirect = eltInst.getTypeReference().getType();
        final TypeBinding eltBinding = bindType(eltDirect);

        if (ai.isPacked())
        {
            // Compound element with packable fields: route through the
            // compound's emitted *_init_packing_context /
            // *_read_packed / *_write_packed helpers. Compound elements
            // without packable fields produce an effectively-unpacked
            // wire format (no delta bits to write), so they fall
            // through to the regular compound-array path below.
            final CompoundType packedCompound = compoundUnderlying(eltDirect);
            if (packedCompound != null && PackingFieldFormatter.hasPackableField(packedCompound))
            {
                final String paramSetupLine =
                        packedCompoundParameterSetup(eltInst, packedCompound, h, owner);
                return packedCompoundArray(name, eltBinding, packedCompound, ai, h, owner,
                        paramSetupLine);
            }
            if (packedCompound != null)
            {
                // Compound element with NO packable fields — same wire
                // shape as an unpacked compound array.
                final ElementOps fallthrough = elementOps(eltDirect, eltInst, eltBinding,
                        origName, h, owner, name + "_i");
                final Expression lenExprPC = ai.getLengthExpression();
                if (lenExprPC == null)
                {
                    return variableArray(name, fallthrough, h);
                }
                final BigInteger fixedLenPC = lenExprPC.getIntegerValue();
                if (fixedLenPC != null)
                {
                    return fixedArray(name, fallthrough, fixedLenPC.toString(), h);
                }
                final String formattedLenPC = CExpressionFormatter.format(lenExprPC, h, owner);
                return expressionLengthArray(name, fallthrough, formattedLenPC, h);
            }

            // String / bytes / extern / float elements aren't packable
            // in upstream's sense — there's no integer to delta-encode.
            // For `packed string[]` and the like, the wire format
            // matches an unpacked array (no isPacked descriptor; just
            // count + elements), so route through the regular array
            // path.
            final ZserioType eltUnderForPackable = underlyingType(eltDirect);
            if (eltUnderForPackable instanceof StringType
                    || eltUnderForPackable instanceof BytesType
                    || eltUnderForPackable instanceof ExternType
                    || eltUnderForPackable instanceof FloatType)
            {
                final ElementOps fallthrough = elementOps(eltDirect, eltInst, eltBinding,
                        origName, h, owner, name + "_i");
                final Expression lenExprS = ai.getLengthExpression();
                if (lenExprS == null)
                {
                    return variableArray(name, fallthrough, h);
                }
                final BigInteger fixedLenS = lenExprS.getIntegerValue();
                if (fixedLenS != null)
                {
                    return fixedArray(name, fallthrough, fixedLenS.toString(), h);
                }
                final String formattedLenS = CExpressionFormatter.format(lenExprS, h, owner);
                return expressionLengthArray(name, fallthrough, formattedLenS, h);
            }

            // VarInteger elements take a different path: the wire
            // format wraps a 1-bit isPacked descriptor + per-element
            // varint encoding (rather than the fixed-width delta
            // context). Generated read handles both packed and
            // unpacked descriptors; write emits the unpacked-descriptor
            // shape (still a valid, decodable encoding).
            final ZserioType eltUnder = underlyingType(eltDirect);
            if (eltUnder instanceof VarIntegerType)
            {
                final String varintSuf = varintSuffix((VarIntegerType)eltUnder);
                final Expression lenExprV = ai.getLengthExpression();
                if (lenExprV == null)
                {
                    return packedVarintHeapArray(name, eltBinding, varintSuf, h, null);
                }
                final BigInteger fixedLenV = lenExprV.getIntegerValue();
                if (fixedLenV != null)
                {
                    return packedVarintFixedArray(name, eltBinding, varintSuf,
                            fixedLenV.toString(), h);
                }
                final String formattedLenV = CExpressionFormatter.format(lenExprV, h, owner);
                return packedVarintHeapArray(name, eltBinding, varintSuf, h, formattedLenV);
            }

            // Packed arrays of fixed-width integer / bit-field / bool
            // elements use the delta-encoding context from the
            // runtime. Other element types (string, bytes, extern)
            // need additional encoder state we don't generate yet.
            final PackedElementSpec packed = packedElementSpec(eltDirect, eltBinding, origName);
            final Expression lenExprP = ai.getLengthExpression();
            if (lenExprP == null)
            {
                return packedVariableArray(name, packed, h);
            }
            final BigInteger fixedLenP = lenExprP.getIntegerValue();
            if (fixedLenP != null)
            {
                return packedFixedArray(name, packed, fixedLenP.toString(), h);
            }
            final String formattedLenP = CExpressionFormatter.format(lenExprP, h, owner);
            return packedExpressionLengthArray(name, packed, formattedLenP, h);
        }

        final ElementOps ops = elementOps(eltDirect, eltInst, eltBinding, origName, h, owner,
                name + "_i");

        final Expression lenExpr = ai.getLengthExpression();
        if (lenExpr == null)
        {
            return variableArray(name, ops, h);
        }
        final BigInteger fixedLen = lenExpr.getIntegerValue();
        if (fixedLen != null)
        {
            return fixedArray(name, ops, fixedLen.toString(), h);
        }
        // Non-literal length expression: heap-allocated array whose
        // count comes from evaluating the expression at read time
        // rather than from a wire varsize prefix. Common use: a
        // parameterised struct with `T data[size]` where `size` is
        // a parameter or earlier field of the parent.
        final String formattedLen = CExpressionFormatter.format(lenExpr, h, owner);
        return expressionLengthArray(name, ops, formattedLen, h);
    }

    /* -------- packed arrays -------- */

    /**
     * Per-element spec for the packed-array path: bit width and
     * signedness for the delta context, plus the C type for the
     * element slot.
     */
    private static PackedElementSpec packedElementSpec(ZserioType eltDirect, TypeBinding b,
            String origName) throws ZserioExtensionException
    {
        final ZserioType under = underlyingType(eltDirect);
        final int bits;
        final boolean signed;
        if (under instanceof BooleanType)
        {
            bits = 1;
            signed = false;
        }
        else if (under instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)under;
            bits = std.getBitSize();
            signed = std.isSigned();
        }
        else if (under instanceof FixedBitFieldType)
        {
            final FixedBitFieldType bf = (FixedBitFieldType)under;
            bits = bf.getBitSize();
            signed = bf.isSigned();
        }
        else if (under instanceof DynamicBitFieldType)
        {
            // Dynamic-width packed: every element shares the same
            // runtime-evaluated width. Storage uses the widest
            // container; the delta context's `num_bits` argument
            // ideally matches the per-array width but the runtime
            // protocol caps at 64 — so we use the maximum and cast
            // each element through it.
            final DynamicBitFieldType dyn = (DynamicBitFieldType)under;
            bits = 64;
            signed = dyn.isSigned();
        }
        else
        {
            throw new ZserioExtensionException(
                    "zserio-c: packed arrays of "
                            + under.getClass().getSimpleName()
                            + " elements are not yet supported (field '" + origName + "')");
        }
        return new PackedElementSpec(b.cType, b.extraInclude, bits, signed);
    }

    /**
     * Produce the read / write / free strings for a packed array of
     * variable-length-integer elements (varuint16/32/64/varuint,
     * varint16/32/64/varint, varsize). The wire format is the same
     * delta-encoded shape upstream uses: a 1-bit isPacked flag, then
     * (if packed) a 6-bit maxBitNumber, then the first element as a
     * full varint and subsequent elements as either a signed delta
     * of (maxBitNumber + 1) bits or another full varint. The read
     * path handles both wire shapes; the write path always emits
     * the unpacked shape (isPacked = 0) for simplicity — output is
     * still a valid wire format any zserio-c reader can decode.
     */
    private static FieldData packedVarintHeapArray(String name, TypeBinding b,
            String runtimeSuffix, String h, String formattedLen)
    {
        final String countExpr = parentHolder(h) + name + "_count";
        final String ptrExpr   = h + name;
        final String iVar      = name + "_i";
        final String declaration = b.cType + " *" + name;
        final String extraMember = "size_t " + name + "_count;";

        final String readCountSetup;
        if (formattedLen == null)
        {
            readCountSetup =
                    "        uint32_t " + name + "_size = 0u;\n"
                    + "        ZSERIO_TRY(zserio_read_varsize(bs, &" + name + "_size));\n"
                    + "        " + countExpr + " = (size_t)" + name + "_size;\n";
        }
        else
        {
            readCountSetup = "        " + countExpr + " = (size_t)("
                    + formattedLen + ");\n";
        }

        final String read =
                "{\n"
                + readCountSetup
                + "        " + ptrExpr + " = NULL;\n"
                + "        if (" + countExpr + " > 0u) {\n"
                + "            " + ptrExpr + " = (" + b.cType + " *)alloc->vtable->alloc(\n"
                + "                    alloc->userdata,\n"
                + "                    " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                    sizeof(*(" + ptrExpr + ")));\n"
                + "            if (" + ptrExpr + " == NULL) {\n"
                + "                return ZSERIO_ERR_OUT_OF_MEMORY;\n"
                + "            }\n"
                + "            uint64_t " + name + "_pkbit = 0u;\n"
                + "            ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1, &"
                + name + "_pkbit));\n"
                + "            uint8_t " + name + "_maxb = 0u;\n"
                + "            if (" + name + "_pkbit != 0u) {\n"
                + "                uint64_t " + name + "_mb = 0u;\n"
                + "                ZSERIO_TRY(zserio_bitstream_read_bits(bs, 6, &"
                + name + "_mb));\n"
                + "                " + name + "_maxb = (uint8_t)" + name + "_mb;\n"
                + "            }\n"
                + "            ZSERIO_TRY(zserio_read_" + runtimeSuffix + "(bs, &"
                + ptrExpr + "[0]));\n"
                + "            int64_t " + name + "_prev = (int64_t)" + ptrExpr + "[0];\n"
                + "            for (size_t " + iVar + " = 1u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                if (" + name + "_pkbit == 0u) {\n"
                + "                    ZSERIO_TRY(zserio_read_" + runtimeSuffix + "(bs, &"
                + ptrExpr + "[" + iVar + "]));\n"
                + "                    " + name + "_prev = (int64_t)" + ptrExpr
                + "[" + iVar + "];\n"
                + "                } else if (" + name + "_maxb > 0u) {\n"
                + "                    int64_t " + name + "_delta = 0;\n"
                + "                    ZSERIO_TRY(zserio_bitstream_read_signed(bs, (uint8_t)("
                + name + "_maxb + 1u), &" + name + "_delta));\n"
                + "                    " + name + "_prev += " + name + "_delta;\n"
                + "                    " + ptrExpr + "[" + iVar + "] = ("
                + b.cType + ")" + name + "_prev;\n"
                + "                } else {\n"
                + "                    " + ptrExpr + "[" + iVar + "] = ("
                + b.cType + ")" + name + "_prev;\n"
                + "                }\n"
                + "            }\n"
                + "        }\n"
                + "    }";

        final String writeCountPrefix = formattedLen == null
                ? "        ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)" + countExpr + "));\n"
                : "";
        // Simplified write: always emit isPacked=0 + each element as a
        // full varint. The output is a valid encoding that any
        // zserio-c reader (or upstream's, which also handles
        // isPacked=0) can decode. Bit-for-bit equality with upstream's
        // packed encoder isn't guaranteed for the same input, but the
        // round-trip values are.
        final String write =
                "{\n"
                + writeCountPrefix
                + "        if (" + countExpr + " > 0u) {\n"
                + "            ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1, 0u));\n"
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                ZSERIO_TRY(zserio_write_" + runtimeSuffix + "(bs, "
                + ptrExpr + "[" + iVar + "]));\n"
                + "            }\n"
                + "        }\n"
                + "    }";

        final String free =
                "if (" + ptrExpr + " != NULL) {\n"
                + "        alloc->vtable->free(alloc->userdata, " + ptrExpr + ",\n"
                + "                " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                sizeof(*(" + ptrExpr + ")));\n"
                + "        " + ptrExpr + " = NULL;\n"
                + "        " + countExpr + " = 0u;\n"
                + "    }";

        return new FieldData(
                name, b.cType + " *", declaration,
                read, write, free,
                /* owned */ true, b.extraInclude, extraMember);
    }

    private static FieldData packedVarintFixedArray(String name, TypeBinding b,
            String runtimeSuffix, String lengthLiteral, String h)
    {
        final String declaration = b.cType + " " + name + "[" + lengthLiteral + "]";
        final String iVar = name + "_i";
        final String access = h + name;

        final String read =
                "{\n"
                + "        uint64_t " + name + "_pkbit = 0u;\n"
                + "        ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1, &"
                + name + "_pkbit));\n"
                + "        uint8_t " + name + "_maxb = 0u;\n"
                + "        if (" + name + "_pkbit != 0u) {\n"
                + "            uint64_t " + name + "_mb = 0u;\n"
                + "            ZSERIO_TRY(zserio_bitstream_read_bits(bs, 6, &"
                + name + "_mb));\n"
                + "            " + name + "_maxb = (uint8_t)" + name + "_mb;\n"
                + "        }\n"
                + "        ZSERIO_TRY(zserio_read_" + runtimeSuffix + "(bs, &"
                + access + "[0]));\n"
                + "        int64_t " + name + "_prev = (int64_t)" + access + "[0];\n"
                + "        for (size_t " + iVar + " = 1u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            if (" + name + "_pkbit == 0u) {\n"
                + "                ZSERIO_TRY(zserio_read_" + runtimeSuffix + "(bs, &"
                + access + "[" + iVar + "]));\n"
                + "                " + name + "_prev = (int64_t)" + access
                + "[" + iVar + "];\n"
                + "            } else if (" + name + "_maxb > 0u) {\n"
                + "                int64_t " + name + "_delta = 0;\n"
                + "                ZSERIO_TRY(zserio_bitstream_read_signed(bs, (uint8_t)("
                + name + "_maxb + 1u), &" + name + "_delta));\n"
                + "                " + name + "_prev += " + name + "_delta;\n"
                + "                " + access + "[" + iVar + "] = ("
                + b.cType + ")" + name + "_prev;\n"
                + "            } else {\n"
                + "                " + access + "[" + iVar + "] = ("
                + b.cType + ")" + name + "_prev;\n"
                + "            }\n"
                + "        }\n"
                + "    }";
        final String write =
                "{\n"
                + "        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1, 0u));\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            ZSERIO_TRY(zserio_write_" + runtimeSuffix + "(bs, "
                + access + "[" + iVar + "]));\n"
                + "        }\n"
                + "    }";
        return new FieldData(
                name, b.cType, declaration,
                read, write, /* free */ "",
                /* owned */ false, b.extraInclude, /* extraMember */ "");
    }

    /** Spec for the per-element delta context (variable + fixed + expr-length packed). */
    private static final class PackedElementSpec
    {
        final String cType;
        final String extraInclude;
        final int bits;
        final boolean signed;

        PackedElementSpec(String cType, String extraInclude, int bits, boolean signed)
        {
            this.cType = cType;
            this.extraInclude = extraInclude;
            this.bits = bits;
            this.signed = signed;
        }
    }

    private static FieldData packedVariableArray(String name, PackedElementSpec p, String h)
    {
        return packedHeapArray(name, p, h, /* lengthSource */ null);
    }

    private static FieldData packedExpressionLengthArray(String name, PackedElementSpec p,
            String formattedLen, String h)
    {
        return packedHeapArray(name, p, h, formattedLen);
    }

    /**
     * Packed array stored on the heap (count + ptr). When
     * {@code formattedLen} is {@code null} the count comes from a
     * varsize wire prefix; otherwise it comes from the expression and
     * no length prefix is emitted.
     */
    private static FieldData packedHeapArray(String name, PackedElementSpec p, String h,
            String formattedLen)
    {
        final String countExpr = parentHolder(h) + name + "_count";
        final String ptrExpr   = h + name;
        final String iVar      = name + "_i";
        final String declaration = p.cType + " *" + name;
        final String extraMember = "size_t " + name + "_count;";

        final String boolStr = p.signed ? "true" : "false";
        final String elementCast = p.signed ? "(int64_t)" : "(uint64_t)";

        // Read pre-loop count handling differs by length source.
        final String readCountSetup;
        if (formattedLen == null)
        {
            readCountSetup =
                    "        uint32_t " + name + "_size = 0u;\n"
                    + "        ZSERIO_TRY(zserio_read_varsize(bs, &" + name + "_size));\n"
                    + "        " + countExpr + " = (size_t)" + name + "_size;\n";
        }
        else
        {
            readCountSetup = "        " + countExpr + " = (size_t)("
                    + formattedLen + ");\n";
        }

        final String read =
                "{\n"
                + readCountSetup
                + "        " + ptrExpr + " = NULL;\n"
                + "        if (" + countExpr + " > 0u) {\n"
                + "            " + ptrExpr + " = (" + p.cType + " *)alloc->vtable->alloc(\n"
                + "                    alloc->userdata,\n"
                + "                    " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                    sizeof(*(" + ptrExpr + ")));\n"
                + "            if (" + ptrExpr + " == NULL) {\n"
                + "                return ZSERIO_ERR_OUT_OF_MEMORY;\n"
                + "            }\n"
                + "            zserio_delta_context_t " + name + "_ctx;\n"
                + "            (void)memset(&" + name + "_ctx, 0, sizeof(" + name + "_ctx));\n"
                + "            ZSERIO_TRY(zserio_delta_init(&" + name + "_ctx, "
                + p.bits + ", " + boolStr + "));\n"
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                uint64_t " + iVar + "_raw;\n"
                + "                ZSERIO_TRY(zserio_delta_read(&" + name + "_ctx, bs, &"
                + iVar + "_raw));\n"
                + "                " + ptrExpr + "[" + iVar + "] = ("
                + p.cType + ")" + iVar + "_raw;\n"
                + "            }\n"
                + "        }\n"
                + "    }";

        // Write: two passes, both gated on count > 0 so an empty array
        // doesn't even initialise the context. The optional varsize
        // prefix matches the read path.
        final String writeCountPrefix = formattedLen == null
                ? "        ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)" + countExpr + "));\n"
                : "";
        final String write =
                "{\n"
                + writeCountPrefix
                + "        if (" + countExpr + " > 0u) {\n"
                + "            zserio_delta_context_t " + name + "_ctx;\n"
                + "            (void)memset(&" + name + "_ctx, 0, sizeof(" + name + "_ctx));\n"
                + "            ZSERIO_TRY(zserio_delta_init(&" + name + "_ctx, "
                + p.bits + ", " + boolStr + "));\n"
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                ZSERIO_TRY(zserio_delta_observe(&" + name + "_ctx, "
                + elementCast + ptrExpr + "[" + iVar + "]));\n"
                + "            }\n"
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                ZSERIO_TRY(zserio_delta_write(&" + name + "_ctx, bs, "
                + elementCast + ptrExpr + "[" + iVar + "]));\n"
                + "            }\n"
                + "        }\n"
                + "    }";

        // Free: pure deallocation of the heap buffer (no per-element
        // free since these element types own no further data).
        final String free =
                "if (" + ptrExpr + " != NULL) {\n"
                + "        alloc->vtable->free(alloc->userdata, " + ptrExpr + ",\n"
                + "                " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                sizeof(*(" + ptrExpr + ")));\n"
                + "        " + ptrExpr + " = NULL;\n"
                + "        " + countExpr + " = 0u;\n"
                + "    }";

        return new FieldData(
                name, p.cType + " *", declaration,
                read, write, free,
                /* owned */ true, p.extraInclude, extraMember);
    }

    /**
     * Packed array of compound elements where the compound has at
     * least one packable scalar field. Uses the compound's emitted
     * {@code _init_packing_context} / {@code _read_packed} /
     * {@code _observe_packing_context} / {@code _write_packed}
     * helpers to delta-encode every packable field across all array
     * elements. Storage matches the unpacked compound-array path
     * (count + heap pointer for variable / expression-length, inline
     * C array for fixed-length); only the wire encoding differs.
     */
    /**
     * Build the {@code <prefix>_set_parameters(&slot, args...)} line
     * for a parameterised compound element of a packed array, or
     * empty when the element type isn't parameterised.
     *
     * The line ends in a newline + indent so the read/write loop
     * substitutes it cleanly between the loop entry and the
     * read_packed / write_packed call.
     */
    private static String packedCompoundParameterSetup(TypeInstantiation eltInst,
            CompoundType ct, String h, CompoundType owner)
            throws ZserioExtensionException
    {
        if (!(eltInst instanceof ParameterizedTypeInstantiation))
        {
            return "";
        }
        final String prefix = CIdentifierFormatter.funcPrefix(
                ct.getPackage().getPackageName(), ct.getName());
        final ParameterizedTypeInstantiation pti = (ParameterizedTypeInstantiation)eltInst;
        final StringBuilder args = new StringBuilder();
        for (final ParameterizedTypeInstantiation.InstantiatedParameter ip
                : pti.getInstantiatedParameters())
        {
            args.append(", ");
            // Use `$INDEX$` as an @index placeholder. The caller
            // substitutes it: with the loop variable for the per-
            // element paramSetup, and with `0u` for the first-element
            // priming call that runs before the for-loop opens.
            // Without the placeholder, both sites used the loop
            // variable and the first-element call referenced an
            // undeclared identifier (task #84).
            args.append(CExpressionFormatter.format(
                    ip.getArgumentExpression(), h, owner, "$INDEX$"));
        }
        // The trailing newline + 16 spaces aligns with the indent
        // depth of the read/write loops in packedCompoundHeapArray
        // and packedCompoundFixedArray.
        return prefix + "_set_parameters(&$SLOT$" + args + ");\n                ";
    }

    /**
     * Produce a {@link FieldData} that lowers an array field through
     * the packed-element encoding. Visible to {@link PackingFieldFormatter}
     * so a parent compound's {@code _read_packed} body can route a
     * non-{@code packed}-declared array field through packed encoding
     * when the array's element type has packable leaves — upstream's
     * rule
     * (cpp/freemarker/CompoundField.inc.ftl:352-355) is "use packed
     * encoding when (parent_is_packed || array.isPacked) and the
     * field is packable".
     */
    static FieldData packedCompoundArrayField(String name, ArrayInstantiation ai,
            String h, CompoundType owner) throws ZserioExtensionException
    {
        final TypeInstantiation eltInst = ai.getElementTypeInstantiation();
        final ZserioType eltDirect = eltInst.getTypeReference().getType();
        final TypeBinding eltBinding = bindType(eltDirect);
        final CompoundType packedCompound = compoundUnderlying(eltDirect);
        if (packedCompound == null
                || !PackingFieldFormatter.hasPackableField(packedCompound))
        {
            throw new ZserioExtensionException(
                    "zserio-c: packedCompoundArrayField called on non-packable element");
        }
        final String paramSetupLine =
                packedCompoundParameterSetup(eltInst, packedCompound, h, owner);
        return packedCompoundArray(name, eltBinding, packedCompound, ai, h, owner,
                paramSetupLine);
    }

    private static FieldData packedCompoundArray(String name, TypeBinding b,
            CompoundType ct, ArrayInstantiation ai, String h, CompoundType owner,
            String paramSetupLine)
            throws ZserioExtensionException
    {
        final String prefix = CIdentifierFormatter.funcPrefix(
                ct.getPackage().getPackageName(), ct.getName());

        final Expression lenExpr = ai.getLengthExpression();
        if (lenExpr == null)
        {
            return packedCompoundHeapArray(name, b, prefix, h,
                    /* lengthSource */ null, paramSetupLine);
        }
        final BigInteger fixedLen = lenExpr.getIntegerValue();
        if (fixedLen != null)
        {
            return packedCompoundFixedArray(name, b, prefix, fixedLen.toString(), h,
                    paramSetupLine);
        }
        final String formattedLen = CExpressionFormatter.format(lenExpr, h, owner);
        return packedCompoundHeapArray(name, b, prefix, h, formattedLen, paramSetupLine);
    }

    private static FieldData packedCompoundHeapArray(String name, TypeBinding b,
            String prefix, String h, String formattedLen, String paramSetupLine)
    {
        final String countExpr = parentHolder(h) + name + "_count";
        final String ptrExpr   = h + name;
        final String iVar      = name + "_i";
        final String ctxVar    = name + "_ctx";
        final String declaration = b.cType + " *" + name;
        final String extraMember = "size_t " + name + "_count;";

        final String readCountSetup;
        if (formattedLen == null)
        {
            readCountSetup =
                    "        uint32_t " + name + "_size = 0u;\n"
                    + "        ZSERIO_TRY(zserio_read_varsize(bs, &" + name + "_size));\n"
                    + "        " + countExpr + " = (size_t)" + name + "_size;\n";
        }
        else
        {
            readCountSetup = "        " + countExpr + " = (size_t)("
                    + formattedLen + ");\n";
        }

        // The slot expression for $SLOT$ substitution within
        // paramSetupLine. Empty paramSetupLine (non-parameterised
        // element) produces empty inserts.
        final String slotExpr = ptrExpr + "[" + iVar + "]";
        final String paramSetupRead = paramSetupLine.isEmpty() ? ""
                : paramSetupLine.replace("$SLOT$", slotExpr).replace("$INDEX$", iVar);
        final String paramSetupWrite = paramSetupRead;

        // The first-element paramSetup primes element 0's parameters
        // so init_packing_context can derive any parameter-driven bit
        // widths (e.g. dynamic-bit fields like `bit<numBits>`) from a
        // fully-set-up element. `@index` is element 0 here, not the
        // loop variable (which isn't in scope yet).
        final String firstSlotExpr = ptrExpr + "[0]";
        final String firstParamSetup = paramSetupLine.isEmpty() ? ""
                : paramSetupLine.replace("$SLOT$", firstSlotExpr).replace("$INDEX$", "0u");

        // Zero-init the packing context: a Choice's init dispatches by
        // selector and only configures the active arm's sub-context.
        // Per-element re-init (emitted in Choice._read_packed) relies
        // on uninitialised sub-contexts reading as zero so the
        // idempotent delta_init treats them as "not yet configured" and
        // configures them on first touch.
        final String read =
                "{\n"
                + readCountSetup
                + "        " + ptrExpr + " = NULL;\n"
                + "        if (" + countExpr + " > 0u) {\n"
                + "            " + ptrExpr + " = (" + b.cType + " *)alloc->vtable->alloc(\n"
                + "                    alloc->userdata,\n"
                + "                    " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                    sizeof(*(" + ptrExpr + ")));\n"
                + "            if (" + ptrExpr + " == NULL) {\n"
                + "                return ZSERIO_ERR_OUT_OF_MEMORY;\n"
                + "            }\n"
                + "            (void)memset(" + ptrExpr + ", 0, "
                + countExpr + " * sizeof(*(" + ptrExpr + ")));\n"
                + "            " + prefix + "_packing_ctx_t " + ctxVar + ";\n"
                + "            (void)memset(&" + ctxVar + ", 0, sizeof(" + ctxVar + "));\n"
                + "            " + firstParamSetup
                + "ZSERIO_TRY(" + prefix + "_init_packing_context(&"
                + firstSlotExpr + ", &" + ctxVar + "));\n"
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                " + paramSetupRead
                + "ZSERIO_TRY(" + prefix + "_read_packed(&"
                + slotExpr + ", bs, alloc, &" + ctxVar + "));\n"
                + "            }\n"
                + "        }\n"
                + "    }";

        final String writeCountPrefix = formattedLen == null
                ? "        ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)" + countExpr + "));\n"
                : "";
        final String write =
                "{\n"
                + writeCountPrefix
                + "        if (" + countExpr + " > 0u) {\n"
                + "            " + prefix + "_packing_ctx_t " + ctxVar + ";\n"
                + "            (void)memset(&" + ctxVar + ", 0, sizeof(" + ctxVar + "));\n"
                + "            " + firstParamSetup
                + "ZSERIO_TRY(" + prefix + "_init_packing_context(&"
                + firstSlotExpr + ", &" + ctxVar + "));\n"
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                " + paramSetupWrite
                + "ZSERIO_TRY(" + prefix + "_observe_packing_context(&"
                + slotExpr + ", &" + ctxVar + "));\n"
                + "            }\n"
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                " + paramSetupWrite
                + "ZSERIO_TRY(" + prefix + "_write_packed(&"
                + slotExpr + ", bs, &" + ctxVar + "));\n"
                + "            }\n"
                + "        }\n"
                + "    }";

        // Free runs the per-element compound _free first to release
        // any owned data the children hold (strings, nested heap
        // members, etc.), then frees the buffer itself.
        final String free =
                "if (" + ptrExpr + " != NULL) {\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "            " + prefix + "_free(&" + ptrExpr + "[" + iVar + "], alloc);\n"
                + "        }\n"
                + "        alloc->vtable->free(alloc->userdata, " + ptrExpr + ",\n"
                + "                " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                sizeof(*(" + ptrExpr + ")));\n"
                + "        " + ptrExpr + " = NULL;\n"
                + "        " + countExpr + " = 0u;\n"
                + "    }";

        return new FieldData(
                name, b.cType + " *", declaration,
                read, write, free,
                /* owned */ true, b.extraInclude, extraMember);
    }

    private static FieldData packedCompoundFixedArray(String name, TypeBinding b,
            String prefix, String lengthLiteral, String h, String paramSetupLine)
    {
        final String declaration = b.cType + " " + name + "[" + lengthLiteral + "]";
        final String iVar   = name + "_i";
        final String ctxVar = name + "_ctx";
        final String access = h + name;
        final String slotExpr = access + "[" + iVar + "]";
        // The fixed-array path's read/write loop indent is one level
        // shallower (8 spaces) than the heap-array path's (16 spaces),
        // so we re-indent the paramSetupLine.
        final String paramSetup = paramSetupLine.isEmpty() ? ""
                : paramSetupLine.replace("$SLOT$", slotExpr).replace("$INDEX$", iVar)
                        .replace("\n                ", "\n            ");
        // First-element paramSetup primes element 0 so init_packing_context
        // can derive parameter-driven bit widths from a fully-initialised
        // element. Indent matches the inline 8-space level here.
        // `@index` is element 0 here, not the loop variable.
        final String firstSlotExpr = access + "[0]";
        final String firstParamSetup = paramSetupLine.isEmpty() ? ""
                : paramSetupLine.replace("$SLOT$", firstSlotExpr).replace("$INDEX$", "0u")
                        .replace("\n                ", "\n        ");

        final String read =
                "{\n"
                + "        " + prefix + "_packing_ctx_t " + ctxVar + ";\n"
                + "        (void)memset(&" + ctxVar + ", 0, sizeof(" + ctxVar + "));\n"
                + "        " + firstParamSetup
                + "ZSERIO_TRY(" + prefix + "_init_packing_context(&"
                + firstSlotExpr + ", &" + ctxVar + "));\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            " + paramSetup
                + "ZSERIO_TRY(" + prefix + "_read_packed(&"
                + slotExpr + ", bs, alloc, &" + ctxVar + "));\n"
                + "        }\n"
                + "    }";
        final String write =
                "{\n"
                + "        " + prefix + "_packing_ctx_t " + ctxVar + ";\n"
                + "        (void)memset(&" + ctxVar + ", 0, sizeof(" + ctxVar + "));\n"
                + "        " + firstParamSetup
                + "ZSERIO_TRY(" + prefix + "_init_packing_context(&"
                + firstSlotExpr + ", &" + ctxVar + "));\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            " + paramSetup
                + "ZSERIO_TRY(" + prefix + "_observe_packing_context(&"
                + slotExpr + ", &" + ctxVar + "));\n"
                + "        }\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            " + paramSetup
                + "ZSERIO_TRY(" + prefix + "_write_packed(&"
                + slotExpr + ", bs, &" + ctxVar + "));\n"
                + "        }\n"
                + "    }";
        final String free =
                "for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "        " + prefix + "_free(&" + access + "[" + iVar + "], alloc);\n"
                + "    }";

        return new FieldData(
                name, b.cType, declaration,
                read, write, free,
                /* owned */ true, b.extraInclude, /* extraMember */ "");
    }

    private static FieldData packedFixedArray(String name, PackedElementSpec p,
            String lengthLiteral, String h)
    {
        // Fixed-length packed array: storage is an inline C array (no
        // count member, no allocation). Read/write run the delta
        // context over the N slots; free is empty (primitive elements
        // own no further data).
        final String declaration = p.cType + " " + name + "[" + lengthLiteral + "]";
        final String iVar = name + "_i";
        final String access = h + name;
        final String boolStr = p.signed ? "true" : "false";
        final String elementCast = p.signed ? "(int64_t)" : "(uint64_t)";

        final String read =
                "{\n"
                + "        zserio_delta_context_t " + name + "_ctx;\n"
                + "        (void)memset(&" + name + "_ctx, 0, sizeof(" + name + "_ctx));\n"
                + "        ZSERIO_TRY(zserio_delta_init(&" + name + "_ctx, "
                + p.bits + ", " + boolStr + "));\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            uint64_t " + iVar + "_raw;\n"
                + "            ZSERIO_TRY(zserio_delta_read(&" + name + "_ctx, bs, &"
                + iVar + "_raw));\n"
                + "            " + access + "[" + iVar + "] = ("
                + p.cType + ")" + iVar + "_raw;\n"
                + "        }\n"
                + "    }";
        final String write =
                "{\n"
                + "        zserio_delta_context_t " + name + "_ctx;\n"
                + "        (void)memset(&" + name + "_ctx, 0, sizeof(" + name + "_ctx));\n"
                + "        ZSERIO_TRY(zserio_delta_init(&" + name + "_ctx, "
                + p.bits + ", " + boolStr + "));\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            ZSERIO_TRY(zserio_delta_observe(&" + name + "_ctx, "
                + elementCast + access + "[" + iVar + "]));\n"
                + "        }\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            ZSERIO_TRY(zserio_delta_write(&" + name + "_ctx, bs, "
                + elementCast + access + "[" + iVar + "]));\n"
                + "        }\n"
                + "    }";

        return new FieldData(
                name, p.cType, declaration,
                read, write, /* free */ "",
                /* owned */ false, p.extraInclude, /* extraMember */ "");
    }

    /**
     * Per-element runtime ops for an array. Each line is a template
     * with {@code $SLOT$} substituted with the actual slot expression
     * ({@code <ptr>[<i>]} for variable, {@code <name>[<i>]} for fixed)
     * by the array-shape helpers below.
     *
     * @param holderExpr   parent struct holder, used when formatting
     *                     parameter argument expressions for parameterised
     *                     compound elements (the args reference parent
     *                     fields/parameters, not per-slot state).
     */
    private static ElementOps elementOps(ZserioType direct, TypeInstantiation typeInst,
            TypeBinding b, String origName, String holderExpr, CompoundType owner,
            String indexVar) throws ZserioExtensionException
    {
        // Compound element (possibly via subtype layers).
        final CompoundType compoundElt = compoundUnderlying(direct);
        if (compoundElt != null)
        {
            final String prefix = CIdentifierFormatter.funcPrefix(
                    compoundElt.getPackage().getPackageName(), compoundElt.getName());

            // Parameterised compound element: each slot needs a
            // _set_parameters call before _read / _write so the slot's
            // parameter members are pinned to the parent-supplied
            // values. Args resolve against the parent's holder, not
            // the per-slot expression — they're parent state.
            String setParamsRead = "";
            String setParamsWrite = "";
            if (typeInst instanceof ParameterizedTypeInstantiation)
            {
                final ParameterizedTypeInstantiation pti =
                        (ParameterizedTypeInstantiation)typeInst;
                final StringBuilder args = new StringBuilder();
                for (final ParameterizedTypeInstantiation.InstantiatedParameter ip
                        : pti.getInstantiatedParameters())
                {
                    args.append(", ");
                    args.append(CExpressionFormatter.format(
                            ip.getArgumentExpression(), holderExpr, owner, indexVar));
                }
                final String setLine = prefix + "_set_parameters(&$SLOT$" + args + ");\n            ";
                setParamsRead  = setLine;
                setParamsWrite = setLine;
            }

            return new ElementOps(b.cType, b.extraInclude, /* owned */ true,
                    setParamsRead  + "ZSERIO_TRY(" + prefix + "_read(&$SLOT$, bs, alloc));",
                    setParamsWrite + "ZSERIO_TRY(" + prefix + "_write(&$SLOT$, bs));",
                    prefix + "_free(&$SLOT$, alloc);");
        }

        final ZserioType under = underlyingType(direct);

        if (under instanceof StringType)
        {
            final String write =
                    "{\n"
                    + "            const zserio_string_view_t slot_view = "
                    + "{ $SLOT$.bytes, $SLOT$.length };\n"
                    + "            ZSERIO_TRY(zserio_write_string(bs, slot_view));\n"
                    + "        }";
            return new ElementOps("zserio_string_t", "zserio/string.h", /* owned */ true,
                    "ZSERIO_TRY(zserio_read_string(bs, alloc, &$SLOT$));",
                    write,
                    "zserio_string_free(&$SLOT$, alloc);");
        }
        if (under instanceof BytesType)
        {
            final String write =
                    "{\n"
                    + "            const zserio_bytes_view_t slot_view = "
                    + "{ $SLOT$.bytes, $SLOT$.length };\n"
                    + "            ZSERIO_TRY(zserio_write_bytes(bs, slot_view));\n"
                    + "        }";
            return new ElementOps("zserio_bytes_t", "zserio/bytes.h", /* owned */ true,
                    "ZSERIO_TRY(zserio_read_bytes(bs, alloc, &$SLOT$));",
                    write,
                    "zserio_bytes_free(&$SLOT$, alloc);");
        }
        if (under instanceof ExternType)
        {
            final String write =
                    "{\n"
                    + "            const zserio_bit_view_t slot_view = "
                    + "{ $SLOT$.bytes, 0u, $SLOT$.bit_length };\n"
                    + "            ZSERIO_TRY(zserio_write_extern(bs, slot_view));\n"
                    + "        }";
            return new ElementOps("zserio_extern_t", "zserio/extern.h", /* owned */ true,
                    "ZSERIO_TRY(zserio_read_extern(bs, alloc, &$SLOT$));",
                    write,
                    "zserio_extern_free(&$SLOT$, alloc);");
        }

        // Bit-field elements: read/write per-element via the bitstream
        // primitives with a temp + cast to the typedef'd C type.
        if (under instanceof FixedBitFieldType)
        {
            final FixedBitFieldType bf = (FixedBitFieldType)under;
            return bitFieldElement(b, bf.getBitSize(), bf.isSigned());
        }
        if (under instanceof DynamicBitFieldType)
        {
            final DynamicBitFieldInstantiation dyn =
                    findDynamicBitFieldInstantiation(direct, typeInst);
            if (dyn != null)
            {
                final boolean signed = ((DynamicBitFieldType)under).isSigned();
                final String formattedLen =
                        CExpressionFormatter.format(dyn.getLengthExpression(), holderExpr, owner);
                return dynamicBitFieldElement(b, formattedLen, signed);
            }
        }

        // Variable-length integers: typed read/write helper.
        if (under instanceof VarIntegerType)
        {
            final String suffix = varintSuffix((VarIntegerType)under);
            return new ElementOps(b.cType, b.extraInclude, /* owned */ false,
                    "ZSERIO_TRY(zserio_read_" + suffix + "(bs, &$SLOT$));",
                    "ZSERIO_TRY(zserio_write_" + suffix + "(bs, $SLOT$));",
                    /* freeLine */ null);
        }

        // Primitive (or subtype/enum/bitmask layered over a primitive)
        final String suffix;
        if (under instanceof BooleanType)
        {
            suffix = "bool";
        }
        else if (under instanceof StdIntegerType)
        {
            final StdIntegerType std = (StdIntegerType)under;
            suffix = intSuffix(std.getBitSize(), std.isSigned());
        }
        else if (under instanceof FloatType)
        {
            suffix = "float" + ((FloatType)under).getBitSize();
        }
        else
        {
            throw new ZserioExtensionException(
                    "zserio-c: array element type "
                            + under.getClass().getSimpleName()
                            + " is not yet supported (field '" + origName + "')");
        }
        return new ElementOps(b.cType, b.extraInclude, /* owned */ false,
                "ZSERIO_TRY(zserio_read_" + suffix + "(bs, &$SLOT$));",
                "ZSERIO_TRY(zserio_write_" + suffix + "(bs, $SLOT$));",
                /* freeLine */ null);
    }

    private static ElementOps dynamicBitFieldElement(TypeBinding b, String formattedLen,
            boolean signed)
    {
        final String runtimeRead  = signed ? "zserio_bitstream_read_signed" : "zserio_bitstream_read_bits";
        final String runtimeWrite = signed ? "zserio_bitstream_write_signed" : "zserio_bitstream_write_bits";
        final String rawType      = signed ? "int64_t" : "uint64_t";
        final String castOnWrite  = signed ? "(int64_t)" : "(uint64_t)";

        final String read =
                "{\n"
                + "            " + rawType + " slot_raw;\n"
                + "            ZSERIO_TRY(" + runtimeRead + "(bs, (uint8_t)("
                + formattedLen + "), &slot_raw));\n"
                + "            $SLOT$ = (" + b.cType + ")slot_raw;\n"
                + "        }";
        final String write = "ZSERIO_TRY(" + runtimeWrite + "(bs, (uint8_t)("
                + formattedLen + "), " + castOnWrite + "$SLOT$));";
        return new ElementOps(b.cType, b.extraInclude, /* owned */ false,
                read, write, /* freeLine */ null);
    }

    private static ElementOps bitFieldElement(TypeBinding b, int bits, boolean signed)
    {
        final String runtimeRead  = signed ? "zserio_bitstream_read_signed" : "zserio_bitstream_read_bits";
        final String runtimeWrite = signed ? "zserio_bitstream_write_signed" : "zserio_bitstream_write_bits";
        final String rawType      = signed ? "int64_t" : "uint64_t";
        final String castOnWrite  = signed ? "(int64_t)" : "(uint64_t)";

        final String read =
                "{\n"
                + "            " + rawType + " slot_raw;\n"
                + "            ZSERIO_TRY(" + runtimeRead + "(bs, " + bits + ", &slot_raw));\n"
                + "            $SLOT$ = (" + b.cType + ")slot_raw;\n"
                + "        }";
        final String write = "ZSERIO_TRY(" + runtimeWrite + "(bs, " + bits
                + ", " + castOnWrite + "$SLOT$));";
        return new ElementOps(b.cType, b.extraInclude, /* owned */ false,
                read, write, /* freeLine */ null);
    }

    private static FieldData variableArray(String name, ElementOps ops, String h)
    {
        // Storage: count + heap pointer. The count member sits before
        // the value member so structs read in the order schemas read.
        final String countExpr = parentHolder(h) + name + "_count";
        final String ptrExpr   = h + name;
        final String iVar      = name + "_i";
        final String slotExpr  = ptrExpr + "[" + iVar + "]";
        final String declaration = ops.cType + " *" + name;
        final String extraMember = "size_t " + name + "_count;";

        final String elementRead  = ops.readLine.replace("$SLOT$", slotExpr);
        final String elementWrite = ops.writeLine.replace("$SLOT$", slotExpr);

        // Owned-element variable arrays must zero the freshly-allocated
        // buffer so a partial-fill failure leaves the unread slots in
        // a state where the runtime _free is a no-op (NULL bytes / 0
        // length / zero-init compound members). Primitive elements
        // don't need the memset — the read fully overwrites each slot
        // and there's no per-element free that could trip on garbage.
        final String memsetLine = ops.owned
                ? "            (void)memset(" + ptrExpr + ", 0, "
                        + countExpr + " * sizeof(*(" + ptrExpr + ")));\n"
                : "";

        final String read =
                "{\n"
                + "        uint32_t " + name + "_size = 0u;\n"
                + "        ZSERIO_TRY(zserio_read_varsize(bs, &" + name + "_size));\n"
                + "        " + countExpr + " = (size_t)" + name + "_size;\n"
                + "        " + ptrExpr   + " = NULL;\n"
                + "        if (" + countExpr + " > 0u) {\n"
                + "            " + ptrExpr + " = (" + ops.cType + " *)alloc->vtable->alloc(\n"
                + "                    alloc->userdata,\n"
                + "                    " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                    sizeof(*(" + ptrExpr + ")));\n"
                + "            if (" + ptrExpr + " == NULL) {\n"
                + "                return ZSERIO_ERR_OUT_OF_MEMORY;\n"
                + "            }\n"
                + memsetLine
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                " + elementRead + "\n"
                + "            }\n"
                + "        }\n"
                + "    }";
        final String write =
                "{\n"
                + "        ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)" + countExpr + "));\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "            " + elementWrite + "\n"
                + "        }\n"
                + "    }";

        // Free: per-element free first (if needed), then the buffer.
        // The element-free loop runs unconditionally on every slot;
        // the runtime _free helpers are no-ops on zero-init storage.
        final String elementFreeLoop;
        if (ops.freeLine != null)
        {
            final String elementFree = ops.freeLine.replace("$SLOT$", slotExpr);
            elementFreeLoop =
                    "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                    + countExpr + "; ++" + iVar + ") {\n"
                    + "            " + elementFree + "\n"
                    + "        }\n";
        }
        else
        {
            elementFreeLoop = "";
        }
        final String free =
                "if (" + ptrExpr + " != NULL) {\n"
                + elementFreeLoop
                + "        alloc->vtable->free(alloc->userdata, " + ptrExpr + ",\n"
                + "                " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                sizeof(*(" + ptrExpr + ")));\n"
                + "        " + ptrExpr + " = NULL;\n"
                + "        " + countExpr + " = 0u;\n"
                + "    }";

        return new FieldData(
                name, ops.cType + " *", declaration,
                read, write, free,
                /* owned */ true, ops.extraInclude, extraMember);
    }

    /**
     * Heap-allocated array whose count is known at read time but
     * comes from a non-wire source — typically a parameter ref or an
     * earlier field. Wire format is just the elements (no length
     * prefix); read evaluates the formatted length expression to
     * decide how many slots to allocate, write iterates the stored
     * count, free uses the stored count.
     */
    private static FieldData expressionLengthArray(String name, ElementOps ops,
            String formattedLen, String h)
    {
        final String countExpr = parentHolder(h) + name + "_count";
        final String ptrExpr   = h + name;
        final String iVar      = name + "_i";
        final String slotExpr  = ptrExpr + "[" + iVar + "]";
        final String declaration = ops.cType + " *" + name;
        final String extraMember = "size_t " + name + "_count;";

        final String elementRead  = ops.readLine.replace("$SLOT$", slotExpr);
        final String elementWrite = ops.writeLine.replace("$SLOT$", slotExpr);

        final String memsetLine = ops.owned
                ? "            (void)memset(" + ptrExpr + ", 0, "
                        + countExpr + " * sizeof(*(" + ptrExpr + ")));\n"
                : "";

        final String read =
                "{\n"
                + "        " + countExpr + " = (size_t)(" + formattedLen + ");\n"
                + "        " + ptrExpr   + " = NULL;\n"
                + "        if (" + countExpr + " > 0u) {\n"
                + "            " + ptrExpr + " = (" + ops.cType + " *)alloc->vtable->alloc(\n"
                + "                    alloc->userdata,\n"
                + "                    " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                    sizeof(*(" + ptrExpr + ")));\n"
                + "            if (" + ptrExpr + " == NULL) {\n"
                + "                return ZSERIO_ERR_OUT_OF_MEMORY;\n"
                + "            }\n"
                + memsetLine
                + "            for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "                " + elementRead + "\n"
                + "            }\n"
                + "        }\n"
                + "    }";

        // Write does NOT emit a wire-length prefix — the receiver
        // recovers the count by re-evaluating the same expression on
        // its own state. We iterate the stored count for safety, since
        // the expression's parameter values may have shifted between
        // read and write.
        final String write =
                "{\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + countExpr + "; ++" + iVar + ") {\n"
                + "            " + elementWrite + "\n"
                + "        }\n"
                + "    }";

        // Free shape is identical to wire-length variable array; the
        // length source doesn't matter once read has populated count.
        final String elementFreeLoop;
        if (ops.freeLine != null)
        {
            final String elementFree = ops.freeLine.replace("$SLOT$", slotExpr);
            elementFreeLoop =
                    "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                    + countExpr + "; ++" + iVar + ") {\n"
                    + "            " + elementFree + "\n"
                    + "        }\n";
        }
        else
        {
            elementFreeLoop = "";
        }
        final String free =
                "if (" + ptrExpr + " != NULL) {\n"
                + elementFreeLoop
                + "        alloc->vtable->free(alloc->userdata, " + ptrExpr + ",\n"
                + "                " + countExpr + " * sizeof(*(" + ptrExpr + ")),\n"
                + "                sizeof(*(" + ptrExpr + ")));\n"
                + "        " + ptrExpr + " = NULL;\n"
                + "        " + countExpr + " = 0u;\n"
                + "    }";

        return new FieldData(
                name, ops.cType + " *", declaration,
                read, write, free,
                /* owned */ true, ops.extraInclude, extraMember);
    }

    private static FieldData fixedArray(String name, ElementOps ops,
            String lengthLiteral, String h)
    {
        // ISO C forbids zero-size arrays. Special-case `T name[0]`:
        // emit a one-byte placeholder member so the struct stays well-
        // formed under {@code -Wpedantic}, and skip the per-element
        // loops entirely (they'd be `for (i = 0; i < 0; …)`, which
        // also trips {@code -Wtype-limits}). On the wire a zero-length
        // fixed array contributes zero bits, so the read and write
        // are no-ops.
        if ("0".equals(lengthLiteral))
        {
            final String declaration = "char _zserio_empty_" + name;
            return new FieldData(
                    name, ops.cType, declaration,
                    /* read */ "/* fixed array " + name + "[0]: no bits on the wire */",
                    /* write */ "/* fixed array " + name + "[0]: no bits on the wire */",
                    /* free */ "",
                    /* owned */ false, ops.extraInclude, /* extraMember */ "");
        }
        final String declaration = ops.cType + " " + name + "[" + lengthLiteral + "]";
        final String iVar     = name + "_i";
        final String slotExpr = h + name + "[" + iVar + "]";

        final String elementRead  = ops.readLine.replace("$SLOT$", slotExpr);
        final String elementWrite = ops.writeLine.replace("$SLOT$", slotExpr);

        final String read =
                "{\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            " + elementRead + "\n"
                + "        }\n"
                + "    }";
        final String write =
                "{\n"
                + "        for (size_t " + iVar + " = 0u; " + iVar + " < "
                + lengthLiteral + "u; ++" + iVar + ") {\n"
                + "            " + elementWrite + "\n"
                + "        }\n"
                + "    }";

        // Fixed-array storage lives inline in the parent struct, so
        // there's no buffer to free — only per-element free for owned
        // element types. Element 0..N-1 are all initialized (parent
        // _init zeroed them), so the runtime _free helper is safe even
        // on partially-read state.
        final String free;
        if (ops.freeLine != null)
        {
            final String elementFree = ops.freeLine.replace("$SLOT$", slotExpr);
            free =
                    "for (size_t " + iVar + " = 0u; " + iVar + " < "
                    + lengthLiteral + "u; ++" + iVar + ") {\n"
                    + "        " + elementFree + "\n"
                    + "    }";
        }
        else
        {
            free = "";
        }

        return new FieldData(
                name, ops.cType, declaration,
                read, write, free,
                /* owned */ ops.freeLine != null, ops.extraInclude, /* extraMember */ "");
    }

    /** Per-element ops descriptor used by both variable and fixed arrays. */
    private static final class ElementOps
    {
        final String cType;
        final String extraInclude;
        final boolean owned;     // true → memset alloc + per-element free loop
        final String readLine;   // template with $SLOT$
        final String writeLine;
        final String freeLine;   // null = no per-element free needed

        ElementOps(String cType, String extraInclude, boolean owned,
                String readLine, String writeLine, String freeLine)
        {
            this.cType = cType;
            this.extraInclude = extraInclude;
            this.owned = owned;
            this.readLine = readLine;
            this.writeLine = writeLine;
            this.freeLine = freeLine;
        }
    }

    /* -------- read/write/free shapes per category -------- */

    /** Fixed-width primitive / bool / float / varint via a typed read fn. */
    private static FieldData scalar(String name, TypeBinding b, String runtimeSuffix, String h)
    {
        // The runtime function has a stdint-typed pointer parameter,
        // and a C typedef is identical to its underlying type, so we
        // can pass &<holder>name directly even when the C type is a
        // user-facing typedef (subtype / enum / bitmask).
        final String read  = "ZSERIO_TRY(zserio_read_"  + runtimeSuffix
                + "(bs, &" + h + name + "));";
        final String write = "ZSERIO_TRY(zserio_write_" + runtimeSuffix
                + "(bs, " + h + name + "));";
        return new FieldData(name, b.cType, read, write, "", false, b.extraInclude);
    }

    /**
     * Dynamic-width bit field — bit count is a runtime expression
     * over parent state. Storage is the widest fixed-width type
     * (uint64_t / int64_t); read evaluates the length expression at
     * each call and uses the bitstream primitives with that width.
     */
    private static FieldData dynamicBitField(String name, TypeBinding b,
            String formattedLen, boolean signed, String h)
    {
        final String runtimeRead  = signed ? "zserio_bitstream_read_signed" : "zserio_bitstream_read_bits";
        final String runtimeWrite = signed ? "zserio_bitstream_write_signed" : "zserio_bitstream_write_bits";
        final String rawType      = signed ? "int64_t" : "uint64_t";
        final String castOnWrite  = signed ? "(int64_t)" : "(uint64_t)";

        final String read =
                "{\n"
                + "        " + rawType + " " + name + "_raw;\n"
                + "        ZSERIO_TRY(" + runtimeRead + "(bs, (uint8_t)("
                + formattedLen + "), &" + name + "_raw));\n"
                + "        " + h + name + " = (" + b.cType + ")" + name + "_raw;\n"
                + "    }";
        final String write = "ZSERIO_TRY(" + runtimeWrite + "(bs, (uint8_t)("
                + formattedLen + "), " + castOnWrite + h + name + "));";
        return new FieldData(name, b.cType, read, write, "", false, b.extraInclude);
    }

    /** Fixed-width bit field; read into a wide raw, then cast. */
    private static FieldData bitField(String name, TypeBinding b, int bits, boolean signed, String h)
    {
        final String runtimeRead  = signed ? "zserio_bitstream_read_signed" : "zserio_bitstream_read_bits";
        final String runtimeWrite = signed ? "zserio_bitstream_write_signed" : "zserio_bitstream_write_bits";
        final String rawType      = signed ? "int64_t" : "uint64_t";
        final String castOnWrite  = signed ? "(int64_t)" : "(uint64_t)";

        final String read =
                "{\n"
                + "        " + rawType + " " + name + "_raw;\n"
                + "        ZSERIO_TRY(" + runtimeRead + "(bs, " + bits
                + ", &" + name + "_raw));\n"
                + "        " + h + name + " = (" + b.cType + ")" + name + "_raw;\n"
                + "    }";
        final String write = "ZSERIO_TRY(" + runtimeWrite + "(bs, " + bits
                + ", " + castOnWrite + h + name + "));";
        return new FieldData(name, b.cType, read, write, "", false, b.extraInclude);
    }

    private static FieldData string(String name, String h)
    {
        final String read = "ZSERIO_TRY(zserio_read_string(bs, alloc, &" + h + name + "));";
        final String write =
                "{\n"
                + "        const zserio_string_view_t " + name + "_view = "
                + "{ " + h + name + ".bytes, " + h + name + ".length };\n"
                + "        ZSERIO_TRY(zserio_write_string(bs, " + name + "_view));\n"
                + "    }";
        final String free = "zserio_string_free(&" + h + name + ", alloc);";
        return new FieldData(name, "zserio_string_t", read, write, free, true, "zserio/string.h");
    }

    private static FieldData bytes(String name, String h)
    {
        final String read = "ZSERIO_TRY(zserio_read_bytes(bs, alloc, &" + h + name + "));";
        final String write =
                "{\n"
                + "        const zserio_bytes_view_t " + name + "_view = "
                + "{ " + h + name + ".bytes, " + h + name + ".length };\n"
                + "        ZSERIO_TRY(zserio_write_bytes(bs, " + name + "_view));\n"
                + "    }";
        final String free = "zserio_bytes_free(&" + h + name + ", alloc);";
        return new FieldData(name, "zserio_bytes_t", read, write, free, true, "zserio/bytes.h");
    }

    private static FieldData externField(String name, String h)
    {
        final String read = "ZSERIO_TRY(zserio_read_extern(bs, alloc, &" + h + name + "));";
        final String write =
                "{\n"
                + "        const zserio_bit_view_t " + name + "_view = "
                + "{ " + h + name + ".bytes, 0u, " + h + name + ".bit_length };\n"
                + "        ZSERIO_TRY(zserio_write_extern(bs, " + name + "_view));\n"
                + "    }";
        final String free = "zserio_extern_free(&" + h + name + ", alloc);";
        return new FieldData(name, "zserio_extern_t", read, write, free, true, "zserio/extern.h");
    }

    private static FieldData compound(String name, CompoundType ct, TypeInstantiation typeInst,
            TypeBinding b, String h, CompoundType owner) throws ZserioExtensionException
    {
        final PackageName pkg = ct.getPackage().getPackageName();
        final String prefix = CIdentifierFormatter.funcPrefix(pkg, ct.getName());

        // For a parameterised child, the parent must pin the child's
        // parameter members before each read or write — the parent's
        // own state may have changed since the last read, and the
        // child's read needs its parameters in place to drive the
        // selector / array-length / constraint expressions.
        String setParamsLine = "";
        if (typeInst instanceof ParameterizedTypeInstantiation)
        {
            final ParameterizedTypeInstantiation pti = (ParameterizedTypeInstantiation)typeInst;
            final StringBuilder args = new StringBuilder();
            for (final ParameterizedTypeInstantiation.InstantiatedParameter ip
                    : pti.getInstantiatedParameters())
            {
                args.append(", ");
                args.append(CExpressionFormatter.format(
                        ip.getArgumentExpression(), h, owner));
            }
            // The trailing newline + indent keeps the rendered statement
            // visually aligned with the read/write line that follows it
            // when the template substitutes the placeholder.
            setParamsLine = prefix + "_set_parameters(&" + h + name + args + ");\n        ";
        }

        final String read  = setParamsLine + "ZSERIO_TRY(" + prefix
                + "_read(&" + h + name + ", bs, alloc));";
        final String write = setParamsLine + "ZSERIO_TRY(" + prefix
                + "_write(&" + h + name + ", bs));";
        // Always emit a free call. For primitive-only children the
        // generated _free is itself a no-op apart from the NULL guard,
        // so this stays cheap; emitting unconditionally avoids needing
        // to look at the child's own field list to decide.
        final String free = prefix + "_free(&" + h + name + ", alloc);";
        return new FieldData(name, b.cType, read, write, free, true, b.extraInclude);
    }

    /* -------- type binding -------- */

    /** What C type to emit for the field, plus the include needed for it. */
    private static TypeBinding bindType(ZserioType direct) throws ZserioExtensionException
    {
        if (direct instanceof zserio.ast.InstantiateType)
        {
            // The instantiate-type is a name alias for a concrete
            // template instantiation; resolve to the underlying
            // compound and use its typedef.
            return bindType(((zserio.ast.InstantiateType)direct).getTypeReference()
                    .getBaseTypeReference().getType());
        }
        if (direct instanceof Subtype)
        {
            final Subtype st = (Subtype)direct;
            return userBinding(st.getPackage().getPackageName(), st.getName());
        }
        if (direct instanceof EnumType)
        {
            final EnumType et = (EnumType)direct;
            return userBinding(et.getPackage().getPackageName(), et.getName());
        }
        if (direct instanceof BitmaskType)
        {
            final BitmaskType bt = (BitmaskType)direct;
            return userBinding(bt.getPackage().getPackageName(), bt.getName());
        }
        if (direct instanceof CompoundType)
        {
            final CompoundType ct = (CompoundType)direct;
            return userBinding(ct.getPackage().getPackageName(), ct.getName());
        }
        // Built-in: read C type from the runtime native-type mapper for
        // anything stdint-shaped; explicit names for runtime-owned types.
        if (direct instanceof StringType)
        {
            return new TypeBinding("zserio_string_t", "zserio/string.h");
        }
        if (direct instanceof BytesType)
        {
            return new TypeBinding("zserio_bytes_t", "zserio/bytes.h");
        }
        if (direct instanceof ExternType)
        {
            return new TypeBinding("zserio_extern_t", "zserio/extern.h");
        }
        // Boolean / StdIntegerType / FixedBitFieldType / VarIntegerType /
        // FloatType — all map to a stdint name (or bool / float / double).
        return new TypeBinding(CNativeTypeMapper.nativeTypeFor(direct), null);
    }

    private static TypeBinding userBinding(PackageName pkg, String name)
    {
        return new TypeBinding(
                CIdentifierFormatter.typeName(pkg, name),
                CIdentifierFormatter.headerIncludePath(pkg, name));
    }

    /* -------- traversal helpers -------- */

    /**
     * If {@code direct} resolves through any number of subtype /
     * instantiate-type layers to a {@link CompoundType}, return that
     * compound. Otherwise null. Used so the field-type dispatch
     * routes subtype-of-compound and instantiate-of-template fields
     * to {@link #compound} the same way it routes direct compound
     * fields.
     */
    private static CompoundType compoundUnderlying(ZserioType direct)
    {
        ZserioType t = direct;
        while (t instanceof Subtype || t instanceof zserio.ast.InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((zserio.ast.InstantiateType)t).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        return (t instanceof CompoundType) ? (CompoundType)t : null;
    }

    /**
     * Recover the {@link DynamicBitFieldInstantiation} whose length
     * expression governs a dynamic-width bit field. The instantiation
     * may live directly on the field's typeInst (bare {@code bit<n>}
     * field), or one layer down on an enum / bitmask's
     * {@code getTypeInstantiation()} (the field is an enum / bitmask
     * whose underlying is a dynamic bit field). Subtype / instantiate
     * layers are walked through.
     */
    private static DynamicBitFieldInstantiation findDynamicBitFieldInstantiation(
            ZserioType direct, TypeInstantiation typeInst)
    {
        if (typeInst instanceof DynamicBitFieldInstantiation)
        {
            return (DynamicBitFieldInstantiation)typeInst;
        }
        ZserioType t = direct;
        while (t instanceof Subtype || t instanceof zserio.ast.InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((zserio.ast.InstantiateType)t).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        if (t instanceof BitmaskType)
        {
            final TypeInstantiation ti = ((BitmaskType)t).getTypeInstantiation();
            if (ti instanceof DynamicBitFieldInstantiation)
            {
                return (DynamicBitFieldInstantiation)ti;
            }
        }
        else if (t instanceof EnumType)
        {
            final TypeInstantiation ti = ((EnumType)t).getTypeInstantiation();
            if (ti instanceof DynamicBitFieldInstantiation)
            {
                return (DynamicBitFieldInstantiation)ti;
            }
        }
        return null;
    }

    private static ZserioType underlyingType(ZserioType direct)
    {
        ZserioType t = direct;
        while (t instanceof Subtype || t instanceof zserio.ast.InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((zserio.ast.InstantiateType)t).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        if (t instanceof EnumType)
        {
            t = ((EnumType)t).getTypeInstantiation().getTypeReference()
                    .getBaseTypeReference().getType();
        }
        else if (t instanceof BitmaskType)
        {
            t = ((BitmaskType)t).getTypeInstantiation().getTypeReference()
                    .getBaseTypeReference().getType();
        }
        // The underlying of an enum/bitmask could itself be wrapped in
        // a subtype / instantiate layer.
        while (t instanceof Subtype || t instanceof zserio.ast.InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((zserio.ast.InstantiateType)t).getTypeReference()
                        .getBaseTypeReference().getType();
            }
        }
        return t;
    }

    private static String intSuffix(int bits, boolean signed) throws ZserioExtensionException
    {
        final int container;
        if (bits <= 8) container = 8;
        else if (bits <= 16) container = 16;
        else if (bits <= 32) container = 32;
        else if (bits <= 64) container = 64;
        else throw new ZserioExtensionException("zserio-c: integer width " + bits + " > 64");
        return (signed ? "int" : "uint") + container;
    }

    /** Map a {@link VarIntegerType} to its runtime function suffix. */
    private static String varintSuffix(VarIntegerType vi) throws ZserioExtensionException
    {
        // The grammar packs varuint16/varint16/.../varuint/varint/varsize
        // into VarIntegerType differentiated by (maxBitSize, isSigned).
        // varsize is the only entry with maxBitSize == 40 — it falls
        // out of the table.
        final int max = vi.getMaxBitSize();
        final boolean signed = vi.isSigned();
        if (max == 40) return "varsize";
        if (max == 16) return signed ? "varint16" : "varuint16";
        if (max == 32) return signed ? "varint32" : "varuint32";
        if (max == 64) return signed ? "varint64" : "varuint64";
        if (max == 72) return signed ? "varint"   : "varuint";
        throw new ZserioExtensionException(
                "zserio-c: unsupported varint maxBitSize " + max);
    }

    /** Per-field FreeMarker view. */
    public static final class FieldData
    {
        private final String name;
        private final String nativeType;
        private final String declaration;
        private final String readStmt;
        private final String writeStmt;
        private final String freeStmt;
        private final boolean owned;
        private final String extraInclude;
        private final String extraMember;

        /** For non-array fields where the C declaration is just
         *  {@code <nativeType> <name>}. */
        FieldData(String name, String nativeType, String readStmt, String writeStmt,
                String freeStmt, boolean owned, String extraInclude)
        {
            this(name, nativeType, nativeType + " " + name,
                    readStmt, writeStmt, freeStmt, owned, extraInclude, "");
        }

        /** Same as the simple constructor, plus an {@code extraMember}. */
        FieldData(String name, String nativeType, String readStmt, String writeStmt,
                String freeStmt, boolean owned, String extraInclude, String extraMember)
        {
            this(name, nativeType, nativeType + " " + name,
                    readStmt, writeStmt, freeStmt, owned, extraInclude, extraMember);
        }

        /** Full constructor — used for arrays where the declaration is
         *  {@code <eltType> <name>[<N>]} or {@code <eltType> *<name>}. */
        FieldData(String name, String nativeType, String declaration,
                String readStmt, String writeStmt, String freeStmt,
                boolean owned, String extraInclude, String extraMember)
        {
            this.name = name;
            this.nativeType = nativeType;
            this.declaration = declaration;
            this.readStmt = readStmt;
            this.writeStmt = writeStmt;
            this.freeStmt = freeStmt;
            this.owned = owned;
            this.extraInclude = extraInclude;
            this.extraMember = extraMember;
        }

        public String  getName()           { return name; }
        public String  getNativeType()     { return nativeType; }
        public String  getDeclaration()    { return declaration; }
        public String  getReadStmt()       { return readStmt; }
        public String  getWriteStmt()      { return writeStmt; }
        public String  getFreeStmt()       { return freeStmt; }
        public boolean getOwned()          { return owned; }
        public String  getExtraInclude()   { return extraInclude; }
        public String  getExtraMember()    { return extraMember; }
    }

    /** Pair of C type + the header providing it (or {@code null} for built-ins). */
    private static final class TypeBinding
    {
        final String cType;
        final String extraInclude;

        TypeBinding(String cType, String extraInclude)
        {
            this.cType = cType;
            this.extraInclude = extraInclude;
        }
    }
}
