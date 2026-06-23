package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.*;
import com.rusefi.newparse.parsing.Struct;
import com.rusefi.output.GetConfigValueConsumer;

import java.io.IOException;
import java.io.PrintStream;

/**
 * New-parser driven generator for {@code value_lookup_generated.cpp/.md}
 * (the Lua getConfigValueByName/setConfigValueByName lookup). It walks the parsed
 * layout and feeds {@link GetConfigValueConsumer}, reusing its getter/setter/markdown
 * emission so output matches the legacy parser.
 */
public class ConfigValueLookupWriter {
    private static final String EC_PREFIX = "engineConfiguration.";
    private static final String UNUSED = "unused";

    private final GetConfigValueConsumer consumer;

    public ConfigValueLookupWriter(String cppFile, String mdFile) {
        this.consumer = new GetConfigValueConsumer(cppFile, mdFile);
    }

    public void write(ParseState parser) throws IOException {
        // Assume the last struct is the root config (matches the other new-parser writers)
        Struct s = parser.getStructs().get(parser.getStructs().size() - 1);
        StructLayout sl = new StructLayout(0, "root", s);

        Visitor visitor = new Visitor();
        StructNamePrefixer prefixer = new StructNamePrefixer('.');
        sl.children.forEach(c -> c.visit(visitor, null, prefixer, 0, new int[0]));

        consumer.endFile();
    }

    private void add(String dottedPath, String cType, String comment) {
        String userName;
        String fullName;
        if (dottedPath.startsWith(EC_PREFIX)) {
            String rest = dottedPath.substring(EC_PREFIX.length());
            userName = rest;
            fullName = "engineConfiguration->" + rest;
        } else {
            userName = dottedPath;
            fullName = "config->" + dottedPath;
        }
        consumer.addConfigValue(userName, fullName, cType, comment == null ? "" : comment);
    }

    private class Visitor extends ILayoutVisitor {
        @Override
        public void visit(StructLayout struct, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            // arrays of structs are not exposed by name, matching the legacy parser
            if (arrayDims.length != 0) {
                return;
            }
            // Always push the member name (even for struct_no_prefix): value_lookup needs the real
            // C member access path (config->engineConfiguration.field -> engineConfiguration->field),
            // which is independent of the TunerStudio no-prefix naming.
            prefixer.push(struct.name);
            struct.children.forEach(c -> c.visit(this, ps, prefixer, offsetAdd, new int[0]));
            prefixer.pop();
        }

        @Override
        public void visit(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            // arrays are not exposed by name; legacy parser skips them too
            if (arrayDims.length > 0) {
                return;
            }
            if (scalar.name.contains(UNUSED)) {
                return;
            }
            add(prefixer.get(scalar.name), scalar.type.cType, scalar.options != null ? scalar.options.comment : null);
        }

        @Override
        public void visit(BitGroupLayout bitGroup, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            if (arrayDims.length > 0) {
                return;
            }
            for (BitGroupLayout.BitLayout bit : bitGroup.bits) {
                if (bit.name.contains(UNUSED)) {
                    continue;
                }
                add(prefixer.get(bit.name), "bool", bit.comment);
            }
        }

        @Override
        public void visit(UnionLayout union, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            // union members all share the same offset
            union.children.forEach(c -> c.visit(this, ps, prefixer, offsetAdd, new int[0]));
        }

        @Override
        public void visit(EnumLayout e, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            // enums are not exposed as numeric calibrations
        }

        @Override
        public void visit(StringLayout str, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
            // strings are not exposed as numeric calibrations
        }
    }
}
