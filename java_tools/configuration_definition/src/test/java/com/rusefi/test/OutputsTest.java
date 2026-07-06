package com.rusefi.test;

import com.rusefi.ReaderStateImpl;
import com.rusefi.output.GetOutputValueConsumer;
import org.junit.Ignore;
import org.junit.Test;

import java.io.IOException;

import static com.rusefi.test.newParse.NewParseHelper.parseToDatalogs;
import static com.rusefi.test.newParse.NewParseHelper.parseToOutputChannels;
import static com.rusefi.test.newParse.NewParseHelper.parseToSdLog;
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
    }

    @Test
    public void generateDataLog() throws IOException {
        String test =
                "#define PACK_MULT_PERCENT 100\n" +
                "#define GAUGE_NAME_FUEL_BASE \"hello\"\n" +
                "struct_no_prefix total\n" +
                "bit issue_294_31,\"si_example\",\"nada_example\"\n" +
                "uint8_t[2 iterate] autoscale knock;;\"\",1, 0, 0, 0, 0\n" +
                "uint8_t[2 iterate] autoscale withName;\"MyNameIsEarl\";\"\",1, 0, 0, 0, 0\n" +
                "\tuint16_t autoscale baseFuel;@@GAUGE_NAME_FUEL_BASE@@\\nThis is the raw value we take from the fuel map or base fuel algorithm, before the corrections;\"mg\",{1/@@PACK_MULT_PERCENT@@}, 0, 0, 0, 0\n" +
                "float afr_type;PID dTime;\"ms\",      1,      0,       0, 3000,      1\n" +
                "uint16_t autoscale speedToRpmRatio;s2rpm;\"value\",{1/@@PACK_MULT_PERCENT@@}, 0, 0, 0, 2\n" +
                "uint8_t afr_typet;;\"ms\",      1,      0,       0, 3000,      0\n" +
                "uint8_t autoscale vehicleSpeedKph;;\"kph\",1, 0, 0, 0, 0\n" +
                "bit isBrakePedalDown;is pedal down?\n" +
                "\tuint8_t unused37;;\"\",1, 0, 0, 0, 0\n" +
                "bit enableFan1WithAc;+Turn on this fan when AC is on.\n" +
                "end_struct\n";
        ReaderStateImpl state = new ReaderStateImpl();

        assertEquals(
                "entry = issue_294_31,\"issue_294_31\",int,\"%d\"\n" +
                        "entry = knock1,\"knock 1\",int,\"%d\"\n" +
                        "entry = knock2,\"knock 2\",int,\"%d\"\n" +
                        "entry = withName1,\"MyNameIsEarl 1\",int,\"%d\"\n" +
                        "entry = withName2,\"MyNameIsEarl 2\",int,\"%d\"\n" +
                        "entry = baseFuel,\"hello\",float,\"%.0f\"\n" +
                        "entry = afr_type,\"PID dTime\",float,\"%.1f\"\n" +
                        "entry = speedToRpmRatio,\"s2rpm\",float,\"%.2f\"\n" +
                        "entry = afr_typet,\"afr_typet\",int,\"%d\"\n" +
                        "entry = vehicleSpeedKph,\"vehicleSpeedKph\",int,\"%d\"\n" +
                        "entry = isBrakePedalDown,\"is pedal down?\",int,\"%d\"\n" +
                        "entry = enableFan1WithAc,\"+Turn on this fan when AC is on.\",int,\"%d\"\n", parseToDatalogs(test));
    }

    @Test
    public void generateDataLogMultiLineCommentWithQuotes() throws IOException {
        String test = "#define GAUGE_NAME_FUEL_BASE \"fuel: base mass\"\n" +
                "struct_no_prefix total\n" +
                "\tuint16_t autoscale baseFuel;@@GAUGE_NAME_FUEL_BASE@@\\nThis is the raw value we take from the fuel map or base fuel algorithm, before the corrections;\"mg\",1, 0, 0, 0, 0\n" +
                "\tuint16_t autoscale baseFuel2;\"line1\\nline2\";\"mg\",1, 0, 0, 0, 0\n" +
                "end_struct\n";

        assertEquals(
                "entry = baseFuel,\"fuel: base mass\",int,\"%d\"\n" +
                        "entry = baseFuel2,\"line1\",int,\"%d\"\n"
                , parseToDatalogs(test));

    }

    @Test
    public void generateSdLogWithCategory() throws IOException {
        String test =
                "struct_no_prefix total\n" +
                "float target;Target;\"kPa\", 1, 0, 0, 0, 1\n" +
                "float output;Output;\"percent\", 1, 0, 0, 0, 2\n" +
                "uint8_t vehicleSpeedKph;;\"kph\", 1, 0, 0, 0, 0\n" +
                "end_struct\n";

        // Each field is an offset-based descriptor {offset, type, multiplier, label, units, digits}.
        // The category ("Boost") prefixes each named field, matching the ini datalog's "Category: Name".
        // A field with no comment falls back to its bare name, with no category prefix (same as the datalog).
        assertEquals(
                "static constexpr LogField fields[] = {\n" +
                "\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},\n" +
                "\t{0, LogField::Type::F32, 1, \"Boost: Target\", \"kPa\", 1},\n" +
                "\t{4, LogField::Type::F32, 1, \"Boost: Output\", \"percent\", 2},\n" +
                "\t{8, LogField::Type::U08, 1, \"vehicleSpeedKph\", \"kph\", 0},\n",
                parseToSdLog(test, 0, "Boost"));
    }

    @Test
    public void generateSdLogNoCategoryWithBaseOffset() throws IOException {
        String test =
                "struct_no_prefix total\n" +
                "float target;Target;\"kPa\", 1, 0, 0, 0, 1\n" +
                "end_struct\n";

        // With no category, a named field uses its bare comment (no "Category: " prefix).
        // The base offset (where this struct starts in the output space) is added to every field offset.
        assertEquals(
                "static constexpr LogField fields[] = {\n" +
                "\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},\n" +
                "\t{1000, LogField::Type::F32, 1, \"Target\", \"kPa\", 1},\n",
                parseToSdLog(test, 1000, null));
    }

    @Test
    public void generateSdLogArray() throws IOException {
        String test =
                "struct_no_prefix total\n" +
                "uint8_t[2 iterate] knock;;\"v\", 1, 0, 0, 0, 0\n" +
                "uint16_t[2 iterate] withName;MyName;\"kPa\", 1, 0, 0, 0, 1\n" +
                "end_struct\n";

        // Array elements step the offset by the element size and get a " N" (1-based) suffix on the label.
        assertEquals(
                "static constexpr LogField fields[] = {\n" +
                "\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},\n" +
                "\t{0, LogField::Type::U08, 1, \"knock 1\", \"v\", 0},\n" +
                "\t{1, LogField::Type::U08, 1, \"knock 2\", \"v\", 0},\n" +
                "\t{2, LogField::Type::U16, 1, \"Cat: MyName 1\", \"kPa\", 1},\n" +
                "\t{4, LogField::Type::U16, 1, \"Cat: MyName 2\", \"kPa\", 1},\n",
                parseToSdLog(test, 0, "Cat"));
    }

    @Test
    public void generateSdLogEnum() throws IOException {
        String test =
                "#define mode_e_enum \"a\", \"b\", \"c\"\n" +
                "custom mode_e 1 bits, U08, @OFFSET@, [0:1], @@mode_e_enum@@\n" +
                "struct_no_prefix total\n" +
                "mode_e myMode;My mode\n" +
                "end_struct\n";

        // Enums are logged as plain integers (multiplier 1, no units, 0 digits), with the category prefix.
        assertEquals(
                "static constexpr LogField fields[] = {\n" +
                "\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},\n" +
                "\t{0, LogField::Type::U08, 1, \"Cat: My mode\", \"\", 0},\n",
                parseToSdLog(test, 0, "Cat"));
    }

    @Test
    public void generateSdLogNestedStruct() throws IOException {
        String test =
                "struct_no_prefix total\n" +
                "    struct pid_status_s\n" +
                "    \tfloat iTerm;;\"v\", 1, 0, 0, 0, 4\n" +
                "    end_struct\n" +
                "\tpid_status_s alternatorStatus\n" +
                "end_struct\n";

        // Nested structs contribute a dotted prefix to the fallback label; the offset is the field's
        // absolute offset within the output space.
        assertEquals(
                "static constexpr LogField fields[] = {\n" +
                "\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},\n" +
                "\t{0, LogField::Type::F32, 1, \"alternatorStatus.iTerm\", \"v\", 4},\n",
                parseToSdLog(test, 0, "Cat"));
    }

    @Test
    public void generateSdLogStructArray() throws IOException {
        String test =
                "struct_no_prefix total\n" +
                "    struct pid_status_s\n" +
                "    \tfloat iTerm;;\"v\", 1, 0, 0, 0, 4\n" +
                "    end_struct\n" +
                "\tpid_status_s[2 iterate] statuses\n" +
                "end_struct\n";

        // Arrays of structs step the offset by the struct size; the label carries the 1-based index.
        assertEquals(
                "static constexpr LogField fields[] = {\n" +
                "\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},\n" +
                "\t{0, LogField::Type::F32, 1, \"statuses1.iTerm\", \"v\", 4},\n" +
                "\t{4, LogField::Type::F32, 1, \"statuses2.iTerm\", \"v\", 4},\n",
                parseToSdLog(test, 0, null));
    }

    @Test
    public void generateSdLogSkipsUnusedAndBits() throws IOException {
        String test =
                "struct_no_prefix total\n" +
                "float target;Target;\"kPa\", 1, 0, 0, 0, 1\n" +
                "uint8_t unused37;;\"\", 1, 0, 0, 0, 0\n" +
                "bit someFlag;a flag\n" +
                "end_struct\n";

        // "unused" scalars and all bit fields are omitted from the SD log.
        assertEquals(
                "static constexpr LogField fields[] = {\n" +
                "\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},\n" +
                "\t{0, LogField::Type::F32, 1, \"Boost: Target\", \"kPa\", 1},\n",
                parseToSdLog(test, 0, "Boost"));
    }

    @Test
    public void sensorStruct() throws IOException {
        String test = "struct_no_prefix total\n" +
                "    struct pid_status_s\n" +
                "    \tfloat iTerm;;\"v\", 1, 0, -10000, 10000, 4\n" +
                "    \tfloat dTerm;;\"v\", 1, 0, -10000, 10000, 4\n" +
                "    end_struct\n" +
                "\tpid_status_s alternatorStatus\n" +
                "\tpid_status_s idleStatus\n" +
                "end_struct\n";

        assertEquals(
                "entry = alternatorStatus_iTerm,\"alternatorStatus_iTerm\",float,\"%.4f\"\n" +
                "entry = alternatorStatus_dTerm,\"alternatorStatus_dTerm\",float,\"%.4f\"\n" +
                "entry = idleStatus_iTerm,\"idleStatus_iTerm\",float,\"%.4f\"\n" +
                "entry = idleStatus_dTerm,\"idleStatus_dTerm\",float,\"%.4f\"\n",
                parseToDatalogs(test));
    }

    @Test(expected = IllegalStateException.class)
    @Ignore // TODO: re-enable this test
    public void nameDuplicate() throws IOException {
        String test = "struct total\n" +
                "float afr_type;PID dTime;\"ms\",      1,      0,       0, 3000,      0\n" +
                "uint8_t afr_type;123;\"ms\",      1,      0,       0, 3000,      0\n" +
                "end_struct\n";

        // No expectation, should throw
        parseToDatalogs(test);
    }
}
