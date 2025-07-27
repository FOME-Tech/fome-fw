package com.rusefi.io.tcp;

import com.devexperts.logging.Logging;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Collection;

import static com.devexperts.logging.Logging.getLogging;

/**
 * @author Andrey Belomutskiy
 *         3/3/14
 */
public class TcpConnector {
    private static final Logging log = getLogging(TcpConnector.class);

    public final static int DEFAULT_SIMULATOR_PORT = 29001;
    public final static int DEFAULT_WIFI_PORT = 29000;
    public static final String LOCALHOST = "localhost";
    public static final String DEFAULT_WIFI_HOST = "192.168.10.1";

    public static boolean isTcpPort(String port) {
        try {
            getTcpPort(port);
            return true;
        } catch (InvalidTcpPort e) {
            return false;
        }
    }

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

    private static String makePortString(String hostname, int port) {
        return hostname + ":" + port;
    }

    public static Collection<String> getAvailablePorts() {
        Collection<String> ports = new ArrayList<>();
        final String[] candidates = new String[] {
                makePortString(DEFAULT_WIFI_HOST, DEFAULT_WIFI_PORT),
                makePortString(LOCALHOST, DEFAULT_SIMULATOR_PORT)
        };

        for (String candidate : candidates) {
            try {
                if (checkHost(candidate)) {
                    ports.add(candidate);
                }
            } catch (InvalidTcpPort e) {
            }
        }

        return ports;
    }

    private static Boolean checkHost(String hostAndPort) throws InvalidTcpPort {
        String host = getHostname(hostAndPort);
        int port = getTcpPort(hostAndPort);

        long now = System.currentTimeMillis();
        try {
            Socket s = new Socket();
            s.connect(new InetSocketAddress(host, port), 300);
            s.close();
            return true;
        } catch (IOException e) {
            log.info("checkHost(" + hostAndPort + ") failed in " + (System.currentTimeMillis() - now) + "ms");
            return false;
        }
    }

    public static boolean isSimulatorActive() {
        try {
            return checkHost(makePortString(LOCALHOST, DEFAULT_SIMULATOR_PORT));
        } catch (InvalidTcpPort t) {
            return false;
        }
    }
}
