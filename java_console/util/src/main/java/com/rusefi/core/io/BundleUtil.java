package com.rusefi.core.io;

import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.File;
import java.net.URL;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.InvalidPathException;
import java.util.Date;

public class BundleUtil {
    /**
     * The deployed bundle layout is:
     *   <bundle>/
     *     fome.bin
     *     fome_update.srec
     *     console/
     *       fome_console.jar   <-- this class lives here
     *       STM32_Programmer_CLI/
     *
     * @return the directory containing the console JAR, or null when running from a classes
     * directory (e.g. IDE) where the bundle layout doesn't apply.
     */
    @Nullable
    public static File getConsoleDir() {
        try {
            URL location = BundleUtil.class.getProtectionDomain().getCodeSource().getLocation();
            File jar = new File(location.toURI());
            if (!jar.isFile() || !jar.getName().endsWith(".jar")) return null;
            return jar.getParentFile();
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * @return the bundle root (one level above the console JAR), or null if not in a deployed bundle.
     */
    @Nullable
    public static File getBundleRoot() {
        File console = getConsoleDir();
        return console == null ? null : console.getParentFile();
    }

    /**
     * @return null in case of error
     */
    @Nullable
    public static String readBundleFullName() {
        File root = getBundleRoot();
        if (root != null) {
            String name = root.getName();
            if (name != null && name.length() >= 3) return name;
        }
        // Legacy CWD-based fallback for running out of an IDE
        try {
            Path path = Paths.get("").toAbsolutePath();
            String fullName = path.getParent().getFileName().toString();
            if (fullName.length() < 3)
                return null; // just paranoia check
            return fullName;
        } catch (InvalidPathException e) {
            System.err.println(new Date() + ": BundleUtil: Error reading bundle name");
            return null;
        }
    }

    @NotNull
    public static String readBundleFullNameNotNull() {
        String bundle = readBundleFullName();
        bundle = bundle == null ? "unknown bundle" : bundle;
        return bundle;
    }

    public static String getBundleTarget() {
        return getBundleTarget(readBundleFullName());
    }

    public static String getBundleTarget(String s) {
        if (s == null) {
            return null;
        }

        int lastDot = s.lastIndexOf('.');
        if (lastDot == -1) {
            return null;
        }

        return s.substring(lastDot + 1);
    }
}
