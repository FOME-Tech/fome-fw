package com.fome.canbridge.utils;

import peak.can.basic.*;
import java.net.NetworkInterface;
import java.util.*;

public class DeviceScanner {

    public static List<String> scanDevices(String type) {
        if ("PCAN".equalsIgnoreCase(type)) {
            return scanPcan();
        } else if ("SLCAN".equalsIgnoreCase(type)) {
            return scanSerialPorts();
        } else {
            return scanSocketCan();
        }
    }

    private static List<String> scanSerialPorts() {
        List<String> devices = new ArrayList<>();
        try {
            com.fazecast.jSerialComm.SerialPort[] ports = com.fazecast.jSerialComm.SerialPort.getCommPorts();
            for (com.fazecast.jSerialComm.SerialPort p : ports) {
                devices.add(p.getSystemPortName());
            }
        } catch (LinkageError e) {
            // Native library failed to load (arch mismatch or blocked)
        } catch (Exception e) {
            // Ignore
        }
        if (devices.isEmpty()) devices.add("/dev/ttyACM0");
        return devices;
    }

    private static List<String> scanSocketCan() {
        List<String> devices = new ArrayList<>();
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            while (interfaces.hasMoreElements()) {
                String name = interfaces.nextElement().getName();
                if (name.startsWith("can") || name.startsWith("vcan")) {
                    devices.add(name);
                }
            }
        } catch (Exception e) {
            // Ignore for now
        }
        if (devices.isEmpty()) {
            devices.add("can0"); // Fallback
        }
        return devices;
    }

    private static List<String> scanPcan() {
        List<String> devices = new ArrayList<>();
        try {
            PCANBasic can = new PCANBasic();
            if (can.initializeAPI()) {
                // Get count of attached channels
                int[] count = new int[1];
                TPCANStatus status = can.GetValue(TPCANHandle.PCAN_NONEBUS, TPCANParameter.PCAN_ATTACHED_CHANNELS_COUNT, count, 4);
                
                if (status == TPCANStatus.PCAN_ERROR_OK && count[0] > 0) {
                    TPCANChannelInformation[] info = new TPCANChannelInformation[count[0]];
                    // Need to initialize array elements for JNI
                    for (int i = 0; i < count[0]; i++) info[i] = new TPCANChannelInformation();
                    
                    status = can.GetValue(TPCANHandle.PCAN_NONEBUS, TPCANParameter.PCAN_ATTACHED_CHANNELS, info, count[0] * 128); // Approximation of size
                    
                    if (status == TPCANStatus.PCAN_ERROR_OK) {
                        for (TPCANChannelInformation ci : info) {
                            if (ci.getChannelHandle() != null) {
                                devices.add(ci.getChannelHandle().toString());
                            }
                        }
                    }
                }
            }
        } catch (Throwable t) {
            // Probably moving without JNI on Linux
        }
        
        if (devices.isEmpty()) {
            devices.add("PCAN_USBBUS1"); // Fallback
        }
        return devices;
    }
}
