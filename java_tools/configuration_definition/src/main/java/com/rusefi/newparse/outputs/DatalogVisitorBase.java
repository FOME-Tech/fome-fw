package com.rusefi.newparse.outputs;

import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;

import java.io.PrintStream;

public class DatalogVisitorBase extends ILayoutVisitor {
    public static String buildDatalogName(String name, String comment) {
        String text = (comment == null || comment.isEmpty()) ? name : comment;

        // Delete anything after a newline
        return text.split("\\\\n")[0];
    }

    @Override
    public void visit(StructLayout struct, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        if (arrayDims.length == 0) {
            visit(struct, ps, prefixer, offsetAdd, struct.name);
        } else if (arrayDims.length == 1) {
            int elementOffset = offsetAdd;

            for (int i = 0; i < arrayDims[0]; i++) {
                visit(struct, ps, prefixer, elementOffset, struct.name + (i + 1));
                elementOffset += struct.size;
            }
        } else {
            throw new IllegalStateException("Output channels don't support multi dimension arrays");
        }
    }
}
