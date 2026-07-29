package com.rusefi.pinout;

import com.rusefi.EnumsReader;
import com.rusefi.pinout.PinoutLogic;
import com.rusefi.enum_reader.Value;
import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;

import static org.junit.Assert.assertEquals;

public class PinoutLogicTest {
    @Test
    public void testEnumToTs() {
        Map<String, Value> currentValues = new TreeMap<>();
        // "NO" is the enum member meaning "nothing assigned", and lands at index 0
        currentValues.put("NO", new Value(null, "0"));
        currentValues.put("KEY", new Value(null, "3"));
        currentValues.put("KEY4", new Value(null, "4"));
        EnumsReader.EnumState enumState = new EnumsReader.EnumState(currentValues, "pins", true);

        {
            // The nothing pin is rendered as NONE whatever name the board gave it
            ArrayList<String> list = new ArrayList<>(Arrays.asList("nothing", "1", "10"));
            String result = PinoutLogic.enumToOptionsList("NO", enumState, list);
            assertEquals("\"NONE\",\"1\",\"10\"", result);
        }

        {
            // Gaps - values this board doesn't route anywhere - become INVALID, keeping every
            // remaining name at the index matching its enum value
            ArrayList<String> list = new ArrayList<>(Arrays.asList("nothing", "1", null, null, null, null, null, "10"));
            String result = PinoutLogic.enumToOptionsList("NO", enumState, list);
            assertEquals("\"NONE\",\"1\",\"INVALID\",\"INVALID\",\"INVALID\",\"INVALID\",\"INVALID\",\"10\"", result);
        }
    }
}
