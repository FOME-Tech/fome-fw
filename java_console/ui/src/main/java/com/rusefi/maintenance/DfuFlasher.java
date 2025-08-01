package com.rusefi.maintenance;

import com.rusefi.Launcher;
import com.rusefi.Timeouts;
import com.rusefi.config.generated.Fields;
import com.rusefi.core.io.BundleUtil;
import com.rusefi.io.DfuHelper;
import com.rusefi.io.IoStream;
import com.rusefi.io.LinkManager;
import com.rusefi.io.UpdateOperationCallbacks;
import org.jetbrains.annotations.NotNull;

import javax.swing.*;
import java.awt.*;
import java.io.*;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class DfuFlasher {
    private static final String DFU_BINARY_LOCATION = Launcher.TOOLS_PATH + File.separator + "STM32_Programmer_CLI/bin";
    private static final String DFU_BINARY = "STM32_Programmer_CLI.exe";
    private static final String WMIC_DFU_QUERY_COMMAND = "wmic path win32_pnpentity where \"Caption like '%STM32%' and Caption like '%Bootloader%'\" get Caption,ConfigManagerErrorCode /format:list";

    public static void doAutoDfu(JComponent parent, @NotNull String port, UpdateOperationCallbacks callbacks) {
        boolean needsEraseFirst = false;
        if (BundleUtil.getBundleTarget().contains("f7")) {
            int result = JOptionPane.showConfirmDialog(parent, "Firmware update requires a full erase of the ECU. If your tune is not saved in TunerStudio, it will be lost.\nEnsure that TunerStudio has your current tune saved!\n\nAfter updating, re-connect TunerStudio to restore your tune.\n\nPress OK to continue with the update, or Cancel to abort so you can save your tune.", "WARNING", JOptionPane.OK_CANCEL_OPTION);

            // 0 means they clicked "OK", 1 means they clicked "Cancel"
            if (result != 0) {
                return;
            }

            needsEraseFirst = true;
        }

        rebootToDfu(port, callbacks, Fields.CMD_REBOOT_DFU);

        if (!ProgramSelector.IS_WIN) {
            callbacks.log("Switched to DFU mode!");
            callbacks.log("FOME console can only program on Windows");
            return;
        }

        boolean finalNeedsEraseFirst = needsEraseFirst;
        submitAction(() -> {
            timeForDfuSwitch(callbacks);
            executeDFU(callbacks, finalNeedsEraseFirst);
        });
    }

    private static void submitAction(Runnable r) {
        ExecHelper.submitAction(r, DfuFlasher.class + " thread");
    }

    public static void rebootToDfu(String port, UpdateOperationCallbacks callbacks, String command) {
        callbacks.log("Using selected " + port + "\n");

        IoStream stream;
        try {
            stream = LinkManager.open(port);
        } catch (IOException e) {
            callbacks.log("Error opening port: " + e);
            callbacks.error();
            return;
        }

        DfuHelper.sendDfuRebootCommand(stream, callbacks, command);
    }

    public static void runDfuEraseAsync(UpdateOperationCallbacks callbacks) {
        submitAction(() -> {
            runDfuErase(callbacks);
            // it's a lengthy operation let's signal end
            Toolkit.getDefaultToolkit().beep();
        });
    }

    private static void runDfuErase(UpdateOperationCallbacks callbacks) {
        try {
            ExecHelper.executeCommand(DFU_BINARY_LOCATION,
                    getDfuEraseCommand(),
                    DFU_BINARY, callbacks);
        } catch (FileNotFoundException e) {
            callbacks.log(e.toString());
            callbacks.error();
        }
    }

    public static void runDfuProgramming(UpdateOperationCallbacks callbacks) {
        submitAction(() -> executeDFU(callbacks, false));
    }

    private static void executeDFU(UpdateOperationCallbacks callbacks, boolean fullErase) {
        boolean driverIsHappy = detectSTM32BootloaderDriverState(callbacks);
        if (!driverIsHappy) {
            callbacks.log("*** DRIVER ERROR? *** Did you have a chance to try 'Install Drivers' button on top of FOME console start screen?");
            callbacks.error();
            return;
        }

        if (fullErase) {
            runDfuErase(callbacks);
        }

        StringBuffer stdout = new StringBuffer();
        String errorResponse;
        try {
            errorResponse = ExecHelper.executeCommand(DFU_BINARY_LOCATION,
                    getDfuWriteCommand(),
                    DFU_BINARY, callbacks, stdout);
        } catch (FileNotFoundException e) {
            callbacks.log("ERROR: " + e);
            callbacks.error();
            return;
        }

        if (stdout.toString().contains("Download verified successfully")) {
            // looks like sometimes we are not catching the last line of the response? 'Upgrade' happens before 'Verify'
            callbacks.log("SUCCESS!");
            callbacks.log("Please power cycle device to exit DFU mode");
            callbacks.done();
        } else if (stdout.toString().contains("Target device not found")) {
            callbacks.log("ERROR: Device not connected or STM32 Bootloader driver not installed?");
            appendWindowsVersion(callbacks);
            callbacks.log("ERROR: Please try installing drivers using 'Install Drivers' button on FOME splash screen");
            callbacks.log("ERROR: Alternatively please install drivers using Device Manager pointing at 'drivers/silent_st_drivers/DFU_Driver' folder");
            callbacks.error();
            appendDeviceReport(callbacks);
        } else {
            appendWindowsVersion(callbacks);
            appendDeviceReport(callbacks);
            callbacks.log(stdout.length() + " / " + errorResponse.length());
            callbacks.error();
        }
    }

    public static boolean detectSTM32BootloaderDriverState(UpdateOperationCallbacks callbacks) {
        return detectDevice(callbacks, WMIC_DFU_QUERY_COMMAND, "ConfigManagerErrorCode=0");
    }

    private static boolean detectDevice(UpdateOperationCallbacks callbacks, String queryCommand, String pattern) {
        //        long now = System.currentTimeMillis();
        StringBuffer output = new StringBuffer();
        StringBuffer error = new StringBuffer();
        ExecHelper.executeCommand(queryCommand, callbacks, output, error, null);
        callbacks.log(output.toString());
        callbacks.log(error.toString());
//        long cost = System.currentTimeMillis() - now;
//        System.out.println("DFU lookup cost " + cost + "ms");
        return output.toString().contains(pattern);
    }

    private static void appendWindowsVersion(UpdateOperationCallbacks callbacks) {
        callbacks.log("ERROR: does not look like DFU has worked!");
    }

    private static void appendDeviceReport(UpdateOperationCallbacks callbacks) {
        for (String line : getDevicesReport()) {
            if (line.contains("STM Device in DFU Mode")) {
                callbacks.log(" ******************************************************************");
                callbacks.log(" ************* YOU NEED TO REMOVE LEGACY DFU DRIVER ***************");
                callbacks.log(" ******************************************************************");
            }
            callbacks.log("Devices: " + line);
        }
    }

    private static void timeForDfuSwitch(UpdateOperationCallbacks callbacks) {
        callbacks.log("Giving time for USB enumeration...");
        try {
            // two seconds not enough on my Windows 10
            Thread.sleep(3 * Timeouts.SECOND);
        } catch (InterruptedException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String getDfuWriteCommand() throws FileNotFoundException {
        String fileName = Launcher.INPUT_FILES_PATH + File.separator + "fome.bin";
        if (fileName == null)
            throw new FileNotFoundException("File not found: fome.bin");
        // we need quotes in case if absolute path contains spaces
        String quotedAbsolutePath = "\"" + (new File(fileName).getAbsolutePath()) + "\"";

        return DFU_BINARY_LOCATION + "/" + DFU_BINARY + " -c port=usb1 -w " + quotedAbsolutePath + " 0x08000000 -v -s";
    }

    private static String getDfuEraseCommand() {
        return DFU_BINARY_LOCATION + "/" + DFU_BINARY + " -c port=usb1 -e all";
    }

    @NotNull
    static List<String> getDevicesReport() {
        // todo: assert windows 10, explicit message if not
        List<String> report = new ArrayList<>();

        try {
            Process powerShellProcess = Runtime.getRuntime().exec("powershell \"Get-PnpDevice -PresentOnly\"");
            // Getting the results
            powerShellProcess.getOutputStream().close();

            String line;
            BufferedReader stdout = new BufferedReader(new InputStreamReader(powerShellProcess.getInputStream()));
            while ((line = stdout.readLine()) != null) {
                String lowerCase = line.toLowerCase();
                if (!lowerCase.contains("stm32") && !lowerCase.contains("dfu") && !lowerCase.contains("fome"))
                    continue;
                report.add(line);
            }
            stdout.close();
            return report;
        } catch (IOException e) {
            return Collections.emptyList();
        }
    }
}
