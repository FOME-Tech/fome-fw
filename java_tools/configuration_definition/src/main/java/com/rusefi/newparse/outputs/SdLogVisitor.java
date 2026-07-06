package com.rusefi.newparse.outputs;

import com.rusefi.newparse.layout.BitGroupLayout;
import com.rusefi.newparse.layout.EnumLayout;
import com.rusefi.newparse.layout.ScalarLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;
import com.rusefi.newparse.parsing.FieldOptions;

import java.io.PrintStream;

// Emits offset-based LogField descriptors for the SD (MLQ) log. Each field is described by its
// offset into the output-channel space (the same space the main TunerStudio log uses), its type,
// scale, label, units and digits. Structurally this mirrors DatalogVisitor (both extend
// OutputChannelVisitorBase and compute scalar.offset + offsetAdd) so the SD log stays in lockstep
// with the ini datalog. Bit fields are omitted (the MLQ field format has no sub-bit type).
public class SdLogVisitor extends OutputChannelVisitorBase {
    public SdLogVisitor(String category) {
        // The category (yaml output_name) drives the "Category: Name" label prefix, shared with the ini datalog.
        super(category);
    }

    private void visitScalar(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int idx) {
        String nameWithSpace = prefixer.get(idx > 0 ? (scalar.name + " " + idx) : scalar.name);

        String comment = scalar.options.comment;
        String commentWithIndex = (idx <= 0 || comment == null || comment.isEmpty()) ? comment : comment + " " + idx;

        ps.print("\t{");
        ps.print(scalar.offset + offsetAdd);
        ps.print(", LogField::Type::");
        ps.print(scalar.type.tsType);
        ps.print(", ");
        ps.print(FieldOptions.tryRound(scalar.options.scale));
        ps.print(", \"");
        // Build the label exactly like the ini datalog (includes the category prefix).
        ps.print(buildDatalogName(nameWithSpace, commentWithIndex));
        ps.print("\", ");
        ps.print(scalar.options.units);
        ps.print(", ");
        ps.print(scalar.options.digits);
        ps.println("},");
    }

    @Override
    public void visit(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        if (scalar.name.startsWith("unused")) {
            return;
        }

        if (arrayDims.length == 0) {
            visitScalar(scalar, ps, prefixer, offsetAdd, -1);
        } else if (arrayDims.length == 1) {
            int elementOffset = offsetAdd;

            for (int i = 0; i < arrayDims[0]; i++) {
                visitScalar(scalar, ps, prefixer, elementOffset, i + 1);
                elementOffset += scalar.type.size;
            }
        } else {
            throw new IllegalStateException("SD log doesn't support multi dimension arrays");
        }
    }

    @Override
    public void visit(EnumLayout e, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        // Emit an enum as a plain integer channel (no scaling), matching the datalog.
        ps.print("\t{");
        ps.print(e.offset + offsetAdd);
        ps.print(", LogField::Type::");
        ps.print(e.type.tsType);
        ps.print(", 1, \"");
        ps.print(buildDatalogName(prefixer.get(e.name), e.options.comment));
        ps.println("\", \"\", 0},");
    }

    @Override
    public void visit(BitGroupLayout bitGroup, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        // Bit flags are not logged to the SD card (MLQ has no sub-bit field type).
    }
}
