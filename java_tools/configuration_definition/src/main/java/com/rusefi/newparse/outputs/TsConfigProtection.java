package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;

import java.io.PrintStream;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Board overrides must also be protected in every tuning dialog and during tune import. */
public class TsConfigProtection {
    private static final String PREFIX = "ts_readonly_";
    private static final Pattern FIELD = Pattern.compile("^(\\s*field\\s*=\\s*(?:\"[^\"]*\"|[^,]+),\\s*)(\\w+)(.*)$");
    private static final Pattern ENABLE = Pattern.compile("^\\s*,\\s*(\\{[^}]*\\}|[^,;]+)(.*)$");
    private final Map<String, String> conditions = new TreeMap<>();

    public TsConfigProtection(ParseState parser) {
        parser.getDefinitions().forEach((name, definition) -> {
            if (name.startsWith(PREFIX)) {
                // ParseState also creates an encoded companion for every integer define.
                if (name.endsWith("_16_hex")) {
                    return;
                }
                String condition = definition.toString().trim();
                if (condition.startsWith("\"") && condition.endsWith("\"")) {
                    condition = condition.substring(1, condition.length() - 1).trim();
                }
                if (condition.isEmpty()) {
                    throw new IllegalArgumentException("Empty read-only condition: " + name);
                }
                conditions.put(name.substring(PREFIX.length()), condition);
            }
        });
    }

    public void writeConstantsExtensions(TsMetadata metadata, PrintStream ps) {
        conditions.forEach((name, condition) -> {
            if (!metadata.hasField(name)) {
                throw new IllegalArgumentException("Unknown read-only TunerStudio constant: " + name);
            }
            // TunerStudio readOnly is unconditional. For revision-dependent overrides, keep
            // the ECU value on tune import and use an enable expression in every field below.
            ps.println("\t" + (condition.equals("1") ? "readOnly" : "controllerPriority") + " = " + name);
        });
    }

    public String protectField(String line) {
        Matcher field = FIELD.matcher(line);
        if (!field.matches()) {
            return line;
        }
        String condition = conditions.get(field.group(2));
        if (condition == null) {
            return line;
        }

        String tail = field.group(3);
        String enable = "1";
        Matcher existing = ENABLE.matcher(tail);
        if (existing.matches()) {
            enable = existing.group(1).trim();
            if (enable.startsWith("{") && enable.endsWith("}")) {
                enable = enable.substring(1, enable.length() - 1).trim();
            }
            tail = existing.group(2);
        }
        // Preserve the existing enable and visibility conditions, including commas inside
        // expressions. Apply this to unconditional fields too so they visibly grey out.
        return (field.group(1) + field.group(2) + ", { (" + enable + ") && !(" + condition + ") }" + tail)
                .replaceFirst("\\s+$", "");
    }
}
