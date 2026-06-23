package com.rusefi.test;

import com.rusefi.EnumsReader;
import com.rusefi.ReaderStateImpl;
import com.rusefi.VariableRegistry;
import org.junit.Test;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.StringReader;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

public class EnumAsTsVariable {
    @Test
    public void testUseEnumAsVariable() throws IOException {
        ReaderStateImpl readerState = new ReaderStateImpl();
        VariableRegistry variableRegistry = new VariableRegistry();
        readerState.read(variableRegistry, new BufferedReader(new StringReader("" +
                "typedef enum {\n" +
                "\tFO_1 = 0,\n" +
                "\n" +
                "\t// 2 cylinder\n" +
                "\tFO_1_2 = 8,\n" +
                "Force_2b_firing_order = ENUM_16_BITS,\n" +
                "Force_4b_firing_order = ENUM_32_BITS,\n" +
                "} firing_order_e;")));

        EnumsReader.EnumState state = readerState.getEnumsReader().getEnums().get("firing_order_e");
        assertNotNull(state);

        String data = variableRegistry.get("firing_order_e_FO_1");
        assertEquals("0", data);

        assertEquals("0", variableRegistry.applyVariables("@@firing_order_e_FO_1@@"));
        assertEquals("\\x00\\x00", variableRegistry.applyVariables("@@firing_order_e_FO_1_16_hex@@"));
    }
}
