package com.rusefi.io;

/**
 * @author Andrey Belomutskiy
 *         3/1/2017
 * @see ConnectionStatusValue
 */
public interface ConnectionStateListener extends ConnectionFailedListener {
    /**
     * This method is invoked once we have connection & configuration from controller
     */
    void onConnectionEstablished();
}
