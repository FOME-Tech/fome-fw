package com.rusefi.newparse.parsing;

import java.util.Arrays;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The list of names for an enum field, as displayed in the TS ini file.
 *
 * Two forms exist: a dense list, where a name's position in the list is its numeric value, and a
 * "compacted" list, where each name is explicitly paired with its numeric value. The compacted form
 * is used for things like pin names, where only the handful of pins that exist on a given board are
 * listed out of the hundreds the enum defines.
 */
public class EnumValues {
    private static final Pattern COMPACTED_ENTRY = Pattern.compile("^(\\d+)\\s*=\\s*(.*)$");

    public final String[] names;

    /**
     * Numeric value of each name, or null if this enum is dense (that is, value == index).
     */
    public final int[] indices;

    public EnumValues(String[] names) {
        this(names, null);
    }

    private EnumValues(String[] names, int[] indices) {
        this.names = names;
        this.indices = indices;
    }

    public int size() {
        return names.length;
    }

    public static EnumValues parse(String rhs) {
        String[] entries = Arrays.stream(rhs.split(","))
                    .map(String::trim)
                    .toArray(String[]::new);

        int compactedCount = 0;

        for (String entry : entries) {
            if (COMPACTED_ENTRY.matcher(entry).matches()) {
                compactedCount++;
            }
        }

        if (compactedCount == 0) {
            return new EnumValues(Arrays.stream(entries).map(EnumValues::unquote).toArray(String[]::new));
        }

        if (compactedCount != entries.length) {
            throw new IllegalStateException("Enum mixes compacted and plain entries: " + rhs);
        }

        String[] names = new String[entries.length];
        int[] indices = new int[entries.length];

        for (int i = 0; i < entries.length; i++) {
            Matcher m = COMPACTED_ENTRY.matcher(entries[i]);

            if (!m.matches()) {
                throw new IllegalStateException("Unexpected enum entry " + entries[i]);
            }

            indices[i] = Integer.parseInt(m.group(1));
            names[i] = unquote(m.group(2).trim());
        }

        return new EnumValues(names, indices);
    }

    private static String unquote(String s) {
        return s.replaceAll("\"", "");
    }
}
