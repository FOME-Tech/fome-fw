package com.rusefi.tools;

import com.rusefi.*;
import com.rusefi.autodetect.PortDetector;
import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.config.generated.Fields;
import com.rusefi.io.ConnectionStateListener;
import com.rusefi.io.IoStream;
import com.rusefi.io.LinkManager;
import com.rusefi.maintenance.ExecHelper;
import org.jetbrains.annotations.Nullable;

import java.io.IOException;
import java.util.Map;
import java.util.TreeMap;
import java.util.function.Function;

public class ConsoleTools {
    public static final String RUS_EFI_NOT_DETECTED = "rusEFI not detected";
    private static final Map<String, ConsoleTool> TOOLS = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);

    private static final Map<String, String> toolsHelp = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);

    static {
        registerTool("help", args -> printTools(), "Print this help.");

        registerTool("ptrace_enums", ConsoleTools::runPerfTraceTool, "NOT A USER TOOL. Development tool to process performance trace enums");

        registerTool("get_performance_trace", args -> PerformanceTraceHelper.getPerformanceTune(), "DEV TOOL: Get performance trace from ECU");

        registerTool("version", ConsoleTools::version, "Only print version");

        registerTool("send_command", args -> {
            String command = args[1];
            System.out.println("Sending command " + command);
            sendCommand(command);
        }, "Sends command specified as second argument");
        registerTool("reboot_ecu", args -> sendCommand(Fields.CMD_REBOOT), "Sends a command to reboot rusEFI controller.");
        registerTool(Fields.CMD_REBOOT_DFU, args -> {
            sendCommand(Fields.CMD_REBOOT_DFU);
            /**
             * AndreiKA reports that auto-detect fails to interrupt communication threads while in native code
             * See https://github.com/rusefi/rusefi/issues/3300
             */
            System.exit(0);
        }, "Sends a command to switch rusEFI controller into DFU mode.");
    }

    private static void version(String[] strings) {
        // version is printed by already, all we need is to do nothing
    }

    private static void registerTool(String command, ConsoleTool callback, String help) {
        TOOLS.put(command, callback);
        toolsHelp.put(command, help);
    }

    public static void printTools() {
        for (String key : TOOLS.keySet()) {
            System.out.println("Tool available: " + key);
            String help = toolsHelp.get(key);
            if (help != null) {
                System.out.println("\t" + help);
                System.out.println("\n");
            }
        }
    }

    private static void sendCommand(String command) throws IOException {
        String autoDetectedPort = autoDetectPort();
        if (autoDetectedPort == null)
            return;
        try (IoStream stream = LinkManager.open(autoDetectedPort)) {
            byte[] commandBytes = BinaryProtocol.getTextCommandBytes(command);
            stream.sendPacket(commandBytes);
        }
    }


    private static void runPerfTraceTool(String[] args) throws IOException {
        PerfTraceTool.readPerfTrace(args[1], args[2], args[3], args[4]);
    }

    public static void startAndConnect(final Function<LinkManager, Void> onConnectionEstablished) {

        String autoDetectedPort = PortDetector.autoDetectSerial().getSerialPort();
        if (autoDetectedPort == null) {
            System.err.println(RUS_EFI_NOT_DETECTED);
            return;
        }
        LinkManager linkManager = new LinkManager();
        linkManager.startAndConnect(autoDetectedPort, new ConnectionStateListener() {
            @Override
            public void onConnectionEstablished() {
                onConnectionEstablished.apply(linkManager);
            }

            @Override
            public void onConnectionFailed(String s) {

            }
        });
    }

    private static void invokeCallback(String callback) {
        if (callback == null)
            return;
        System.out.println("Invoking " + callback);
        ExecHelper.submitAction(new Runnable() {
            @Override
            public void run() {
                try {
                    Runtime.getRuntime().exec(callback);
                } catch (IOException e) {
                    throw new IllegalStateException(e);
                }
            }
        }, "callback");
    }

    public static boolean runTool(String[] args) throws Exception {
        if (args == null || args.length == 0)
            return false;
        String toolName = args[0];
        ConsoleTool consoleTool = TOOLS.get(toolName);
        if (consoleTool != null) {
            consoleTool.runTool(args);
            return true;
        }
        return false;
    }

    @Nullable
    private static String autoDetectPort() {
        String autoDetectedPort = PortDetector.autoDetectSerial().getSerialPort();
        if (autoDetectedPort == null) {
            System.err.println(RUS_EFI_NOT_DETECTED);
            return null;
        }
        return autoDetectedPort;
    }

    interface ConsoleTool {
        void runTool(String[] args) throws Exception;
    }
}
