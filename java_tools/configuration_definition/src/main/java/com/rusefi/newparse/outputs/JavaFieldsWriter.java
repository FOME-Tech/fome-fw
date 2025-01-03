package com.rusefi.newparse.outputs;

import com.rusefi.VariableRegistry;
import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.*;
import com.rusefi.newparse.parsing.Definition;
import com.rusefi.newparse.parsing.Struct;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.util.Map;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public class JavaFieldsWriter {
    private final PrintStream ps;

    private final int baseOffset;

    public JavaFieldsWriter(final String outputFile, int baseOffset) throws FileNotFoundException {
        ps = new PrintStreamAlwaysUnix(new FileOutputStream(outputFile));
        this.baseOffset = baseOffset;

        String className = new File(outputFile).getName();
        ps.println("package com.rusefi.config.generated;");
        ps.println();
        ps.println("import com.rusefi.config.*;");
        ps.println();
        ps.println("public class " + className.substring(0, className.indexOf('.')) + " {");
    }

    public void finish() {
        ps.println("}");
        ps.close();
    }

    public void writeDefinitions(final Map<String, Definition> defs) {
        Stream<Map.Entry<String, Definition>> sortedDefs = defs.entrySet().stream().sorted((o1, o2)->o1.getKey().
                compareTo(o2.getKey()));

        for (final Map.Entry<String, Definition> d : sortedDefs.collect(Collectors.toList())) {
            String name = d.getKey();

            if (name.endsWith(VariableRegistry._16_HEX_SUFFIX) || name.endsWith(VariableRegistry._HEX_SUFFIX)) {
                continue;
            }

            ps.print("\tpublic static final ");

            if (d.getValue().isInteger()) {
                ps.print("int " + d.getKey() + " = " + d.getValue().asInt());
            } else if (d.getValue().isNumeric()) {
                ps.print("double " + d.getKey() + " = " + d.getValue().asDouble());
            } else {
                ps.print("String " + d.getKey() + " = " + d.getValue().toString());
            }

            ps.println(";");
        }
    }

    public void writeFields(ParseState parser) {
        // Assume the last struct is the one we want...
        Struct s = parser.getStructs().get(parser.getStructs().size() - 1);

        StructLayout sl = new StructLayout(0, "root", s);

        JavaFieldVisitor visitor = new JavaFieldVisitor();
        StructNamePrefixer prefixer = new StructNamePrefixer('.');

        visitor.visitRoot(sl, ps, prefixer);
    }

    private class JavaFieldVisitor extends ILayoutVisitor {
        public void visitRoot(StructLayout sl, PrintStream ps, StructNamePrefixer prefixer) {
            sl.children.forEach(c -> c.visit(this, ps, prefixer, 0, new int[0]));
        }

        public void visit(StructLayout sl, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            if (arrayDims.length == 0) {

            }
//            } else if (arrayDims.length == 1) {
//                ps.println("\t" + sl.typeName + " " + sl.name + "[" + arrayDims[0] + "];");
//            } else {
//                throw new IllegalStateException("Multi dim array of structs not supported");
//            }
        }

        private String toJavaType(String tsType) {
            switch (tsType) {
                case "S08": return "INT8";
                case "U08": return "INT8";
                case "S16": return "INT16";
                case "U16": return "INT16";
                case "S32": return "INT";
                case "U32": return "INT";
                case "F32": return "FLOAT";
            }

            throw new IllegalArgumentException("toJavaType didn't understand " + tsType);
        }

        private void visit(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int idx) {
            String name = prefixer.get(scalar.name.toUpperCase());
            if (idx != -1) {
                name = name + idx;
            }

            ps.print("\tpublic static final Field ");
            ps.print(name);
            ps.print(" = Field.create(\"");
            ps.print(name);
            ps.print("\", ");
            ps.print(scalar.offset + offsetAdd);
            ps.print(", FieldType.");
            ps.print(toJavaType(scalar.type.tsType));

            // if (scalar.options.scale != 1) {
            if (!scalar.type.tsType.equals("F32")) {
                ps.print(").setScale(");
                ps.print(scalar.options.scale);
            }

            ps.print(").setBaseOffset(");

            // TODO
            ps.print(baseOffset);

            ps.println(");");
        }

        public void visit(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            if (arrayDims.length == 0) {
                visit(scalar, ps, prefixer, offsetAdd, -1);
            } else if (arrayDims.length == 1) {
                int elementOffset = offsetAdd;

                for (int i = 0; i < arrayDims[0]; i++) {
                    visit(scalar, ps, prefixer, elementOffset, i + 1);
                    elementOffset += scalar.type.size;
                }
            } else {
                throw new IllegalStateException("Output channels don't support multi dimension arrays");
            }
        }

        public void visit(BitGroupLayout bitGroup, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        }

        @Override
        public void visit(UnusedLayout struct, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            // Do nothing
        }
    }
}
