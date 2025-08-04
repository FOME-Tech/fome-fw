package com.rusefi.io.tcp;

import com.devexperts.logging.Logging;
import com.rusefi.NamedThreadFactory;
import org.jetbrains.annotations.NotNull;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ThreadFactory;

import static com.devexperts.logging.Logging.getLogging;

/**
 * This class makes rusEfi console a proxy for other tuning software, this way we can have two tools connected via same
 * serial port simultaneously
 *
 * See BinaryProtocolServerSandbox
 * @author Andrey Belomutskiy
 * 11/24/15
 */

public class BinaryProtocolServer {
    private static final Logging log = getLogging(BinaryProtocolServer.class);
    public static final String TS_OK = "\0";

    static {
        log.configureDebugEnabled(false);
    }

    private final static ConcurrentHashMap<String, ThreadFactory> THREAD_FACTORIES_BY_NAME = new ConcurrentHashMap<>();

    @NotNull
    public static ThreadFactory getThreadFactory(String threadName) {
        synchronized (THREAD_FACTORIES_BY_NAME) {
            ThreadFactory threadFactory = THREAD_FACTORIES_BY_NAME.get(threadName);
            if (threadFactory == null) {
                threadFactory = new NamedThreadFactory(threadName);
                THREAD_FACTORIES_BY_NAME.put(threadName, threadFactory);
            }
            return threadFactory;
        }
    }
}