package peak.can.basic;

import java.util.Arrays;

/**
 * Stub for the PEAK PCAN message structure.
 * Replace with the real library on Windows.
 */
public class TPCANMsg {
    private int id;
    private byte type;
    private byte length;
    private byte[] data;

    /** Create an empty message with the given maximum data buffer size. */
    public TPCANMsg(int maxLength) {
        this.data = new byte[maxLength];
        this.length = 0;
    }

    /** Create a message ready to send. */
    public TPCANMsg(int id, byte type, byte length, byte[] data) {
        this.id = id;
        this.type = type;
        this.length = length;
        this.data = Arrays.copyOf(data, data.length);
    }

    public int getID() {
        return id;
    }

    public byte getLength() {
        return length;
    }

    public byte[] getData() {
        return Arrays.copyOf(data, length & 0xFF);
    }

    @Override
    public String toString() {
        return "TPCANMsg{id=" + String.format("%X", id) + ", len=" + length + "}";
    }
}
