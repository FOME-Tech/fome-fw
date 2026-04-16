package com.opensr5.io;

/**
 * Callback for incoming data bytes.
 */
public interface DataListener {
    void onDataArrived(byte[] data);
}
