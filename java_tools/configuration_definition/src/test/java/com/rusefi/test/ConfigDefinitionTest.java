package com.rusefi.test;

import com.rusefi.EnumsReader;
import com.rusefi.RusefiParseErrorStrategy;
import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.outputs.PrintStreamAlwaysUnix;
import com.rusefi.newparse.outputs.TsWriter;
import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;

import static org.junit.Assert.assertTrue;

public class ConfigDefinitionTest {
    private static final String FIRMWARE = "../../firmware";

    /**
     * An enum read out of a real firmware header should turn into a sane TS options list.
     */
    @Test
    public void testEnumIntoType() throws IOException {
        EnumsReader enumsReader = new EnumsReader();
        enumsReader.read(new FileReader(FIRMWARE + File.separator + "controllers/algo/engine_types.h"));

        ParseState state = new ParseState(enumsReader);
        state.updateEnumsFromReader();

        RusefiParseErrorStrategy.parseDefinitionString(state.getListener(),
                "custom engine_type_e 4 bits, U32, @OFFSET@, [0:6], @@engine_type_e_auto_enum@@\n" +
                        "struct_no_prefix myStruct\n" +
                        "engine_type_e engineType;\n" +
                        "end_struct");

        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        PrintStream ps = new PrintStreamAlwaysUnix(baos, true, StandardCharsets.UTF_8.name());
        new TsWriter().writeLayoutAndComments(state, ps);

        String result = baos.toString(StandardCharsets.UTF_8.name());

        assertTrue(result, result.contains("#define ENUM_engine_type_e = \"DEFAULT_FRANKENSO\", "));
        assertTrue(result, result.contains("engineType = bits, U32, 0, [0:6], $ENUM_engine_type_e\n"));
        assertTrue("Seems too long" + result, result.length() < 100000);
    }
}
