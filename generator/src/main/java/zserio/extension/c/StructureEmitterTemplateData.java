/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

package zserio.extension.c;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;

import zserio.antlr.ZserioParser;
import zserio.ast.ArrayInstantiation;
import zserio.ast.CompoundType;
import zserio.ast.Expression;
import zserio.ast.Field;
import zserio.ast.FixedSizeType;
import zserio.ast.InstantiateType;
import zserio.ast.PackageName;
import zserio.ast.StructureType;
import zserio.ast.Subtype;
import zserio.ast.TypeInstantiation;
import zserio.ast.ZserioType;
import zserio.extension.common.ZserioExtensionException;

/** FreeMarker data for {@code Structure.h.ftl} and {@code Structure.c.ftl}. */
public final class StructureEmitterTemplateData
{
    private final String generatorDescription;
    private final String typeName;
    private final String funcPrefix;
    private final String includeGuard;
    private final String headerIncludePath;
    private final List<ParameterData> parameters;
    private final List<FieldFormatter.FieldData> fields;
    private final List<PackingFieldFormatter.PackedField> packedFields;
    private final boolean hasPackingContext;
    private final boolean hasPackingContextMembers;
    private final List<FunctionData> functions;
    private final List<String> extraIncludes;
    private final boolean hasOwnedFields;

    public StructureEmitterTemplateData(TemplateDataContext context, StructureType structType)
            throws ZserioExtensionException
    {
        this.generatorDescription = context.getGeneratorDescription();
        final PackageName pkg = structType.getPackage().getPackageName();
        final String zserioName = structType.getName();
        this.typeName = CIdentifierFormatter.typeName(pkg, zserioName);
        this.funcPrefix = CIdentifierFormatter.funcPrefix(pkg, zserioName);
        this.includeGuard = CIdentifierFormatter.includeGuard(pkg, zserioName);
        this.headerIncludePath = CIdentifierFormatter.headerIncludePath(pkg, zserioName);
        this.parameters = ParameterData.from(structType.getTypeParameters());

        final List<FieldFormatter.FieldData> built = new ArrayList<>();
        final List<PackingFieldFormatter.PackedField> packedBuilt = new ArrayList<>();
        boolean usesContext = false;
        // LinkedHashSet so the include order matches the schema's field
        // order — readable, and stable across re-runs.
        final LinkedHashSet<String> includes = new LinkedHashSet<>();
        // Parameter types may live in other packages; without these
        // includes the generated header would reference an undeclared
        // typedef on every parameter member.
        for (final ParameterData p : this.parameters)
        {
            if (p.getExtraInclude() != null)
            {
                includes.add(p.getExtraInclude());
            }
        }
        boolean owned = false;
        // Pre-scan: every field whose `<expr>:` offset directive names
        // a fixed-width sibling field becomes an "offset source" — its
        // emitter records the writer bit position before its inner
        // write so the anchored field can later back-patch it. Two
        // shapes are handled: an atomic identifier ({@code <name>:}
        // → atomicSourceBits) and an array-element indexed reference
        // ({@code <name>[@index]:} → indexedSourceBits + a per-anchor
        // info record for the anchored array). Both maps are passed
        // through OffsetContext to FieldFormatter so the relevant
        // wraps fire on the right side.
        final FieldFormatter.OffsetContext offsetContext = buildOffsetContext(structType);
        final boolean compoundHasOffsets = !offsetContext.isEmpty();
        for (final Field f : structType.getFields())
        {
            final FieldFormatter.FieldData data =
                    FieldFormatter.format(f, "self->", structType, offsetContext);
            built.add(data);
            final PackingFieldFormatter.PackedField packed =
                    PackingFieldFormatter.format(f, data, "self->", structType, "ctx->");
            packedBuilt.add(packed);
            usesContext = usesContext || packed.getUsesContext();
            if (data.getExtraInclude() != null)
            {
                includes.add(data.getExtraInclude());
            }
            owned = owned || data.getOwned();
        }
        this.fields = built;
        this.packedFields = packedBuilt;
        // A compound emits packing helpers if it is packable per
        // upstream (any field is packable, including arrays whose
        // elements pack). Helpers may carry an empty packing context
        // when no individual field needs a context member (e.g. the
        // only packable leaves live inside arrays which scope their
        // own local contexts).
        //
        // Exception: compounds with `<expr>:` offset directives suppress
        // packed code generation entirely. Offsets demand byte
        // boundaries and need their source field written at its
        // declared bit width for the single-pass back-patch to take
        // effect; delta encoding (used by the packed path) gates each
        // packable field through a context that defeats both
        // properties. Upstream zserio's wire format does not combine
        // offsets with packed encoding either, so this matches
        // semantics — an offset-bearing struct simply cannot be used
        // as an element of a `packed S[]` array. Emitting the packed
        // helpers anyway would compile-fail because the offset
        // back-patch references a position local that the packed
        // write does not declare.
        this.hasPackingContext = !compoundHasOffsets
                && PackingFieldFormatter.hasPackableField(structType);
        this.hasPackingContextMembers = usesContext && !compoundHasOffsets;
        this.functions = FunctionData.from(structType);
        for (final FunctionData fn : this.functions)
        {
            if (fn.getExtraInclude() != null)
            {
                includes.add(fn.getExtraInclude());
            }
        }
        // Pull in every const referenced from this compound's
        // expressions (constraints, optional clauses, parameterised-
        // type args, array lengths, function bodies). Each const lives
        // in its own header and lowers to a `<pkg>_<NAME>` macro that
        // must be in scope wherever it is used.
        includes.addAll(ReferencedConstants.headerIncludePathsFor(structType));
        this.extraIncludes = new ArrayList<>(includes);
        this.hasOwnedFields = owned;
    }

