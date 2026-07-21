package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.BitGroupLayout;
import com.rusefi.newparse.layout.EnumLayout;
import com.rusefi.newparse.layout.ScalarLayout;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;
import com.rusefi.newparse.parsing.FieldOptions;
import com.rusefi.newparse.parsing.Struct;
import com.rusefi.output.GetOutputValueConsumer;
import com.rusefi.output.variables.VariableRecord;
import com.rusefi.util.LazyOutputStream;

import java.io.IOException;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.List;

public class OutputLookupWriter {
    private final PrintStream ps;

    // private final Map<String, String> mappings = new TreeMap<>();
    private final List<VariableRecord> getterPairs = new ArrayList<>();

    public OutputLookupWriter(final String outputFile, final String functionName) throws IOException {
        ps = new PrintStreamAlwaysUnix(new LazyOutputStream(outputFile));

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

    // baseOffset is the offset of this struct within the whole output channel space, so each field
    // can be looked up by absolute offset via getOutputChannelValue/getOutputChannelBit at runtime -
    // no compile-time address (constexpr) required.
    public void addOutputLookups(ParseState parser, int baseOffset, String conditional) {
        // Assume the last struct is the one we want...
        Struct s = parser.getStructs().get(parser.getStructs().size() - 1);

        StructLayout sl = new StructLayout(0, "root", s);

        OutputLookupVisitor visitor = new OutputLookupVisitor(conditional);
        StructNamePrefixer prefixer = new StructNamePrefixer('.');

        visitor.visitRoot(sl, ps, prefixer, baseOffset);
    }

    private class OutputLookupVisitor extends ILayoutVisitor {
        private final String conditional;

        public OutputLookupVisitor(final String conditional) {
            this.conditional = conditional;
        }

        public void visitRoot(StructLayout sl, PrintStream ps, StructNamePrefixer prefixer, int baseOffset) {
            sl.children.forEach(c -> c.visit(this, ps, prefixer, baseOffset, new int[0]));
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
            String lookup = "getOutputChannelValue(" + (scalar.offset + offsetAdd) + ", LogField::Type::"
                    + scalar.type.tsType + ", " + FieldOptions.tryRound(scalar.options.scale) + ")";

            getterPairs.add(new VariableRecord(userName, lookup, null, conditional));
        }

        @Override
        public void visit(EnumLayout e, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            // Enums are read as plain integers (no scaling)
            String userName = prefixer.get(e.name);
            String lookup = "getOutputChannelValue(" + (e.offset + offsetAdd) + ", LogField::Type::"
                    + e.type.tsType + ", 1)";

            getterPairs.add(new VariableRecord(userName, lookup, null, conditional));
        }

        public void visit(BitGroupLayout bitGroup, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            if (arrayDims.length > 0) {
                return;
            }

            for (int i = 0; i < bitGroup.bits.size(); i++) {
                String userName = prefixer.get(bitGroup.bits.get(i).name);
                String lookup = "getOutputChannelBit(" + (bitGroup.offset + offsetAdd) + ", " + i + ")";

                getterPairs.add(new VariableRecord(userName, lookup, null, conditional));
            }
        }
    }
}
