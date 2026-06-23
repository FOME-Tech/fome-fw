package com.rusefi;

import com.devexperts.logging.Logging;
import com.opensr5.ini.RawIniFile;
import com.opensr5.ini.field.EnumIniField;
import com.rusefi.enum_reader.Value;
import com.rusefi.output.*;
import com.rusefi.util.IoUtils;
import com.rusefi.util.SystemOut;
import org.jetbrains.annotations.NotNull;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.ConfigFieldImpl.BOOLEAN_T;
import static com.rusefi.VariableRegistry.unquote;
import static com.rusefi.output.JavaSensorsConsumer.quote;

/**
 * We keep state here as we read configuration definition
 * <p>
 * Andrey Belomutskiy, (c) 2013-2020
 * 12/19/18
 */
public class ReaderStateImpl implements ReaderState {
    // used to update other files
    private final List<String> inputFiles = new ArrayList<>();
    private final Stack<ConfigStructureImpl> stack = new Stack<>();
    private final Map<String, Integer> tsCustomSize = new HashMap<>();
    private final Map<String, String> tsCustomLine = new HashMap<>();
    private final Map<String, ConfigStructureImpl> structures = new HashMap<>();
    // well, technically those should be a builder for state, not this state class itself

    private final EnumsReader enumsReader = new EnumsReader();
    private final VariableRegistry variableRegistry = new VariableRegistry();

    public EnumsReader getEnumsReader() {
        return enumsReader;
    }

    public List<String> getInputFiles() {
        return inputFiles;
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

    public void addInputFile(String fileName) {
        inputFiles.add(fileName);
    }

    @Override
    public VariableRegistry getVariableRegistry() {
        return variableRegistry;
    }

    @Override
    public Map<String, Integer> getTsCustomSize() {
        return tsCustomSize;
    }

    @Override
    public Map<String, ? extends ConfigStructure> getStructures() {
        return structures;
    }

    @Override
    public Map<String, String> getTsCustomLine() {
        return tsCustomLine;
    }

    @Override
    public boolean isStackEmpty() {
        return stack.isEmpty();
    }
}
