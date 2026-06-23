package com.rusefi;

import com.rusefi.output.ConfigStructure;

import java.util.Map;

public interface ReaderState {
    VariableRegistry getVariableRegistry();

    Map<String, Integer> getTsCustomSize();

    Map<String, ? extends ConfigStructure> getStructures();

    Map<String, String> getTsCustomLine();

    boolean isStackEmpty();
}
