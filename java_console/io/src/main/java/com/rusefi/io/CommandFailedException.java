package com.rusefi.io;

/**
 * The ECU received a console command and reported that it failed: unknown command or setting name,
 * wrong argument count, or an argument the handler rejected (e.g. an invalid pin name).
 */
public class CommandFailedException extends RuntimeException {
    public CommandFailedException(String command) {
        super("ECU rejected command [" + command + "]");
    }
}
