package com.rusefi.io;

import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.binaryprotocol.BinaryProtocolState;
import org.jetbrains.annotations.NotNull;

/**
 * @author Andrey Belomutskiy
 *         3/3/14
 */
public interface LinkConnector extends LinkDecoder {
    LinkConnector VOID = new LinkConnector() {
        @Override
        public void connectAndReadConfiguration(ConnectionStateListener listener) {
        }

        @Override
        public void send(String command, boolean fireEvent) {
        }

        @Override
        public BinaryProtocol getBinaryProtocol() {
            return null;
        }
    };

    void connectAndReadConfiguration(ConnectionStateListener listener);

    void send(String command, boolean fireEvent) throws InterruptedException;

    BinaryProtocol getBinaryProtocol();

    default void stop() {
    }
}
