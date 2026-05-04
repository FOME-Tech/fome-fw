package com.rusefi;

import com.devexperts.logging.Logging;
import com.rusefi.config.generated.Fields;
import com.rusefi.core.io.BundleUtil;
import com.rusefi.core.rusEFIVersion;
import com.rusefi.tools.ConsoleTools;
import com.rusefi.ui.engine.EngineSnifferPanel;
import com.rusefi.core.preferences.storage.PersistentConfiguration;

import java.io.File;
import java.util.Date;

import static com.devexperts.logging.Logging.getLogging;

/**
 * this is the main entry point of rusEfi ECU console
 * <p/>
 * <p/>
 * 12/25/12
 * Andrey Belomutskiy, (c) 2013-2020
 *
 * @see StartupFrame
 * @see EngineSnifferPanel
 */
public class Launcher extends rusEFIVersion {
    private static final Logging log = getLogging(Launcher.class);
    public static final String INPUT_FILES_PATH = resolveInputFilesPath();
    public static final String TOOLS_PATH = resolveToolsPath();

    private static String resolveInputFilesPath() {
        String override = System.getProperty("input_files_path");
        if (override != null) return override;
        File root = BundleUtil.getBundleRoot();
        return root != null ? root.getAbsolutePath() : "..";
    }

    private static String resolveToolsPath() {
        String override = System.getProperty("tools_path");
        if (override != null) return override;
        File console = BundleUtil.getConsoleDir();
        return console != null ? console.getAbsolutePath() : ".";
    }

    /**
     * rusEfi console entry point
     *
     * @see StartupFrame if no parameters specified
     */
    public static void main(final String[] args) throws Exception {
        log.info("FOME UI Console for " + Fields.TS_SIGNATURE);
        log.info("Compiled " + new Date(rusEFIVersion.classBuildTimeMillis()));
        log.info("\n\n");
        PersistentConfiguration.registerShutdownHook();

        if (ConsoleTools.runTool(args)) {
            return;
        }

        ConsoleTools.printTools();

        ConsoleUI.startUi(args);
    }
}
