package com.rusefi.test;

import com.rusefi.BitState;
import com.rusefi.ReaderStateImpl;
import com.rusefi.newparse.outputs.OutputChannelWriter;
import com.rusefi.output.DataLogConsumer;
import com.rusefi.output.GetOutputValueConsumer;
import com.rusefi.output.OutputsSectionConsumer;
import org.jetbrains.annotations.NotNull;
import org.junit.Test;

import java.io.IOException;

import static com.rusefi.test.newParse.NewParseHelper.parseToOutputChannels;
import static org.junit.Assert.assertEquals;

public class OutputsTest {
    @Test
    public void generateSomething() throws IOException {
        String test =
                "#define GAUGE_NAME_FUEL_WALL_CORRECTION \"wall\"\n" +
                "struct total\n" +
                "float afr_type;PID dTime;\"ms\",      1,      0,       0, 3000,      0\n" +
                "uint8_t afr_typet;@@GAUGE_NAME_FUEL_WALL_CORRECTION@@;\"ms\",      1,      0,       0, 3000,      0\n" +
                "bit isForcedInduction;isForcedInduction\\nDoes the vehicle have a turbo or supercharger?\n" +
                "bit enableFan1WithAc;+Turn on this fan when AC is on.\n" +
                "angle_t m_requested_pump;Computed requested pump \n" +
                "float tCharge;speed density\n" +
                "end_struct\n";

        String expected = "root_afr_type = scalar, F32, 0, \"ms\", 1, 0\n" +
                "root_afr_typet = scalar, U08, 4, \"ms\", 1, 0\n" +
                "root_isForcedInduction = bits, U32, 8, [0:0]\n" +
                "root_enableFan1WithAc = bits, U32, 8, [1:1]\n" +
                "root_m_requested_pump = scalar, F32, 12, \"\", 1, 0\n" +
                "root_tCharge = scalar, F32, 16, \"\", 1, 0\n" +
                "; total TS size = 20\n";
        assertEquals(expected, parseToOutputChannels(test));

        String expectedLegacy = "afr_type = scalar, F32, 0, \"ms\", 1, 0\n" +
                "afr_typet = scalar, U08, 4, \"ms\", 1, 0\n" +
                "isForcedInduction = bits, U32, 8, [0:0]\n" +
                "enableFan1WithAc = bits, U32, 8, [1:1]\n" +
                "m_requested_pump = scalar, F32, 12, \"\", 1, 0\n" +
                "tCharge = scalar, F32, 16, \"\", 1, 0\n" +
                "; total TS size = 20\n";
        assertEquals(expectedLegacy, runOriginalImplementation(test, new ReaderStateImpl()).getContent());
    }

