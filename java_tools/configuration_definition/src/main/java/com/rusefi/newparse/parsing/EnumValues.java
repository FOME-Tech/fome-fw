package com.rusefi.newparse.parsing;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The list of names for an enum field, as displayed in the TS ini file.
 *
 * Dense: a name's position in the list is its numeric value. Values that don't mean anything - a
 * pin the board doesn't route anywhere, say - are named INVALID, which TunerStudio hides from the
 * dropdown.
 */
public class EnumValues {
    public final String[] names;

    public EnumValues(String[] names) {
        this.names = names;
    }

    public int size() {
        return names.length;
    }

    /**
     * Write the comma separated list of names as it appears in the TS ini file, ie the right hand
     * side of a "#define ENUM_foo = " line.
     */
    public void writeTsList(PrintStream ps) {
        for (int i = 0; i < names.length; i++) {
            if (i != 0) {
                ps.print(", ");
            }

            ps.print('"');
            ps.print(names[i]);
            ps.print('"');
        }
    }

    public static EnumValues parse(String rhs) {
        return new EnumValues(Arrays.stream(splitEntries(rhs)).map(EnumValues::unquote).toArray(String[]::new));
    }

    /**
     * Split on commas, ignoring any that are inside a quoted name - plenty of names have a comma in
     * them, like "105 - IDLE rev A,B".
     */
    private static String[] splitEntries(String rhs) {
        List<String> entries = new ArrayList<>();
        StringBuilder entry = new StringBuilder();
        boolean inQuotes = false;

        for (int i = 0; i < rhs.length(); i++) {
            char c = rhs.charAt(i);

            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ',' && !inQuotes) {
                entries.add(entry.toString().trim());
                entry.setLength(0);
                continue;
            }

            entry.append(c);
        }

        entries.add(entry.toString().trim());

        return entries.toArray(new String[0]);
    }

    private static String unquote(String s) {
        return s.replaceAll("\"", "");
    }
}
