package com.opensr5.io;

import java.io.IOException;

/**
 * Basic write operations for a stream.
 */
public interface WriteStream {
    void write(byte[] bytes) throws IOException;
    void flush() throws IOException;

    default void writeShort(int v) throws IOException {
        byte[] packet = new byte[2];
        packet[0] = (byte) (v >> 8);
        packet[1] = (byte) v;
        write(packet);
    }

    default void writeInt(int v) throws IOException {
        byte[] packet = new byte[4];
        packet[0] = (byte) (v >> 24);
        packet[1] = (byte) (v >> 16);
        packet[2] = (byte) (v >> 8);
        packet[3] = (byte) v;
        write(packet);
    }
}