    @Test(expected = BitState.TooManyBitsInARow.class)
    public void tooManyBits() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 40; i++)
            sb.append("bit b" + i + "\n");
        String test = "struct total\n" +
                sb +
                "end_struct\n";
        runOriginalImplementation(test);
    }

    /**
     * while we have {@link OutputChannelWriter} here we use the current 'legacy' implementation
     */
    private static OutputsSectionConsumer runOriginalImplementation(String test) {
        ReaderStateImpl state = new ReaderStateImpl();

        return runOriginalImplementation(test, state);
    }

    @NotNull
    private static OutputsSectionConsumer runOriginalImplementation(String test, ReaderStateImpl state) {
        OutputsSectionConsumer tsProjectConsumer = new OutputsSectionConsumer(null);
        state.readBufferedReader(test, tsProjectConsumer);
        return tsProjectConsumer;
    }

    @Test
    public void generateDataLog() {
        String test = "struct total\n" +
                "bit issue_294_31,\"si_example\",\"nada_example\"\n" +
                "uint8_t[2 iterate] autoscale knock;;\"\",1, 0, 0, 0, 0\n" +
                "uint8_t[2 iterate] autoscale withName;\"MyNameIsEarl\";\"\",1, 0, 0, 0, 0\n" +
                "\tuint16_t autoscale baseFuel;@@GAUGE_NAME_FUEL_BASE@@\\nThis is the raw value we take from the fuel map or base fuel algorithm, before the corrections;\"mg\",{1/@@PACK_MULT_PERCENT@@}, 0, 0, 0, 0\n" +
                "float afr_type;PID dTime;\"ms\",      1,      0,       0, 3000,      0\n" +
                "uint16_t autoscale speedToRpmRatio;s2rpm;\"value\",{1/@@PACK_MULT_PERCENT@@}, 0, 0, 0, 0\n" +
                "uint8_t afr_typet;;\"ms\",      1,      0,       0, 3000,      0\n" +
                "uint8_t autoscale vehicleSpeedKph;;\"kph\",1, 0, 0, 0, 0\n" +
                "bit isBrakePedalDown;is pedal down?\n" +
                "\tuint8_t unused37;;\"\",1, 0, 0, 0, 0\n" +
                "bit enableFan1WithAc;+Turn on this fan when AC is on.\n" +
                "end_struct\n";
        ReaderStateImpl state = new ReaderStateImpl();
        state.getVariableRegistry().register("PACK_MULT_PERCENT", 100);
        state.getVariableRegistry().register("GAUGE_NAME_FUEL_BASE", "hello");

        DataLogConsumer dataLogConsumer = new DataLogConsumer(null);
        state.readBufferedReader(test, dataLogConsumer);
        assertEquals(
                "entry = issue_294_31, \"issue_294_31\", int,    \"%d\"\n" +
                        "entry = knock1, \"knock 1\", int,    \"%d\"\n" +
                        "entry = knock2, \"knock 2\", int,    \"%d\"\n" +
                        "entry = withName1, \"MyNameIsEarl 1\", int,    \"%d\"\n" +
                        "entry = withName2, \"MyNameIsEarl 2\", int,    \"%d\"\n" +
                        "entry = baseFuel, \"hello\", float,  \"%.3f\"\n" +
                        "entry = afr_type, \"PID dTime\", float,  \"%.3f\"\n" +
                        "entry = speedToRpmRatio, \"s2rpm\", float,  \"%.3f\"\n" +
                        "entry = afr_typet, \"afr_typet\", int,    \"%d\"\n" +
                        "entry = vehicleSpeedKph, \"vehicleSpeedKph\", int,    \"%d\"\n" +
                        "entry = isBrakePedalDown, \"is pedal down?\", int,    \"%d\"\n" +
                        "entry = enableFan1WithAc, \"+Turn on this fan when AC is on.\", int,    \"%d\"\n", dataLogConsumer.getContent());

    }

    @Test
    public void generateDataLogMultiLineCommentWithQuotes() {
        String test = "#define GAUGE_NAME_FUEL_BASE \"fuel: base mass\"\n" +
                "struct total\n" +
                "\tuint16_t autoscale baseFuel;@@GAUGE_NAME_FUEL_BASE@@\\nThis is the raw value we take from the fuel map or base fuel algorithm, before the corrections;\"mg\",1, 0, 0, 0, 0\n" +
                "\tuint16_t autoscale baseFuel2;\"line1\\nline2\";\"mg\",1, 0, 0, 0, 0\n" +
                "end_struct\n";
        ReaderStateImpl state = new ReaderStateImpl();

        DataLogConsumer dataLogConsumer = new DataLogConsumer(null);
        state.readBufferedReader(test, dataLogConsumer);

        assertEquals("\"fuel: base mass\"", state.getVariableRegistry().get("GAUGE_NAME_FUEL_BASE"));
        assertEquals(
                "entry = baseFuel, \"fuel: base mass\", int,    \"%d\"\n" +
                        "entry = baseFuel2, \"line1\", int,    \"%d\"\n"
                , dataLogConsumer.getContent());

    }

    @Test
    public void generateGetOutputs() {
        String test = "struct_no_prefix ts_outputs_s\n" +
                "bit issue_294_31,\"si_example\",\"nada_example\"\n" +
                "bit enableFan1WithAc;+Turn on this fan when AC is on.\n" +
                "int hwChannel;\n" +
                "end_struct\n";
        ReaderStateImpl state = new ReaderStateImpl();

        GetOutputValueConsumer outputValueConsumer = new GetOutputValueConsumer(null);
        outputValueConsumer.conditional = "EFI_BOOST_CONTROL";
        state.readBufferedReader(test, (outputValueConsumer));
        assertEquals(
                "#if !EFI_UNIT_TEST\n" +
                "#include \"pch.h\"\n" +
                        "#include \"value_lookup.h\"\n" +
                        "float getOutputValueByName(const char *name) {\n" +
                        "\tint hash = djb2lowerCase(name);\n" +
                        "\tswitch(hash) {\n" +
                        "#if EFI_BOOST_CONTROL\n" +
                        "\t\tcase -1571463185:\n" +
                        "\t\t\treturn engine->outputChannels.issue_294_31;\n" +
                        "#endif\n" +
                        "#if EFI_BOOST_CONTROL\n" +
                        "\t\tcase -298185774:\n" +
                        "\t\t\treturn engine->outputChannels.enableFan1WithAc;\n" +
                        "#endif\n" +
                        "#if EFI_BOOST_CONTROL\n" +
                        "\t\tcase -709106787:\n" +
                        "\t\t\treturn engine->outputChannels.hwChannel;\n" +
                        "#endif\n" +
                        "\t}\n" +
                        "\treturn EFI_ERROR_CODE;\n" +
                        "}\n" +
                        "#endif\n", outputValueConsumer.getContent());
    }

    @Test
    public void sensorStruct() {
        String test = "struct total\n" +
                "    struct pid_status_s\n" +
                "    \tfloat iTerm;;\"v\", 1, 0, -10000, 10000, 4, @@GAUGE_CATEGORY@@\n" +
                "    \tfloat dTerm;;\"v\", 1, 0, -10000, 10000, 4, @@GAUGE_CATEGORY@@\n" +
                "    end_struct\n" +
                "\tpid_status_s alternatorStatus\n" +
                "\tpid_status_s idleStatus\n" +
                "end_struct\n";

        ReaderStateImpl state = new ReaderStateImpl();
        state.getVariableRegistry().register("GAUGE_CATEGORY", "Alternator");
        DataLogConsumer dataLogConsumer = new DataLogConsumer(null);
        state.readBufferedReader(test, dataLogConsumer);
        assertEquals(
                "entry = alternatorStatus_iTerm, \"alternatorStatus_iTerm\", float,  \"%.3f\"\n" +
                        "entry = alternatorStatus_dTerm, \"alternatorStatus_dTerm\", float,  \"%.3f\"\n" +
                        "entry = idleStatus_iTerm, \"idleStatus_iTerm\", float,  \"%.3f\"\n" +
                        "entry = idleStatus_dTerm, \"idleStatus_dTerm\", float,  \"%.3f\"\n",
                dataLogConsumer.getContent());
    }

    @Test
    public void testLongTooltipsIterate() {
        ReaderStateImpl state = new ReaderStateImpl();
        String test = "struct total\n" +
                "\tint[3 iterate] triggerSimulatorPins;Each rusEFI piece can provide synthetic trigger signal for external ECU. Sometimes these wires are routed back into trigger inputs of the same rusEFI board.\\nSee also directSelfStimulation which is different.\n" +
                "end_struct\n";
        TestTSProjectConsumer tsProjectConsumer = new TestTSProjectConsumer("", state);
        state.readBufferedReader(test, tsProjectConsumer);
        assertEquals(
"\ttriggerSimulatorPins1 = \"Each rusEFI piece can provide synthetic trigger signal for external ECU. Sometimes these wires are routed back into trigger inputs of the same rusEFI board.\\nSee also directSelfStimulation which is different. 1\"\n" +
        "\ttriggerSimulatorPins2 = \"Each rusEFI piece can provide synthetic trigger signal for external ECU. Sometimes these wires are routed back into trigger inputs of the same rusEFI board.\\nSee also directSelfStimulation which is different. 2\"\n" +
        "\ttriggerSimulatorPins3 = \"Each rusEFI piece can provide synthetic trigger signal for external ECU. Sometimes these wires are routed back into trigger inputs of the same rusEFI board.\\nSee also directSelfStimulation which is different. 3\"\n", tsProjectConsumer.getSettingContextHelpForUnitTest());
    }

    @Test(expected = IllegalStateException.class)
    public void nameDuplicate() {
        String test = "struct total\n" +
                "float afr_type;PID dTime;\"ms\",      1,      0,       0, 3000,      0\n" +
                "uint8_t afr_type;123;\"ms\",      1,      0,       0, 3000,      0\n" +
                "end_struct\n";


        String expectedLegacy = "afr_type = scalar, F32, 0, \"ms\", 1, 0\n" +
                "afr_type = scalar, U08, 0, \"ms\", 1, 0\n" +
                "; total TS size = 1\n";
        assertEquals(expectedLegacy, runOriginalImplementation(test).getContent());
    }

    @Test
    public void nameNotDuplicate() {
        String test = "struct total\n" +
                "float afr_type;PID dTime;\"ms\",      1,      0,       0, 3000,      0\n" +
                "struct afr_type\n" +
                "float afr_type2;PID dTime;\"ms\",      1,      0,       0, 3000,      0\n" +
                "end_struct\n" +
                "end_struct\n";

        runOriginalImplementation(test);
    }
}
