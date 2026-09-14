package com.rusefi.newparse.outputs;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.List;
import java.util.HashSet;
import java.util.Set;

public class TsMetadata {
    private final List<String> comments = new ArrayList<>();
    private final Set<String> fields = new HashSet<>();

    public boolean hasField(String name) {
        return fields.contains(name);
    }

    public void addField(String name, String comment) {
        fields.add(name);
        addComment(name, comment);
    }

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
