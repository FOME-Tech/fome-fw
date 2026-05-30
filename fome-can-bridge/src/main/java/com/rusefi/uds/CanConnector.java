package com.rusefi.uds;

/**
 * Minimal CAN packet abstraction used by SocketCAN.
 */
public interface CanConnector {
    interface CanPacket {
        int id();
        byte[] payload();
    }
}
