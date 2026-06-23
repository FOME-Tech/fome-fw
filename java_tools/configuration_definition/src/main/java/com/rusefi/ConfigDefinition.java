package com.rusefi;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.outputs.ConfigValueLookupWriter;
import com.rusefi.newparse.outputs.CStructWriter;
import com.rusefi.newparse.outputs.JavaFieldsWriter;
import com.rusefi.newparse.outputs.PrintStreamAlwaysUnix;
import com.rusefi.newparse.outputs.TsWriter;
import com.rusefi.newparse.parsing.Definition;
import com.rusefi.output.*;
import com.rusefi.pinout.PinoutLogic;
import com.rusefi.trigger.TriggerWheelTSLogic;
import com.rusefi.util.SystemOut;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.SimpleDateFormat;
import java.util.*;

public class ConfigDefinition {
    private static final String KEY_DEFINITION = "-definition";
    private static final String KEY_TS_TEMPLATE = "-ts_template";
    private static final String KEY_C_DESTINATION = "-c_destination";
    private static final String KEY_C_DEFINES = "-c_defines";
    private static final String KEY_JAVA_DESTINATION = "-java_destination";
    public static final String KEY_PREPEND = "-prepend";
    private static final String KEY_ZERO_INIT = "-initialize_to_zero";
    private static final String KEY_BOARD_NAME = "-board";
    /**
     * This flag controls if we assign default zero value (useful while generating structures used for class inheritance)
     * versus not assigning default zero value like we need for non-class headers
     * This could be related to configuration header use-case versus "live data" (not very alive idea) use-case
     */
    public static boolean needZeroInit = true;

    public static void main(String[] args) {
        try {
            doJob(args, new ReaderStateImpl());
        } catch (Throwable e) {
            SystemOut.println(e);
            e.printStackTrace();
            SystemOut.close();
            System.exit(-1);
        }
        SystemOut.close();
    }

