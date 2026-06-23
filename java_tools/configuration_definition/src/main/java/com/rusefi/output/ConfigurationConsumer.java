package com.rusefi.output;

import com.rusefi.ReaderState;

import java.io.IOException;

public interface ConfigurationConsumer {
    String UNUSED = "unused";

    default void endFile() throws IOException {

    }
}
