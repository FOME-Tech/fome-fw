package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.BitGroupLayout;
import com.rusefi.newparse.layout.ScalarLayout;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;
import com.rusefi.newparse.parsing.Struct;
import com.rusefi.output.GetOutputValueConsumer;
import com.rusefi.output.variables.VariableRecord;

import java.io.IOException;
import java.io.PrintStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class OutputLookupWriter {
    private final PrintStream ps;

    // private final Map<String, String> mappings = new TreeMap<>();
    private final List<VariableRecord> getterPairs = new ArrayList<>();

    public OutputLookupWriter(final String outputFile, final String functionName) throws IOException {
        ps = new PrintStreamAlwaysUnix(Files.newOutputStream(Paths.get(outputFile)));

        ps.println("#if !EFI_UNIT_TEST");
        ps.println("#include \"pch.h\"");
        ps.println("#include \"value_lookup.h\"");
        ps.print("expected<float> ");
        ps.print(functionName);
        ps.println("(const char *name) {");
        ps.println("\tint hash = djb2lowerCase(name);");
        ps.println("\tswitch(hash) {");
    }

    public void endFile() {
        StringBuilder switchBody = new StringBuilder();
        GetOutputValueConsumer.getGetters(switchBody, getterPairs);
        ps.print(switchBody);

        ps.println("\t}");
        ps.println("\treturn unexpected;");
        ps.println("}");
        ps.println("#endif");
        ps.close();
    }

    public void addOutputLookups(ParseState parser, String name, String conditional) {
        // Assume the last struct is the one we want...
        Struct s = parser.getStructs().get(parser.getStructs().size() - 1);

        StructLayout sl = new StructLayout(0, "root", s);

        OutputLookupVisitor visitor = new OutputLookupVisitor(name, conditional);
        StructNamePrefixer prefixer = new StructNamePrefixer('.');

        visitor.visitRoot(sl, ps, prefixer);
    }

    private class OutputLookupVisitor extends ILayoutVisitor {
        private final String prefix;

        private final String conditional;

        public OutputLookupVisitor(final String prefix, final String conditional) {
            this.prefix = prefix;
            this.conditional = conditional;
        }

        public void visitRoot(StructLayout sl, PrintStream ps, StructNamePrefixer prefixer) {
            sl.children.forEach(c -> c.visit(this, ps, prefixer, 0, new int[0]));
        }

        public void visit(StructLayout struct, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            if (arrayDims.length == 0) {
                visit(struct, ps, prefixer, offsetAdd, struct.name);
            }
        }

        public void visit(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            if (arrayDims.length > 0) {
                return;
            }

            if (scalar.name.startsWith("unused")) {
                return;
            }

            String userName = prefixer.get(scalar.name);
            String lookup = prefix + userName;

            getterPairs.add(new VariableRecord(userName, lookup, null, conditional));
        }

        public void visit(BitGroupLayout bitGroup, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            if (arrayDims.length > 0) {
                return;
            }

            for (int i = 0; i < bitGroup.bits.size(); i++) {
                String userName = prefixer.get(bitGroup.bits.get(i).name);
                String lookup = prefix + userName;

                getterPairs.add(new VariableRecord(userName, lookup, null, conditional));
            }
        }
    }
}
