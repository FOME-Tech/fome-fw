package com.rusefi.newparse.outputs;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.List;

public class TsMetadata {
    private final List<String> comments = new ArrayList<>();
    private final List<String> constantsExtensions = new ArrayList<>();

    /**
     * Records a line for the [ConstantsExtensions] section. Written where the ini template places the
     * CONSTANTS_EXTENSIONS_GENERATED marker, which must be inside that section.
     */
    public void addConstantsExtension(String line) {
        constantsExtensions.add("\t" + line);
    }

    public void writeConstantsExtensions(PrintStream ps) {
        this.constantsExtensions.forEach(ps::println);
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
