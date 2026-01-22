package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;
import com.rusefi.newparse.parsing.Definition;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class TsWriter {
    private static final Pattern[] REPLACEMENT_PATTERNS = new Pattern[] {
        // matches strings in the form of @@MY_var_123@@
        Pattern.compile("@@([a-zA-Z0-9_]+?)@@"),
        // matches strings in the form of @#OTHER_var_456#@
        Pattern.compile("@#([a-zA-Z0-9_]+?)#@")
    };

    private static final Pattern OPTIONAL_LINE = Pattern.compile("@@if_([a-zA-Z0-9_]+)");

    public void writeTunerstudio(ParseState parser, String inputFile, String outputFile) throws IOException {
        PrintStream ps = new PrintStreamAlwaysUnix(Files.newOutputStream(Paths.get(outputFile)));
        writeTunerstudio(parser, inputFile, ps);
        ps.close();
    }

    public void writeTunerstudio(ParseState parser, String inputFile, PrintStream ps) throws IOException {
        BufferedReader is = new BufferedReader(new FileReader(inputFile));

        while (is.ready()) {
            String line = is.readLine();

            if (line.contains("CONFIG_DEFINITION_START")) {
                writeLayoutAndComments(parser, ps);
                continue;
            }

            // Check if this line has a "skip me" suffix
            Matcher skipMatch = OPTIONAL_LINE.matcher(line);
            if (skipMatch.find()) {
                String varName = skipMatch.group(1);

                Definition def = parser.findDefinition(varName);

                if (def == null || def.toString().equals("false")) {
                    // Definition was missing or false, skip this line
                    continue;
                }

                // Delete that part of the line
                line = line.replace(skipMatch.group(0), "");
            }

            // Don't strip surrounding quotes of the FIRST replace of the line - only do it in nested replacements
            boolean isNested = false;

            // While there is a line to replace, do it
            while (line.contains("@@") || line.contains("@#")) {
                Matcher match = null;
                boolean forceStripQuotes = false;
                for (Pattern p : REPLACEMENT_PATTERNS) {
                    Matcher candidate = p.matcher(line);

                    if (candidate.find()) {
                        match = candidate;

                        if (p.pattern().startsWith("@#")) {
                            forceStripQuotes = true;
                        }
                        break;
                    }
                }

                if (match == null) {
                    throw new RuntimeException("Failed to resolve definition in line: " + line);
                }

                String varName = match.group(1);
                Definition def = parser.findDefinition(varName);

                String replacement = def != null ? def.toString() : "MISSING DEFINITION";

                // Strip off any quotes from the resolved string - we may be trying to concatenate inside a string literal where quotes aren't allowed
                while ((isNested || forceStripQuotes)
                        && ((replacement.startsWith("\"") && replacement.endsWith("\""))
                        || (replacement.startsWith("'") && replacement.endsWith("'")))) {
                    replacement = replacement.substring(1, replacement.length() - 1);
                }

                line = line.replace(match.group(0), replacement);

                if (!isNested) {
                    isNested = true;
                }
            }

            // TODO: remove extra whitespace from the line

            // Copy the line to the output stream
            ps.println(line);
        }

        is.close();
    }

    public void writeLayoutAndComments(ParseState parser, PrintStream ps) {
        StructLayout root = new StructLayout(0, "root", parser.getLastStruct());
        TsMetadata meta = new TsMetadata();

        // Print configuration layout
        int size = root.getSize();
        ps.println("pageSize            = " + size);
        ps.println("page = 1");

        StructNamePrefixer prefixer = new StructNamePrefixer('_');

        TsLayoutVisitor v = new TsLayoutVisitor(meta);
        root.visit(v, ps, prefixer, 0, new int[0]);

        ps.println("; total TS size = " + size);

        // Print context help
        ps.println("[SettingContextHelp]");
        meta.writeComments(ps);
    }
}