    /**
     * Walk the compound's fields, find every {@code <expr>:} offset
     * directive, and build the {@link FieldFormatter.OffsetContext}
     * that drives all four offset wraps. Three shapes recognised:
     *
     * <ul>
     *   <li>Atomic — {@code <name>:}: the offset source is a sibling
     *       field whose underlying type is fixed-width. Recorded in
     *       {@code atomicSourceBits}.</li>
     *   <li>Indexed — {@code <name>[@index]:}: the offset source is
     *       an array sibling whose element type is fixed-width.
     *       Recorded in {@code indexedSourceBits} (under the source
     *       array's name) and {@code indexedAnchors} (under the
     *       anchored field's name, carrying the per-element wrap
     *       data).</li>
     *   <li>Anything else (dot paths, mixed expressions) is skipped;
     *       the generator emits the field without offset semantics.
     *       Discovery / compile still pass; the wire output won't
     *       honor the directive.</li>
     * </ul>
     */
    private static FieldFormatter.OffsetContext buildOffsetContext(CompoundType compound)
    {
        final Map<String, Integer> atomic = new HashMap<>();
        final Map<String, Integer> indexedSrc = new HashMap<>();
        final Map<String, FieldFormatter.IndexedAnchorInfo> indexedAnchors = new HashMap<>();
        for (final Field f : compound.getFields())
        {
            final Expression off = f.getOffsetExpr();
            if (off == null)
            {
                continue;
            }

            // Atomic: `<name>:` — single-identifier expression
            // resolving to a Field, both operands null.
            if (off.getExprSymbolObject() instanceof Field
                    && off.op1() == null && off.op2() == null)
            {
                final Field src = (Field)off.getExprSymbolObject();
                final Integer bits = fixedBitWidth(src);
                if (bits != null)
                {
                    atomic.put(src.getName(), bits);
                }
                continue;
            }

            // Indexed: `<name>[@index]:` — LBRACKET binary expression
            // whose op1 is an atomic Field reference and op2 is an
            // INDEX atom. The source must be an array; its element
            // type must be fixed-width for the back-patch to derive
            // element[i]'s position from start + i * elemBits.
            if (off.getType() == ZserioParser.LBRACKET
                    && off.op1() != null && off.op2() != null
                    && off.op1().getExprSymbolObject() instanceof Field
                    && off.op2().getType() == ZserioParser.INDEX)
            {
                final Field src = (Field)off.op1().getExprSymbolObject();
                final Integer elemBits = arrayElementFixedBitWidth(src);
                if (elemBits != null)
                {
                    indexedSrc.put(src.getName(), elemBits);
                    indexedAnchors.put(f.getName(),
                            new FieldFormatter.IndexedAnchorInfo(src.getName(),
                                    elemBits.intValue()));
                }
            }
        }
        return new FieldFormatter.OffsetContext(atomic, indexedSrc, indexedAnchors);
    }

    /** Resolve the field's underlying type and return its bit width
     *  if it implements {@link FixedSizeType}, else {@code null}. */
    private static Integer fixedBitWidth(Field field)
    {
        final TypeInstantiation inst = field.getTypeInstantiation();
        ZserioType t = inst.getTypeReference().getBaseTypeReference().getType();
        while (t instanceof Subtype || t instanceof InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((InstantiateType)t).getTypeReference().getBaseTypeReference().getType();
            }
        }
        if (t instanceof FixedSizeType)
        {
            return Integer.valueOf(((FixedSizeType)t).getBitSize());
        }
        return null;
    }

    /** If {@code field} is an array with fixed-width elements, return
     *  the element bit width; otherwise return {@code null}. */
    private static Integer arrayElementFixedBitWidth(Field field)
    {
        final TypeInstantiation inst = field.getTypeInstantiation();
        if (!(inst instanceof ArrayInstantiation))
        {
            return null;
        }
        ZserioType t = ((ArrayInstantiation)inst).getElementTypeInstantiation()
                .getTypeReference().getBaseTypeReference().getType();
        while (t instanceof Subtype || t instanceof InstantiateType)
        {
            if (t instanceof Subtype)
            {
                t = ((Subtype)t).getTypeReference().getBaseTypeReference().getType();
            }
            else
            {
                t = ((InstantiateType)t).getTypeReference().getBaseTypeReference().getType();
            }
        }
        if (t instanceof FixedSizeType)
        {
            return Integer.valueOf(((FixedSizeType)t).getBitSize());
        }
        return null;
    }

    public String  getGeneratorDescription() { return generatorDescription; }
    public String  getTypeName()             { return typeName; }
    public String  getFuncPrefix()           { return funcPrefix; }
    public String  getIncludeGuard()         { return includeGuard; }
    public String  getHeaderIncludePath()    { return headerIncludePath; }
    public List<ParameterData> getParameters() { return parameters; }
    public boolean getHasParameters()        { return !parameters.isEmpty(); }
    public List<FieldFormatter.FieldData> getFields() { return fields; }
    public List<PackingFieldFormatter.PackedField> getPackedFields() { return packedFields; }
    public boolean getHasPackingContext()    { return hasPackingContext; }
    public boolean getHasPackingContextMembers() { return hasPackingContextMembers; }
    public List<FunctionData> getFunctions() { return functions; }
    public boolean getHasFunctions()         { return !functions.isEmpty(); }
    public List<String> getExtraIncludes()   { return extraIncludes; }
    public boolean getHasOwnedFields()       { return hasOwnedFields; }
}
