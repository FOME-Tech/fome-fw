package com.rusefi.io;

/**
 * 3/8/14
 * Andrey Belomutskiy, (c) 2013-2020
 */
public interface InvocationConfirmationListener {
    InvocationConfirmationListener VOID = new InvocationConfirmationListener() {
        @Override
        public void onCommandConfirmation() {
        }
    };

    void onCommandConfirmation();

    /**
     * The ECU received the command and reported that it failed, see {@link CommandFailedException}
     */
    default void onCommandFailure() {
    }
}
