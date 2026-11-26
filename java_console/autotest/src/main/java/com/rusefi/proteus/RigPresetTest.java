package com.rusefi.proteus;

import com.rusefi.Rig;
import com.rusefi.RusefiTestBase;
import com.rusefi.config.Field;
import com.rusefi.config.generated.Fields;
import com.rusefi.enums.engine_type_e;
import org.junit.Test;

import java.lang.reflect.Modifier;
import java.util.*;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

/**
 * The HW CI rig preset (Proteus and Atlas) puts the rig's connections where the jumpers are, and leaves every other
 * rig pin free for tests to assign
 */
public class RigPresetTest extends RusefiTestBase {
    /**
     * Pin fields of all these types hold a Gpio value, whatever names their lists give it
     */
    private static final Set<String> PIN_TYPES = new HashSet<>(Arrays.asList(
            "output_pin_e", "brain_input_pin_e", "switch_input_pin_e", "Gpio"));
    private static final String ANALOG_TYPE = "adc_channel_e";

    @Test
    public void testRigPreset() {
        ecu.setEngineType(engine_type_e.HW_CI_RIG);
        ecu.readConfigurationFromEcu();

        // Raw values, since triggerSimulatorPins' list names MCU pins ("PD5") rather than connector pins
        Map<Field, Integer> expected = new LinkedHashMap<>();
        expected.put(Fields.IDLE_SOLENOIDPIN, outputPin(Rig.ANALOG_LOOP_OUT));
        expected.put(Fields.TPS1_1ADCCHANNEL, analogInput(Rig.ANALOG_LOOP_IN));
        expected.put(Fields.TRIGGERSIMULATORPINS1, outputPin(Rig.CRANK_OUT));
        expected.put(Fields.TRIGGERSIMULATORPINS2, outputPin(Rig.CAM_OUT));
        expected.put(Fields.TRIGGERINPUTPINS1, inputPin(Rig.CRANK_IN));
        expected.forEach((field, value) -> assertEquals(field.getName(), value.intValue(), (int) ecu.getField(field)));

        // Nothing else may sit on a rig pin, or a test assigning it would fight the preset
        Set<Integer> rigPins = new HashSet<>(Arrays.asList(
                outputPin(Rig.ANALOG_LOOP_OUT), outputPin(Rig.CRANK_OUT), outputPin(Rig.CAM_OUT),
                outputPin(Rig.FREQUENCY_A_OUT), outputPin(Rig.FREQUENCY_B_OUT),
                inputPin(Rig.CRANK_IN), inputPin(Rig.CAM_IN), inputPin(Rig.FREQUENCY_A_IN), inputPin(Rig.FREQUENCY_B_IN)));
        int analogLoop = analogInput(Rig.ANALOG_LOOP_IN);

        List<String> clashes = new ArrayList<>();
        for (Field field : allEnumFields()) {
            if (expected.containsKey(field))
                continue;
            int value = (int) ecu.getField(field);
            boolean clash = PIN_TYPES.contains(field.getEnumType()) ? rigPins.contains(value)
                    : ANALOG_TYPE.equals(field.getEnumType()) && value == analogLoop;
            if (clash)
                clashes.add(field.getName() + " = " + field.enumName(value));
        }
        assertTrue("Fields on rig pins: " + clashes, clashes.isEmpty());
    }

    private static int outputPin(String name) {
        return Fields.IDLE_SOLENOIDPIN.enumIndex(name);
    }

    private static int inputPin(String name) {
        return Fields.TRIGGERINPUTPINS1.enumIndex(name);
    }

    private static int analogInput(String name) {
        return Fields.TPS1_1ADCCHANNEL.enumIndex(name);
    }

    private static List<Field> allEnumFields() {
        List<Field> fields = new ArrayList<>();
        for (java.lang.reflect.Field f : Fields.class.getFields()) {
            if (!Modifier.isStatic(f.getModifiers()) || f.getType() != Field.class)
                continue;
            try {
                Field field = (Field) f.get(null);
                if (field.isEnum())
                    fields.add(field);
            } catch (IllegalAccessException e) {
                throw new IllegalStateException(e);
            }
        }
        return fields;
    }
}