    public static void doJob(String[] args, ReaderStateImpl state) throws IOException {
        if (args.length < 2) {
            SystemOut.println("Please specify\r\n"
                    + KEY_DEFINITION + " x\r\n"
                    + KEY_TS_TEMPLATE + " x\r\n"
                    + KEY_C_DESTINATION + " x\r\n"
                    + KEY_JAVA_DESTINATION + " x\r\n"
            );
            return;
        }

        SystemOut.println(ConfigDefinition.class + " Invoked with " + Arrays.toString(args) + " from " + Paths.get("").toAbsolutePath());

        String definitionFile = null;
        String tsTemplateFile = null;
        String destCDefinesFileName = null;
        String cHeaderDestination = null;
        String tsIniDestination = null;
        String javaFieldsDestination = null;
        String makefileDepsDestination = null;
        String stampFile = null;
        String fieldLookupFile = null;
        String fieldLookupMdFile = null;
        // we postpone reading so that in case of cache hit we do less work
        String triggersInputFolder = null;
        List<String> enumInputFiles = new ArrayList<>();
        List<String> outputFiles = new ArrayList<>();
        PinoutLogic pinoutLogic = null;
        String branchName = null;
        String shortBoardName = null;
        final List<String> prependFiles = new ArrayList<>();

        ParseState parseState = new ParseState(state.getEnumsReader());

        for (int i = 0; i < args.length - 1; i += 2) {
            String key = args[i];
            switch (key) {
                case KEY_DEFINITION:
                    definitionFile = args[i + 1];
                    state.addInputFile(definitionFile);
                    break;
                case KEY_TS_TEMPLATE:
                    tsTemplateFile = args[i + 1];
                    break;
                case KEY_C_DESTINATION:
                    cHeaderDestination = args[i + 1];
                    break;
                case KEY_ZERO_INIT:
                    needZeroInit = Boolean.parseBoolean(args[i + 1]);
                    break;
                case KEY_C_DEFINES:
                    destCDefinesFileName = args[i + 1];
                    break;
                case KEY_JAVA_DESTINATION:
                    javaFieldsDestination = args[i + 1];
                    break;
                case "-field_lookup_file": {
                    fieldLookupFile = args[i + 1];
                    fieldLookupMdFile = args[i + 2];
                    i++;
                }
                    break;
                case "-readfile":
                    String keyName = args[i + 1];
                    // yes, we take three parameters here thus pre-increment!
                    String fileName = args[++i + 1];
                    try {
                        parseState.addDefinition(state.getVariableRegistry(), keyName, IoUtil2.readFile(fileName), Definition.OverwritePolicy.NotAllowed);
                    } catch (RuntimeException e) {
                        throw new IllegalStateException("While processing " + fileName, e);
                    }
                    state.addInputFile(fileName);
                    break;
                case "-triggerInputFolder":
                    triggersInputFolder = args[i + 1];
                    break;
                case KEY_PREPEND: {
                    String prependFile = args[i + 1].trim();
                    prependFiles.add(prependFile);
                    state.addInputFile(prependFile);
                    break;
                } case EnumToString.KEY_ENUM_INPUT_FILE:
                    enumInputFiles.add(args[i + 1]);
                    break;
                case "-ts_output_name":
                    tsIniDestination = args[i + 1];
                    break;
                case KEY_BOARD_NAME:
                    String boardName = args[i + 1];
                    pinoutLogic = PinoutLogic.create(boardName);
                    for (String inputFile : pinoutLogic.getInputFiles())
                        state.addInputFile(inputFile);
                    break;
                case "-branch":
                    branchName = args[i + 1];
                    break;
                case "-boardName":
                    shortBoardName = args[i + 1];
                    break;
                case "-makefileDep":
                    makefileDepsDestination = args[i + 1];
                    break;
                case "-stampFile":
                    stampFile = args[i + 1];
                    break;
            }
        }

        if (tsTemplateFile != null) {
            // used to update .ini files
            state.addInputFile(tsTemplateFile);
        }

        if (!enumInputFiles.isEmpty()) {
            for (String ef : enumInputFiles) {
                state.read(new FileReader(ef));
                state.addInputFile(ef);
            }

            SystemOut.println(state.getEnumsReader().getEnums().size() + " total enumsReader");
        }

        parseState.updateEnumsFromReader();

        {
            // Add the variable for the config signature
            String signature = buildSignature(branchName, shortBoardName, Long.toString(IoUtil2.getCrc32(state.getInputFiles())));
            parseState.addDefinition(state.getVariableRegistry(), "TS_SIGNATURE", signature, Definition.OverwritePolicy.NotAllowed);
            System.out.println("Signature: " + signature);
        }

        if (triggersInputFolder != null) {
            state.addInputFile(triggersInputFolder + File.separator + "triggers.txt");
        }

        if (makefileDepsDestination != null) {
            // Use stamp file as target if provided, otherwise fall back to cHeaderDestination
            String depTarget = stampFile != null ? stampFile : cHeaderDestination;
            if (depTarget != null) {
                // Build list of output files
                if (cHeaderDestination != null) {
                    outputFiles.add(cHeaderDestination);
                }
                if (destCDefinesFileName != null) {
                    outputFiles.add(destCDefinesFileName);
                }
                if (fieldLookupFile != null) {
                    outputFiles.add(fieldLookupFile);
                }
                writeMakefileDependencyFile(state.getInputFiles(), depTarget, makefileDepsDestination, outputFiles);
            }
        }

        new TriggerWheelTSLogic().execute(triggersInputFolder, state.getVariableRegistry());

        if (pinoutLogic != null) {
            pinoutLogic.registerBoardSpecificPinNames(state.getVariableRegistry(), parseState, state.getEnumsReader());
        }

        // Parse the input files
        {
            // Load prepend files
            {
                // Ignore duplicates of definitions made during prepend phase
                parseState.setDefinitionPolicy(Definition.OverwritePolicy.IgnoreNew);

                for (String prependFile : prependFiles) {
                    RusefiParseErrorStrategy.parseDefinitionFile(parseState.getListener(), prependFile);
                }
            }

            // Now load the main config file
            {
                // don't allow duplicates in the main file
                parseState.setDefinitionPolicy(Definition.OverwritePolicy.NotAllowed);
                RusefiParseErrorStrategy.parseDefinitionFile(parseState.getListener(), definitionFile);
            }

            // Write C structs (new parser is now the source of truth)
            CStructWriter cStructs = new CStructWriter();
            cStructs.writeCStructs(parseState, cHeaderDestination);

            // Write tunerstudio layout (new parser is now the source of truth)
            TsWriter writer = new TsWriter();
            writer.writeTunerstudio(parseState, tsTemplateFile, tsIniDestination);

            // Total config page size, consumed by rusefi_generated.h (firmware static_assert) and
            // Fields.java (java console). Previously registered by TSProjectConsumer.handleEndStruct,
            // which the new TsWriter replaces.
            int totalConfigSize = new StructLayout(0, "root", parseState.getLastStruct()).getSize();
            state.getVariableRegistry().register("TOTAL_CONFIG_SIZE", totalConfigSize);

            // Bridge the new parser's #define values into the variable registry, which derives the
            // C defines (rusefi_generated.h) and the Java constants (Fields.java). _char/_hex/_16_hex
            // are synthesized by VariableRegistry.register itself, so skip the parser's own copies.
            for (Map.Entry<String, Definition> def : parseState.getDefinitions().entrySet()) {
                String defName = def.getKey();
                if (defName.endsWith("_char")
                        || defName.endsWith(VariableRegistry._HEX_SUFFIX)
                        || defName.endsWith(VariableRegistry._16_HEX_SUFFIX)) {
                    continue;
                }
                if (def.getValue().isMultilineString()) {
                    continue;
                }
                state.getVariableRegistry().register(defName, def.getValue().toString());
            }

            // Write Java fields: constants from the registry (typed correctly), field offsets from
            // the new layout.
            if (javaFieldsDestination != null) {
                JavaFieldsWriter javaWriter = new JavaFieldsWriter(javaFieldsDestination, 0);
                javaWriter.writeRawDefinitions(state.getVariableRegistry().getJavaConstants());
                javaWriter.writeFields(parseState);
                javaWriter.finish();
            }

            // Write the Lua value_lookup (getConfigValueByName / setConfigValueByName)
            if (fieldLookupFile != null) {
                new ConfigValueLookupWriter(fieldLookupFile, fieldLookupMdFile).write(parseState);
            }
        }

        if (destCDefinesFileName != null) {
            ExtraUtil.writeDefinesToFile(state.getVariableRegistry(), destCDefinesFileName);
        }
    }

