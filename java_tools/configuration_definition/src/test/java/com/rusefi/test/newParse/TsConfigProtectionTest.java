package com.rusefi.test.newParse;

import com.rusefi.newparse.outputs.TsWriter;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import static org.junit.Assert.*;

public class TsConfigProtectionTest {
    @Rule
    public TemporaryFolder temporaryFolder = new TemporaryFolder();

    private String generate(String definitions, String fields) throws Exception {
        File template = temporaryFolder.newFile();
        Files.write(template.toPath(), ("; CONFIG_DEFINITION_START\n" +
                "[ConstantsExtensions]\n; CONFIG_READ_ONLY\n[UserDefined]\n" + fields)
                .getBytes(StandardCharsets.UTF_8));
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        new TsWriter().writeTunerstudio(NewParseHelper.parse(definitions), template.getPath(),
                new PrintStream(output, true, StandardCharsets.UTF_8.name()));
        return output.toString(StandardCharsets.UTF_8.name());
    }

    private static final String CONFIG = "custom name_t 16 string, ASCII, @OFFSET@, 16\n" +
            "struct_no_prefix config\n" +
            "int pin\nint freePin\nbit enabled\nname_t name\nend_struct\n";

    @Test
    public void fixedSettingIsProtectedInEveryDialogAndOnImport() throws Exception {
        String result = generate("#define ts_readonly_pin 1\n" + CONFIG,
                "dialog = fullPinout\nfield = \"Pin, fixed\", pin\n" +
                "dialog = hardware\nfield = pin, pin, { enabled }, { freePin != 0 }\n" +
                "field = \"Free pin\", freePin\n");
        assertTrue(result.contains("[ConstantsExtensions]\n\treadOnly = pin\n"));
        assertTrue(result.contains("field = \"Pin, fixed\", pin, { (1) && !(1) }"));
        assertTrue(result.contains("field = pin, pin, { (enabled) && !(1) }, { freePin != 0 }"));
        assertTrue(result.contains("field = \"Free pin\", freePin\n"));
        assertFalse(result.contains("readOnly = freePin"));
    }

    @Test
    public void revisionConditionPreservesEnableAndVisibilityExpressions() throws Exception {
        String result = generate("#define ts_readonly_pin \"hellenBoardId != -1\"\n" + CONFIG,
                "field = \"Pin\", pin, { selectExpression(enabled, 0, 1) }, { freePin != 0 }\n" +
                "field = \"Pin alias\", pin, 1, { enabled }\n");
        assertTrue(result.contains("\tcontrollerPriority = pin\n"));
        assertFalse(result.contains("\treadOnly = pin\n"));
        assertTrue(result.contains("{ (selectExpression(enabled, 0, 1)) && !(hellenBoardId != -1) }, { freePin != 0 }"));
        assertTrue(result.contains("{ (1) && !(hellenBoardId != -1) }, { enabled }"));
    }

    @Test
    public void optionalFieldsAreFilteredBeforeProtection() throws Exception {
        String result = generate("#define ts_readonly_pin 1\n#define show_pin true\n" + CONFIG,
                "field = \"Visible\", pin @@if_show_pin\n" +
                "field = \"Hidden\", pin @@if_show_missing\n");
        assertTrue(result.contains("field = \"Visible\", pin, { (1) && !(1) }"));
        assertFalse(result.contains("Hidden"));
        assertFalse(result.contains("@@"));
    }

    @Test
    public void bitAndStringConstantsWithoutHelpAreRecognized() throws Exception {
        String result = generate("#define ts_readonly_enabled 1\n#define ts_readonly_name 1\n" + CONFIG, "");
        assertTrue(result.contains("\treadOnly = enabled\n"));
        assertTrue(result.contains("\treadOnly = name\n"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void misspelledConstantFailsGeneration() throws Exception {
        generate("#define ts_readonly_pinTypo 1\n" + CONFIG, "");
    }

    @Test
    public void boardWithoutOverridesKeepsEditableFields() throws Exception {
        String fields = "field = \"Pin\", pin, { enabled }, { freePin != 0 }\n";
        String result = generate(CONFIG, fields);
        assertTrue(result.endsWith(fields));
        assertFalse(result.contains("readOnly ="));
        assertFalse(result.contains("controllerPriority ="));
    }
}
