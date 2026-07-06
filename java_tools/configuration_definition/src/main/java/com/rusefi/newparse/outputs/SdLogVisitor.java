package com.rusefi.newparse.outputs;

import com.rusefi.newparse.layout.BitGroupLayout;
import com.rusefi.newparse.layout.ScalarLayout;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;

import java.io.PrintStream;

public class SdLogVisitor extends OutputChannelVisitorBase {
    private final String mSourceName;

    public SdLogVisitor(String sourceName, String category) {
        // The category (yaml output_name) drives the "Category: Name" label prefix, shared with the ini datalog.
        super(category);
        mSourceName = sourceName;
    }

    @Override
    public void visit(StructLayout struct, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        // The SD log emits C++ field-access expressions, so use array subscripts and ignore byte offsets,
        // rather than the flattened TS names used by the base output-channel visitor.
        if (arrayDims.length == 0) {
            visit(struct, ps, prefixer, offsetAdd, struct.name);
        } else if (arrayDims.length == 1) {
            int elementOffset = offsetAdd;

            for (int i = 0; i < arrayDims[0]; i++) {
                visit(struct, ps, prefixer, elementOffset, struct.name + "[" + i + "]");
                elementOffset += struct.size;
            }
        } else {
            throw new IllegalStateException("Output channels don't support multi dimension arrays");
        }
    }

    private void visitScalar(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, String arraySub, String commentSuffix) {
        final String prefixedName = prefixer.get(scalar.name);

        ps.print("\t{");
        ps.print(mSourceName);
        ps.print(prefixedName);
        ps.print(arraySub);
        ps.print(", \"");

        // Build the label the same way as the ini datalog so the two stay in sync (includes the category prefix).
        ps.print(buildDatalogName(prefixedName, scalar.options.comment));
        ps.print(commentSuffix);
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
            visitScalar(scalar, ps, prefixer, "", "");
        } else if (arrayDims.length == 1) {
            for (int i = 0; i < arrayDims[0]; i++) {
                visitScalar(scalar, ps, prefixer, "[" + i + "]", " " + (i + 1));
            }
        } else {
            throw new IllegalStateException("SD log doesn't support multi dimension arrays");
        }
    }

    @Override
    public void visit(BitGroupLayout bitGroup, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {

    }
}
