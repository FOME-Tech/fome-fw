package com.rusefi.io.can;

import com.devexperts.logging.Logging;
import com.rusefi.io.IoStream;
import org.jetbrains.annotations.NotNull;
import peak.can.basic.PCANBasic;
import peak.can.basic.TPCANBaudrate;
import peak.can.basic.TPCANHandle;
import peak.can.basic.TPCANMessageType;
import peak.can.basic.TPCANMsg;
import peak.can.basic.TPCANStatus;
import peak.can.basic.TPCANType;

import static com.devexperts.logging.Logging.getLogging;
import static peak.can.basic.TPCANMessageType.PCAN_MESSAGE_STANDARD;

/**
 * PEAK PCAN-USB adapter helpers.
 *
 * Requires the PCAN Basic API for Java to be installed:
 *   Windows: Install the PEAK PCAN driver, then add PCANBasic.dll and PCANBasic_JNI.dll
 *            to the same directory as the console JAR (or on PATH).
 *   Java library: peak-can-basic.jar must be on the classpath.
 *
 * At runtime the stubs in the peak-can-basic subproject are replaced by the real
 * PCAN library when the DLLs and real JAR are present.
 */
public class PCanHelper {
    private static final Logging log = getLogging(PCanHelper.class);

    /** PCAN USB channel — first PCAN-USB device plugged in. */
    public static final TPCANHandle CHANNEL = TPCANHandle.PCAN_USBBUS1;

    @NotNull
    public static PCANBasic create() {
        PCANBasic can = new PCANBasic();
        can.initializeAPI();
        return can;
    }

    public static TPCANStatus init(PCANBasic can) {
        return can.Initialize(CHANNEL, TPCANBaudrate.PCAN_BAUD_500K, TPCANType.PCAN_TYPE_NONE, 0, (short) 0);
    }

    public static TPCANStatus send(PCANBasic can, int id, byte[] payLoad) {
        if (log.debugEnabled())
            log.debug("PCAN TX id=" + String.format("%x", id) + " " + IoStream.printHexBinary(payLoad));
        TPCANMsg msg = new TPCANMsg(id, PCAN_MESSAGE_STANDARD.getValue(), (byte) payLoad.length, payLoad);
        return can.Write(CHANNEL, msg);
    }

    public static void debugPacket(TPCANMsg rx) {
        if (log.debugEnabled())
            log.debug("PCAN RX id=" + String.format("%X", rx.getID()) + " len=" + rx.getLength()
                    + ": " + IoStream.printHexBinary(rx.getData()));
    }

    /**
     * Returns true if the PCAN library is available on this platform.
     * On Linux/macOS this will always be false (PEAK USB on Windows only).
     */
    public static boolean isAvailable() {
        try {
            Class.forName("peak.can.basic.PCANBasic");
            // Also check that the native library is loadable
            PCANBasic probe = new PCANBasic();
            probe.initializeAPI();
            return true;
        } catch (UnsatisfiedLinkError | Exception e) {
            return false;
        }
    }
}
