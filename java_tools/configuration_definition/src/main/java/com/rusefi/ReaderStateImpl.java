package com.rusefi;

import com.rusefi.enum_reader.Value;

import java.io.IOException;
import java.io.Reader;
import java.util.*;

/**
 * We keep state here as we read configuration definition
 * <p>
 * Andrey Belomutskiy, (c) 2013-2020
 * 12/19/18
 */
public class ReaderStateImpl {
    // well, technically those should be a builder for state, not this state class itself

    private final EnumsReader enumsReader = new EnumsReader();
    private final VariableRegistry variableRegistry = new VariableRegistry();

    public EnumsReader getEnumsReader() {
        return enumsReader;
    }

    public void read(Reader reader) throws IOException {
        Map<String, EnumsReader.EnumState> newEnums = EnumsReader.readStatic(reader);

        for (Map.Entry<String, EnumsReader.EnumState> enumFamily : newEnums.entrySet()) {

            for (Map.Entry<String, Value> enumValue : enumFamily.getValue().entrySet()) {

                String key = enumFamily.getKey() + "_" + enumValue.getKey();
                String value = enumValue.getValue().getValue();
                variableRegistry.register(key, value);

                try {
                    int numericValue = enumValue.getValue().getIntValue();
                    variableRegistry.registerHex(key, numericValue);
                } catch (NumberFormatException ignore) {
                    // ENUM_32_BITS would be an example of a non-numeric enum, let's just skip for now
                }
            }
        }

        enumsReader.enums.putAll(newEnums);
    }

    public VariableRegistry getVariableRegistry() {
        return variableRegistry;
    }
}
