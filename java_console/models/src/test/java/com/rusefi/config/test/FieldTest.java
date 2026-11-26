package com.rusefi.config.test;

import com.opensr5.ConfigurationImage;
import com.rusefi.config.Field;
import com.rusefi.config.FieldType;
import com.rusefi.config.FieldCommandResponse;
import com.rusefi.core.Pair;
import org.junit.Test;

import static org.junit.Assert.*;

public class FieldTest {
    @Test
    public void testParse() {
        {
            assertNull(FieldCommandResponse.parseResponse("notint @768 is 21"));
        }
        {
            Pair<Integer, ?> p = FieldCommandResponse.parseResponse("int @768 is 21");
            assertNotNull(p);
            assertEquals(new Integer(768), p.first);
            assertEquals(21, p.second);
        }
        {
            Pair<Integer, ?> p = FieldCommandResponse.parseResponse("float @808 is 1.00");
            assertNotNull(p);
            assertEquals(new Integer(808), p.first);
            assertEquals(1.0, p.second);
        }
    }

    @Test
    public void testPrecisionDependingOnScale() {
        assertEquals("0.12302", Field.niceToString(0.12302, 4));
        assertEquals("0.1232", Field.niceToString(0.12317, 3));

        assertEquals("1234567.1", Field.niceToString(1234567.123, 4));
        assertEquals("10000.0", Field.niceToString(10000.00002, 4));
        assertEquals("0.002", Field.niceToString(0.002, 4));
        assertEquals("12.302", Field.niceToString(12.302, 4));
        assertEquals("12.302", Field.niceToString(12.302, 3));
        assertEquals("12.31", Field.niceToString(12.312, 2));
        assertEquals("123.02", Field.niceToString(123.02, 4));
    }

    @Test
    public void testSetValueRoundTrips() {
        ConfigurationImage ci = new ConfigurationImage(16);

        Field int8 = Field.create("I8", 0, FieldType.INT8);
        int8.setValue(ci, -5);
        assertEquals(-5, int8.getValue(ci, 1), 0);
        // the generated fields don't carry signedness, so an unsigned byte value is accepted too
        int8.setValue(ci, 200);
        assertEquals(200, Field.create("U8", 0, FieldType.UINT8).getValue(ci, 1), 0);

        Field int16 = Field.create("I16", 2, FieldType.INT16);
        int16.setValue(ci, 12345);
        assertEquals(12345, int16.getValue(ci, 1), 0);

        Field int32 = Field.create("I32", 4, FieldType.INT);
        int32.setValue(ci, -100000);
        assertEquals(-100000, int32.getValue(ci, 1), 0);

        Field f = Field.create("F", 8, FieldType.FLOAT);
        f.setValue(ci, 1.5);
        assertEquals(1.5, f.getValue(ci, 1), 0);

        // neighbours untouched
        assertEquals(200, Field.create("U8", 0, FieldType.UINT8).getValue(ci, 1), 0);
        assertEquals(12345, int16.getValue(ci, 1), 0);
    }

    @Test
    public void testSetBitLeavesOtherBits() {
        ConfigurationImage ci = new ConfigurationImage(8);
        Field word = Field.create("W", 4, FieldType.INT);
        word.setValue(ci, 0b1010);

        Field bit0 = Field.create("B0", 4, FieldType.BIT, 0);
        Field bit3 = Field.create("B3", 4, FieldType.BIT, 3);
        bit0.setValue(ci, 1);
        bit3.setValue(ci, 0);

        assertEquals(0b0011, word.getValue(ci, 1), 0);
        assertEquals(1, bit0.getValue(ci, 1), 0);
        assertEquals(0, bit3.getValue(ci, 1), 0);
    }

    @Test
    public void testEnumValues() {
        Field pin = Field.create("PIN", 0, FieldType.INT16).setEnumValues("test_e", "NONE\nINVALID\nIgn 1\nINVALID\nDigital 1");
        assertTrue(pin.isEnum());
        assertEquals(5, pin.getEnumValues().length);
        assertEquals(2, pin.enumIndex("Ign 1"));
        assertEquals(4, pin.enumIndex("Digital 1"));
        assertEquals(0, pin.enumIndex("NONE"));
        assertEquals("Digital 1", pin.enumName(4));
        assertNull(pin.enumName(5));

        ConfigurationImage ci = new ConfigurationImage(4);
        pin.setValue(ci, pin.enumIndex("Digital 1"));
        assertEquals("Digital 1", pin.enumName(pin.getValue(ci, 1).intValue()));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testEnumUnknownName() {
        Field.create("PIN", 0, FieldType.INT16).setEnumValues("test_e", "NONE\nIgn 1").enumIndex("Ign 2");
    }

    @Test(expected = IllegalArgumentException.class)
    public void testEnumInvalidIsNotAValue() {
        Field.create("PIN", 0, FieldType.INT16).setEnumValues("test_e", "NONE\nINVALID\nIgn 1").enumIndex("INVALID");
    }

    @Test(expected = IllegalArgumentException.class)
    public void testEnumAmbiguousName() {
        Field.create("E", 0, FieldType.INT8).setEnumValues("test_e", "A\nB\nA").enumIndex("A");
    }

    @Test(expected = IllegalArgumentException.class)
    public void testSetValueOutOfRange() {
        Field.create("I8", 0, FieldType.INT8).setValue(new ConfigurationImage(4), 256);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testSetValueFractionOnInteger() {
        Field.create("I16", 0, FieldType.INT16).setValue(new ConfigurationImage(4), 1.5);
    }

}

