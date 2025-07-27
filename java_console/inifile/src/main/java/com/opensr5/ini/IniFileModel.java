package com.opensr5.ini;

import com.devexperts.logging.Logging;
import com.opensr5.ini.field.*;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

import java.io.*;
import java.util.*;

/**
 * Andrey Belomutskiy, (c) 2013-2020
 * 12/23/2015.
 */
public class IniFileModel {
    private static final Logging log = Logging.getLogging(IniFileModel.class);
    public static final String FOME_INI_PREFIX = "fome_";
    public static final String FOME_INI_SUFFIX = ".ini";
    public static final String INI_FILE_PATH = System.getProperty("ini_file_path", "..");
    public static final String FIELD_TYPE_STRING = "string";

    private static IniFileModel INSTANCE;

    private File sourceFile;

    private String signature = null;

    public void findAndReadIniFile() throws IOException {
        File input = findFile(INI_FILE_PATH, FOME_INI_PREFIX, FOME_INI_SUFFIX);

        log.info("Reading " + input.getCanonicalPath());
        RawIniFile content = IniFileReader.read(input);

        readIniFile(content);
        sourceFile = input;
    }

    public IniFileModel readIniFile(RawIniFile content) {
        for (RawIniFile.Line line : content.getLines()) {
            handleLine(line);
        }

        return this;
    }

    @NotNull
    private static File findFile(String fileDirectory, String prefix, String suffix) throws IOException {
        File dir = new File(fileDirectory);
        if (!dir.isDirectory()) {
            throw new IllegalArgumentException("Cannot search path " + fileDirectory + " as it is not a directory");
        }

        log.info("Searching for " + prefix + "*" + suffix + " in " + fileDirectory);
        for (File f : dir.listFiles()) {
            if (f.getName().startsWith(prefix) && f.getName().endsWith(suffix)) {
                return f;
            }
        }

        throw new FileNotFoundException("No matching ini found in dir " + dir.getCanonicalPath());
    }

    public File getSourceFile() {
        return sourceFile;
    }

    private void handleLine(RawIniFile.Line line) {

        String rawText = line.getRawText();
        try {
            LinkedList<String> list = new LinkedList<>(Arrays.asList(line.getTokens()));
            String first = list.getFirst();

            if ("signature".equals(first)) {
                handleSignature(list);
            }
        } catch (RuntimeException e) {
            throw new IllegalStateException("While [" + rawText + "]", e);
        }
    }

    private void handleSignature(LinkedList<String> list) {
        list.removeFirst(); // "signature"

        this.signature = list.removeFirst().replace("\"", "");

        log.debug("IniFileModel: ECU signature: " + signature);
    }

    public String getSignature() {
        return signature;
    }

    public static synchronized IniFileModel getInstance() throws IOException {
        if (INSTANCE == null) {
            INSTANCE = new IniFileModel();
            INSTANCE.findAndReadIniFile();
        }
        return INSTANCE;
    }
}
