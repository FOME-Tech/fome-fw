package com.rusefi;

import com.rusefi.output.ConfigStructure;
import com.rusefi.output.ConfigurationConsumer;

import java.io.IOException;
import java.util.List;
import java.util.Map;

public interface ReaderState {
    VariableRegistry getVariableRegistry();

    Map<String, Integer> getTsCustomSize();

    Map<String, ? extends ConfigStructure> getStructures();

    Map<String, String> getTsCustomLine();

    boolean isStackEmpty();
}
