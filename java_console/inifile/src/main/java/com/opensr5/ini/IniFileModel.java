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
    private static final String SECTION_PAGE = "page";
    private static final String FIELD_TYPE_SCALAR = "scalar";
    public static final String FIELD_TYPE_STRING = "string";
    private static final String FIELD_TYPE_ARRAY = "array";
    private static final String FIELD_TYPE_BITS = "bits";

    private static IniFileModel INSTANCE;

    public final Map<String, IniField> allIniFields = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);

    public final Map<String, String> tooltips = new TreeMap<>();
    public final Map<String, String> protocolMeta = new TreeMap<>();
    private boolean isConstantsSection;

    private File sourceFile;

    private boolean isInSettingContextHelp = false;
    private boolean isInsidePageDefinition;

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

            if (!list.isEmpty() && list.get(0).equals(SECTION_PAGE)) {
                isInsidePageDefinition = true;
                return;
            }

            // todo: use TSProjectConsumer constant
            if (isInSettingContextHelp) {
                // todo: use TSProjectConsumer constant
                if (rawText.contains("SettingContextHelpEnd")) {
                    isInSettingContextHelp = false;
                }
                if (list.size() == 2)
                    tooltips.put(list.get(0), list.get(1));
                return;
            } else if (rawText.contains("SettingContextHelp")) {
                isInsidePageDefinition = false;
                isInSettingContextHelp = true;
                return;
            }

            if (RawIniFile.Line.isCommentLine(rawText))
                return;

            if (RawIniFile.Line.isPreprocessorDirective(rawText))
                return;

            trim(list);

            if (list.isEmpty())
                return;

            String first = list.getFirst();

            if (first.startsWith("[") && first.endsWith("]")) {
                log.info("Section " + first);
                isConstantsSection = first.equals("[Constants]");
            }

            if (isConstantsSection) {
                if (isInsidePageDefinition) {
                    if (list.size() > 1)
                        handleFieldDefinition(list);
                    return;
                } else {
                    if (list.size() > 1) {
                        protocolMeta.put(list.get(0), list.get(1));
                    }
                }
            }

        if ("signature".equals(first)) {
                handleSignature(list);
            }
        } catch (RuntimeException e) {
            throw new IllegalStateException("While [" + rawText + "]", e);
        }
    }

    private void handleFieldDefinition(LinkedList<String> list) {
        switch (list.get(1)) {
            case FIELD_TYPE_SCALAR:
                registerField(ScalarIniField.parse(list));
                break;
            case FIELD_TYPE_STRING:
                registerField(StringIniField.parse(list));
                break;
            case FIELD_TYPE_ARRAY:
                registerField(ArrayIniField.parse(list));
                break;
            case FIELD_TYPE_BITS:
                registerField(EnumIniField.parse(list));
                break;
            default:
                throw new IllegalStateException("Unexpected " + list);
        }
    }

    private void registerField(IniField field) {
        // todo: only the first occurrence should matter, but com.rusefi.ui.TuneReadWriteTest is failing when uncommented :(
        //if (allIniFields.containsKey(field.getName()))
        //	return;
        allIniFields.put(field.getName(), field);
    }

    private void handleSignature(LinkedList<String> list) {
        list.removeFirst(); // "signature"

        this.signature = list.removeFirst().replace("\"", "");

        log.debug("IniFileModel: ECU signature: " + signature);
    }

    public String getSignature() {
        return signature;
    }

    private void trim(LinkedList<String> list) {
        while (!list.isEmpty() && list.getFirst().isEmpty())
            list.removeFirst();
    }

    public static synchronized IniFileModel getInstance() throws IOException {
        if (INSTANCE == null) {
            INSTANCE = new IniFileModel();
            INSTANCE.findAndReadIniFile();
        }
        return INSTANCE;
    }
}
