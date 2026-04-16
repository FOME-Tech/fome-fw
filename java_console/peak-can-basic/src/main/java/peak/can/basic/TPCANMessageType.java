package peak.can.basic;

/** Stub for PEAK PCAN message type constants. Replace with the real library on Windows. */
public enum TPCANMessageType {
    PCAN_MESSAGE_STANDARD(0x00),
    PCAN_MESSAGE_RTR(0x01),
    PCAN_MESSAGE_EXTENDED(0x02),
    PCAN_MESSAGE_FD(0x04),
    PCAN_MESSAGE_BRS(0x08),
    PCAN_MESSAGE_ESI(0x10),
    PCAN_MESSAGE_ERRFRAME(0x40),
    PCAN_MESSAGE_STATUS(0x80);

    private final byte value;

    TPCANMessageType(int value) {
        this.value = (byte) value;
    }

    public byte getValue() {
        return value;
    }
}
