package com.rusefi.io.tcp;

import com.devexperts.logging.Logging;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.Collection;
import java.util.Collections;

import static com.devexperts.logging.Logging.getLogging;

/**
 * @author Andrey Belomutskiy
 *         3/3/14
 */
public class TcpConnector {
    private static final Logging log = getLogging(TcpConnector.class);

    public final static int DEFAULT_PORT = 29001;
    public static final String LOCALHOST = "localhost";

    public static boolean isTcpPort(String port) {
        try {
            getTcpPort(port);
            return true;
        } catch (InvalidTcpPort e) {
            return false;
        }
    }

    /*
        public static String doUnpackConfirmation(String message) {
            String confirmation = message.substring(CommandQueue.CONFIRMATION_PREFIX.length());
            int index = confirmation.indexOf(":");
            if (index < 0) {
                return null;
            }
            String number = confirmation.substring(index + 1);
            int length;
            try {
                length = Integer.parseInt(number);
            } catch (NumberFormatException e) {
                return null;
            }
            if (length != index) {
                return null;
            }
            return confirmation.substring(0, length);
        }
        */
    public static class InvalidTcpPort extends IOException {
    }

    public static int getTcpPort(String port) throws InvalidTcpPort {
        try {
            String[] portParts = port.split(":");
            int index = (portParts.length == 1 ? 0 : 1);
            return Integer.parseInt(portParts[index]);
        } catch (NumberFormatException e) {
            throw new InvalidTcpPort();
        }
    }

    public static String getHostname(String port) {
        String[] portParts = port.split(":");
        return (portParts.length == 1 ? LOCALHOST : portParts[0].length() > 0 ? portParts[0] : LOCALHOST);
    }

    public static Collection<String> getAvailablePorts() {
        return isTcpPortOpened() ? Collections.singletonList("" + DEFAULT_PORT) : Collections.emptyList();
    }

    public static boolean isTcpPortOpened() {
        long now = System.currentTimeMillis();
        try {
            Socket s = new Socket();
            s.connect(new InetSocketAddress(LOCALHOST, DEFAULT_PORT), 500);
            s.close();
            return true;
        } catch (IOException e) {
            log.info("Connection refused in getAvailablePorts(): simulator not running in " + (System.currentTimeMillis() - now) + "ms");
            return false;
        }
    }
}
