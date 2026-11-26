package com.rusefi.test.newParse;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.outputs.JavaFieldsWriter;
import com.rusefi.newparse.outputs.PrintStreamAlwaysUnix;
import org.junit.Assert;
import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

import static com.rusefi.test.newParse.NewParseHelper.parse;

public class JavaFieldsTest {
    private static final String HEADER = "package com.rusefi.config.generated;\n" +
            "\n" +
            "import com.rusefi.config.*;\n" +
            "\n" +
            "public class Test {\n";

    private static String parseToJavaFields(String input) throws IOException {
        ParseState state = parse(input);

        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        String utf8 = StandardCharsets.UTF_8.name();
        PrintStream ps = new PrintStreamAlwaysUnix(baos, true, utf8);

        JavaFieldsWriter writer = new JavaFieldsWriter(ps, "Test", 0);
        writer.writeFields(state);
        writer.finish();

        return baos.toString(utf8);
    }

    @Test
    public void scalars() throws IOException {
        String input = "struct_no_prefix myStruct\n" +
                "uint8_t first;;\"\", 1, 0, 0, 100, 0\n" +
                "int16_t second;;\"\", 1, 0, 0, 100, 0\n" +
                "float third;;\"\", 1, 0, 0, 100, 0\n" +
                "end_struct";

        Assert.assertEquals(HEADER +
                "\tpublic static final Field FIRST = Field.create(\"FIRST\", 0, FieldType.INT8).setBaseOffset(0);\n" +
                "\tpublic static final Field SECOND = Field.create(\"SECOND\", 2, FieldType.INT16).setBaseOffset(0);\n" +
                "\tpublic static final Field THIRD = Field.create(\"THIRD\", 4, FieldType.FLOAT).setBaseOffset(0);\n" +
                "}\n", parseToJavaFields(input));
    }

    @Test
    public void bits() throws IOException {
        String input = "struct_no_prefix myStruct\n" +
                "uint8_t before;;\"\", 1, 0, 0, 100, 0\n" +
                "bit first\n" +
                "bit unusedBit_1_1\n" +
                "bit third,\"a\",\"b\"\n" +
                "end_struct";

        // Bit index is the position in the group, unused bits are skipped but still take their position
        Assert.assertEquals(HEADER +
                "\tpublic static final Field BEFORE = Field.create(\"BEFORE\", 0, FieldType.INT8).setBaseOffset(0);\n" +
                "\tpublic static final Field FIRST = Field.create(\"FIRST\", 4, FieldType.BIT, 0).setBaseOffset(0);\n" +
                "\tpublic static final Field THIRD = Field.create(\"THIRD\", 4, FieldType.BIT, 2).setBaseOffset(0);\n" +
                "}\n", parseToJavaFields(input));
    }

    @Test
    public void enumsShareOneValueList() throws IOException {
        String input = "#define pin_enum \"NONE\", \"INVALID\", \"Ign 1\", \"Digital 3, or 4\"\n" +
                "custom pin_e 2 bits, U16, @OFFSET@, [0:7], @@pin_enum@@\n" +
                "custom mode_e 1 bits, U08, @OFFSET@, [0:1], \"off\", \"on\"\n" +
                "struct_no_prefix myStruct\n" +
                "pin_e firstPin;\n" +
                "mode_e mode;\n" +
                "pin_e secondPin;\n" +
                "end_struct";

        Assert.assertEquals(HEADER +
                "\tpublic static final String ENUM_VALUES_pin_e = \"NONE\\nINVALID\\nIgn 1\\nDigital 3, or 4\";\n" +
                "\tpublic static final String ENUM_VALUES_mode_e = \"off\\non\";\n" +
                "\tpublic static final Field FIRSTPIN = Field.create(\"FIRSTPIN\", 0, FieldType.INT16).setBaseOffset(0).setEnumValues(ENUM_VALUES_pin_e);\n" +
                "\tpublic static final Field MODE = Field.create(\"MODE\", 2, FieldType.INT8).setBaseOffset(0).setEnumValues(ENUM_VALUES_mode_e);\n" +
                "\tpublic static final Field SECONDPIN = Field.create(\"SECONDPIN\", 4, FieldType.INT16).setBaseOffset(0).setEnumValues(ENUM_VALUES_pin_e);\n" +
                "}\n", parseToJavaFields(input));
    }

    @Test
    public void enumArrayIsNumberedFromOne() throws IOException {
        String input = "#define pin_enum \"NONE\", \"Ign 1\"\n" +
                "custom pin_e 2 bits, U16, @OFFSET@, [0:7], @@pin_enum@@\n" +
                "struct_no_prefix myStruct\n" +
                "uint16_t before;;\"\", 1, 0, 0, 100, 0\n" +
                "pin_e[3 iterate] pins;\n" +
                "end_struct";

        Assert.assertEquals(HEADER +
                "\tpublic static final String ENUM_VALUES_pin_e = \"NONE\\nIgn 1\";\n" +
                "\tpublic static final Field BEFORE = Field.create(\"BEFORE\", 0, FieldType.INT16).setBaseOffset(0);\n" +
                "\tpublic static final Field PINS1 = Field.create(\"PINS1\", 2, FieldType.INT16).setBaseOffset(0).setEnumValues(ENUM_VALUES_pin_e);\n" +
                "\tpublic static final Field PINS2 = Field.create(\"PINS2\", 4, FieldType.INT16).setBaseOffset(0).setEnumValues(ENUM_VALUES_pin_e);\n" +
                "\tpublic static final Field PINS3 = Field.create(\"PINS3\", 6, FieldType.INT16).setBaseOffset(0).setEnumValues(ENUM_VALUES_pin_e);\n" +
                "}\n", parseToJavaFields(input));
    }

    @Test
    public void structPrefixAppliesToBitsAndEnums() throws IOException {
        String input = "custom mode_e 1 bits, U08, @OFFSET@, [0:1], \"off\", \"on\"\n" +
                "struct inner\n" +
                "mode_e mode;\n" +
                "bit flag\n" +
                "end_struct\n" +
                "struct_no_prefix myStruct\n" +
                "inner idle;\n" +
                "end_struct";

        Assert.assertEquals(HEADER +
                "\tpublic static final String ENUM_VALUES_mode_e = \"off\\non\";\n" +
                "\tpublic static final Field IDLE_MODE = Field.create(\"IDLE_MODE\", 0, FieldType.INT8).setBaseOffset(0).setEnumValues(ENUM_VALUES_mode_e);\n" +
                "\tpublic static final Field IDLE_FLAG = Field.create(\"IDLE_FLAG\", 4, FieldType.BIT, 0).setBaseOffset(0);\n" +
                "}\n", parseToJavaFields(input));
    }
}
