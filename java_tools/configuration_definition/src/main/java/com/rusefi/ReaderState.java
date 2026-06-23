package com.rusefi;

import java.util.Map;

public interface ReaderState {
    VariableRegistry getVariableRegistry();

    Map<String, Integer> getTsCustomSize();

    Map<String, String> getTsCustomLine();

    boolean isStackEmpty();
}
