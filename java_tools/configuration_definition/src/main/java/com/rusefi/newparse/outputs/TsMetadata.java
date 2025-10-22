package com.rusefi.newparse.outputs;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.List;

public class TsMetadata {
    private final List<String> comments = new ArrayList<>();
    public void addComment(String name, String comment) {
        if (comment == null) {
            return;
        }

        comment = comment.trim();

        // LEGACY FEATURE: clips off the previously-required +
        if (comment.startsWith("+")) {
            // Clip off leading +, and any leading/trailing whitespace
            comment = comment.substring(1).trim();
        }

        if (comment.isEmpty()) {
            return;
        }

        comments.add("\t" + name + " = \"" + comment + "\"");
    }

    public void writeComments(PrintStream ps) {
        this.comments.forEach(ps::println);
    }
}
