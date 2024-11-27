package com.rusefi.newparse.outputs;

public class DatalogVisitorBase extends ILayoutVisitor {
    public static String buildDatalogName(String name, String comment) {
        String text = (comment == null || comment.isEmpty()) ? name : comment;

        // Delete anything after a newline
        return text.split("\\\\n")[0];
    }
}
