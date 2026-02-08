package com.rusefi;

import com.rusefi.enum_reader.Value;
import com.rusefi.util.LazyFile;
import com.rusefi.util.SystemOut;

import java.io.*;
import java.util.*;

/**
 * Andrey Belomutskiy, (c) 2013-2020
 * <p/>
 * 10/6/14
 */
@SuppressWarnings("StringConcatenationInsideStringBufferAppend")
public class EnumToString {
    private final StringBuilder includesSection = new StringBuilder();

    /**
     * same header for .cpp and .h
     */
    private final StringBuilder headerFileContent = new StringBuilder();

    public final static String KEY_ENUM_INPUT_FILE = "-enumInputFile";

    public static void main(String[] args) throws IOException {
        InvokeReader invokeReader = new InvokeReader(args).invoke();
        EnumToString instance = new EnumToString();
        instance.handleRequest(invokeReader);
    }

    public void handleRequest(InvokeReader invokeReader) throws IOException {
        String outputPath = invokeReader.getOutputPath();

        EnumsReader enumsReader = new EnumsReader();
        EnumToString state = new EnumToString();

        headerFileContent.append("#pragma once\n");

        for (String inputFile : invokeReader.getInputFiles()) {
            state.consumeFile(enumsReader, invokeReader.getInputPath(), inputFile);
        }

        state.outputData(enumsReader);

        headerFileContent.insert(0, state.includesSection);

        SystemOut.println("includesSection:\n" + state.includesSection + "end of includesSection\n");

        new File(outputPath).mkdirs();
        state.writeCppAndHeaderFiles(outputPath + File.separator + "auto_generated_" +
                InvokeReader.fileSuffix);
        SystemOut.close();
    }

    private void writeCppAndHeaderFiles(String outFileName) throws IOException {
        LazyFile bw = new LazyFile(outFileName + ".h");
        bw.write(headerFileContent.toString());
        bw.close();
    }

    public void consumeFile(EnumsReader enumsReader, String inputPath, String headerInputFileName) throws IOException {
        Objects.requireNonNull(inputPath, "inputPath");
        File f = new File(inputPath + File.separator + headerInputFileName);
        SystemOut.println("Reading enums from " + headerInputFileName);

        includesSection.append("#include \"" + f.getName() + "\"\n");
        enumsReader.read(new FileReader(f));
    }

    public EnumToString outputData(EnumsReader enumsReader) {
        SystemOut.println("Preparing output for " + enumsReader.getEnums().size() + " enums\n");

        for (Map.Entry<String, EnumsReader.EnumState> e : enumsReader.getEnums().entrySet()) {
            String enumName = e.getKey();
            EnumsReader.EnumState enumState = e.getValue();
            headerFileContent.append(makeCode(enumName, enumState));
        }
        SystemOut.println("EnumToString: " + headerFileContent.length() + " bytes of content\n");
        return this;
    }

    private static String makeCode(String enumName, EnumsReader.EnumState enumState) {
        StringBuilder sb = new StringBuilder();
        Collection<Value> values = enumState.values.values();
        sb.append(getMethodSignature(enumName) + " {\n");

        sb.append("\tswitch (value) {\n");

        for (Value e : values) {
            sb.append("\t\tcase ");
            if (enumState.isEnumClass) {
                sb.append(enumState.enumName).append("::");
            }
            sb.append(e.getName() + ":\n");
            sb.append("\t\t\treturn \"" + e.getName() + "\";\n");
        }

        sb.append("\t}\n");
        sb.append("\treturn \"unknown\";\n");
        sb.append("}\n");

        return sb.toString();
    }

    private static String getMethodSignature(String enumName) {
        return "constexpr inline const char* get" + capitalize(enumName) + "(" + enumName + " value)";
    }

    private static String capitalize(String enumName) {
        return Character.toUpperCase(enumName.charAt(0)) + enumName.substring(1);
    }

    public String getContent() {
        return headerFileContent.toString();
    }
}