    private static String buildSignature(String branch, String boardName, String inputFilesHash) {
        SimpleDateFormat df = new SimpleDateFormat("yyyy.MM.dd");

        return "\"rusEFI (FOME) " + branch + "." + df.format(new Date()) + "."+ boardName + "." + inputFilesHash + "\"";
    }

    private static void writeMakefileDependencyFile(List<String> inputFiles, String targetFile, String makefileDepsDestination, List<String> outputFiles) throws IOException {
        Path path = Paths.get(makefileDepsDestination);
        Files.createDirectories(path.getParent());
        PrintStream f = new PrintStreamAlwaysUnix(Files.newOutputStream(path));

        // The target (stamp file or generated header) depends on all inputs
        f.print(targetFile + ": ");
        for (String input : inputFiles) {
            f.print(input);
            f.print(" ");
        }
        f.println();

        // Inform make of all inputs, these have no dependencies
        for (String input : inputFiles) {
            f.println(input + ":");
            f.println();
        }

        f.close();

        // Write the outputs .mk file for make to know about generated files
        writeOutputsMkFile(path.getParent(), outputFiles);
    }

    private static void writeOutputsMkFile(Path depDir, List<String> outputFiles) throws IOException {
        Path mkPath = depDir.resolve("generated_config_outputs.mk");
        PrintStream f = new PrintStreamAlwaysUnix(Files.newOutputStream(mkPath));

        f.print("CONFIG_GENERATED_FILES +=");
        for (String output : outputFiles) {
            f.print(" \\\n\t" + output);
        }
        f.println();

        f.close();
    }
}
