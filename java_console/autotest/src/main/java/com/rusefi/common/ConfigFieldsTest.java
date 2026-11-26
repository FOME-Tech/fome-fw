package com.rusefi.common;

import com.rusefi.RusefiTestBase;
import com.rusefi.config.Field;
import com.rusefi.config.generated.Fields;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

/**
 * The generated Java fields (offsets, bit positions, enum lists) agree with the firmware's own layout. Writes in RAM
 * only, through the TunerStudio path, and puts everything back.
 */
public class ConfigFieldsTest extends RusefiTestBase {
    /**
     * Flip bits by their Java offset and bit index, and read them back through the firmware's lookup, which is
     * generated from the C struct. Harmless bits, one early in the config and one late inside a struct.
     */
    @Test
    public void testBitsMatchFirmwareLayout() {
        assertBitMatches(Fields.USETLE8888_CRANKING_HACK, "useTLE8888_cranking_hack");
        assertBitMatches(Fields.TRACTIONCONTROL_DRIVENAXLEISFRONT, "tractionControl.drivenAxleIsFront");
        assertBitMatches(Fields.VERBOSETRIGGERSYNCHDETAILS, "verboseTriggerSynchDetails");
    }

    private void assertBitMatches(Field field, String cName) {
        double original = ecu.getField(field);
        try {
            for (double value : new double[]{1 - original, original}) {
                ecu.setField(field, value);
                assertEquals(cName, value, ecu.getFirmwareValue(cName), 0);
            }
        } finally {
            ecu.setField(field, original);
        }
    }

    /**
     * Write an enum by its TunerStudio name and read the tune back from the ECU
     */
    @Test
    public void testEnumByName() {
        Field field = Fields.TIMINGMODE;
        String original = ecu.getFieldName(field);
        String other = original.equals("fixed") ? "dynamic" : "fixed";
        try {
            ecu.setField(field, other);
            ecu.readConfigurationFromEcu();
            assertEquals(other, ecu.getFieldName(field));
        } finally {
            ecu.setField(field, original);
        }
    }
}
