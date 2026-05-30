package com.rusefi.binaryprotocol;

import com.rusefi.config.generated.Integration;

/**
 * Slimmed-down BinaryProtocol — only provides findCommand() for proxy logging.
 */
public class BinaryProtocol {

    public static String findCommand(byte command) {
        switch (command) {
            case Integration.TS_COMMAND_F:
                return "PROTOCOL";
            case Integration.TS_CRC_CHECK_COMMAND:
                return "CRC_CHECK";
            case Integration.TS_BURN_COMMAND:
                return "BURN";
            case Integration.TS_HELLO_COMMAND:
                return "HELLO";
            case Integration.TS_READ_COMMAND:
                return "READ";
            case Integration.TS_GET_TEXT:
                return "TS_GET_TEXT";
            case Integration.TS_GET_FIRMWARE_VERSION:
                return "GET_FW_VERSION";
            case Integration.TS_CHUNK_WRITE_COMMAND:
                return "WRITE_CHUNK";
            case Integration.TS_OUTPUT_COMMAND:
                return "TS_OUTPUT_COMMAND";
            case Integration.TS_RESPONSE_OK:
                return "TS_RESPONSE_OK";
            default:
                return "command " + (char) command + "/" + command;
        }
    }
}
