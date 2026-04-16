package peak.can.basic;

/**
 * Stub for the PEAK PCAN Basic Java API.
 *
 * This class compiles everywhere so that PCanHelper/PCanIoStream can be compiled
 * on Linux/macOS without the real PCAN library. At runtime, PCanHelper.isAvailable()
 * checks for the real native library; if it's absent these stubs are never used.
 *
 * On Windows with a real PCAN-USB adapter:
 *  1. Download PCAN Basic API from peak-system.com
 *  2. Replace this stub JAR with the real peak-can-basic.jar
 *  3. Place PCANBasic.dll + PCANBasic_JNI.dll next to the console JAR or on PATH
 */
public class PCANBasic {

    /**
     * Load the native PCAN library. On Windows with the real library this calls
     * System.loadLibrary("PCANBasic_JNI"). This stub does nothing.
     */
    public void initializeAPI() {
        // Stub — real implementation loads the JNI DLL
    }

    public TPCANStatus Initialize(TPCANHandle channel, TPCANBaudrate baudrate,
                                   TPCANType hwType, int ioPort, short interrupt) {
        throw new UnsupportedOperationException(
                "PCAN stub: real PCAN library not installed. " +
                "See java_console/peak-can-basic/build.gradle for instructions.");
    }

    public TPCANStatus Write(TPCANHandle channel, TPCANMsg msg) {
        throw new UnsupportedOperationException("PCAN stub");
    }

    public TPCANStatus Read(TPCANHandle channel, TPCANMsg msg, Object timestamp) {
        throw new UnsupportedOperationException("PCAN stub");
    }

    public TPCANStatus Uninitialize(TPCANHandle channel) {
        throw new UnsupportedOperationException("PCAN stub");
    }

    public TPCANStatus FilterMessages(TPCANHandle channel, int fromId, int toId,
                                       Object mode) {
        throw new UnsupportedOperationException("PCAN stub");
    }
}
