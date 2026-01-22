package com.rusefi.output;

import com.rusefi.ReaderState;

import java.io.IOException;

public interface ConfigurationConsumer {
    String UNUSED = ConfigStructure.UNUSED_ANYTHING_PREFIX;

    default void startFile() {

    }

    default void endFile() throws IOException {

    }

    void handleEndStruct(ReaderState readerState, ConfigStructure structure) throws IOException;
}
