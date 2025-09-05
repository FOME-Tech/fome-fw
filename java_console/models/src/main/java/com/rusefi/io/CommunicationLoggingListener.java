package com.rusefi.io;

/**
 * Andrey Belomutskiy, (c) 2013-2020
 * 9/4/14
 */
public interface CommunicationLoggingListener {
    void onPortHolderMessage(final Class clazz, final String message);
}
