package com.rusefi.output;

import com.rusefi.*;
import com.rusefi.util.LazyFile;
import com.rusefi.util.Output;
import com.rusefi.util.SystemOut;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import static com.rusefi.util.IoUtils.CHARSET;

/**
 * [Constants]
 */
public class TSProjectConsumer implements ConfigurationConsumer {
    private static final String CONFIG_DEFINITION_START = "CONFIG_DEFINITION_START";
    private static final String CONFIG_DEFINITION_END = "CONFIG_DEFINITION_END";
    private static final String TS_CONDITION = "@@if_";
    private static final String FIELD_DIRECTIVE = "field =";
    private static final String G0_PRESENT_FIELD = "g0Present";
    private static final String NO_G0_SUFFIX = "_nog0";
    public static final String SETTING_CONTEXT_HELP_END = "SettingContextHelpEnd";
    public static final String SETTING_CONTEXT_HELP = "SettingContextHelp";

    private final String tsPath;
    private final ReaderStateImpl state;
    private int totalTsSize;
    private final TsOutput tsOutput;

    public TSProjectConsumer(String tsPath, ReaderStateImpl state) {
        this.tsPath = tsPath;
        tsOutput = new TsOutput(true);
        this.state = state;
    }

    // also known as TS tooltips
    public String getSettingContextHelpForUnitTest() {
        return tsOutput.getSettingContextHelp();
    }

    protected void writeTunerStudioFile(String inputFile, String fieldsSection) throws IOException {
        TsFileContent tsContent = readTsTemplateInputFile(inputFile);
        SystemOut.println("Got " + tsContent.getPrefix().length() + "/" + tsContent.getPostfix().length() + " from " + inputFile);

        // File.getPath() would eliminate potential separator at the end of the path
        String fileName = state.getTsFileOutputName();
        Output tsHeader = new LazyFile(fileName);
        writeContent(fieldsSection, tsContent, tsHeader);
    }

    protected void writeContent(String fieldsSection, TsFileContent tsContent, Output tsHeader) throws IOException {
        StringBuilder content = new StringBuilder();

        content.append(tsContent.getPrefix());

        content.append("; ").append(CONFIG_DEFINITION_START).append(ToolUtil.EOL);
        content.append("pageSize            = ").append(totalTsSize).append(ToolUtil.EOL);
        content.append("page = 1").append(ToolUtil.EOL);
        content.append(fieldsSection);
        if (!tsOutput.getSettingContextHelp().isEmpty()) {
            content.append("[").append(SETTING_CONTEXT_HELP).append("]").append(ToolUtil.EOL);
            content.append(tsOutput.getSettingContextHelp()).append(ToolUtil.EOL).append(ToolUtil.EOL);
            content.append("; ").append(SETTING_CONTEXT_HELP_END).append(ToolUtil.EOL);
        }
        content.append("; ").append(CONFIG_DEFINITION_END).append(ToolUtil.EOL);
        content.append(tsContent.getPostfix());

        tsHeader.write(applyG0PinVisibility(content.toString()));
        tsHeader.close();
    }

    public static String applyG0PinVisibility(String content) {
        String[] lines = content.split("\\r?\\n", -1);
        Map<String, String> g0PinFields = new LinkedHashMap<>();
        StringBuilder withAlternatePinDefs = new StringBuilder();

        for (String line : lines) {
            withAlternatePinDefs.append(line).append(ToolUtil.EOL);

            AlternatePinField alternateField = createAlternatePinField(line);
            if (alternateField != null) {
                g0PinFields.put(alternateField.originalName, alternateField.alternateName);
                withAlternatePinDefs.append(alternateField.definitionLine).append(ToolUtil.EOL);
            }
        }

        if (g0PinFields.isEmpty()) {
            return content;
        }

        StringBuilder result = new StringBuilder();
        for (String line : withAlternatePinDefs.toString().split("\\r?\\n", -1)) {
            DialogFieldReplacement replacement = createDialogFieldReplacement(line, g0PinFields);
            if (replacement != null) {
                result.append(replacement.whenPresent).append(ToolUtil.EOL);
                result.append(replacement.whenMissing).append(ToolUtil.EOL);
            } else {
                result.append(line).append(ToolUtil.EOL);
            }
        }

        return result.toString();
    }

    /**
     * tunerstudio.template.ini has all the content of the future .ini file with the exception of data page
     * TODO: start generating [outputs] section as well
     */
    private TsFileContent readTsTemplateInputFile(String fileName) throws IOException {
        BufferedReader r = new BufferedReader(new InputStreamReader(Files.newInputStream(Paths.get(fileName)), CHARSET));

        StringBuilder prefix = new StringBuilder();
        StringBuilder postfix = new StringBuilder();

        boolean isBeforeStartTag = true;
        boolean isAfterEndTag = false;
        String line;
        while ((line = r.readLine()) != null) {
            if (line.contains(CONFIG_DEFINITION_START)) {
                isBeforeStartTag = false;
                continue;
            }
            if (line.contains(CONFIG_DEFINITION_END)) {
                isAfterEndTag = true;
                continue;
            }

            if (line.contains(TS_CONDITION)) {
                String token = getToken(line);
                String strValue = state.getVariableRegistry().get(token);
                boolean value = Boolean.parseBoolean(strValue);
                if (!value)
                    continue; // skipping this line
                line = removeToken(line);
            }

            line = state.getVariableRegistry().applyVariables(line);

            if (isBeforeStartTag) {
                prefix.append(line);
                prefix.append(ToolUtil.EOL);
            }

            if (isAfterEndTag) {
                postfix.append(state.getVariableRegistry().applyVariables(line));
                postfix.append(ToolUtil.EOL);
            }
        }
        r.close();
        return new TsFileContent(prefix.toString(), postfix.toString());
    }

    public static String removeToken(String line) {
        int index = line.indexOf(TS_CONDITION);
        String token = getToken(line);
        int afterTokenIndex = index + TS_CONDITION.length() + token.length();
        if (afterTokenIndex < line.length())
            afterTokenIndex++; // skipping one whitestace after token
        line = line.substring(0, index) + line.substring(afterTokenIndex);
        return line;
    }

    public static String getToken(String line) {
        int index = line.indexOf(TS_CONDITION) + TS_CONDITION.length();
        StringBuilder token = new StringBuilder();
        while (index < line.length() && !Character.isWhitespace(line.charAt(index))) {
            token.append(line.charAt(index));
            index++;
        }
        return token.toString();
    }

    private static AlternatePinField createAlternatePinField(String line) {
        int equalsIndex = line.indexOf('=');
        if (equalsIndex < 0) {
            return null;
        }

        String fieldName = line.substring(0, equalsIndex).trim();
        if (fieldName.isEmpty() || fieldName.startsWith(";")) {
            return null;
        }

        List<String> tokens = splitTopLevelCsv(line.substring(equalsIndex + 1));
        if (tokens.size() < 6) {
            return null;
        }

        String typeToken = tokens.get(0).trim();
        String storageToken = tokens.get(1).trim();
        if (!"bits".equals(typeToken) || !(storageToken.equals("U08") || storageToken.equals("U16"))) {
            return null;
        }

        boolean containsG0Option = false;
        List<String> filteredTokens = new ArrayList<>();
        for (String token : tokens) {
            if (isG0OptionToken(token)) {
                containsG0Option = true;
                continue;
            }
            filteredTokens.add(token.trim());
        }

        if (!containsG0Option) {
            return null;
        }

        String alternateName = fieldName + NO_G0_SUFFIX;
        String definitionLine = alternateName + " = " + String.join(", ", filteredTokens);
        return new AlternatePinField(fieldName, alternateName, definitionLine);
    }

    private static DialogFieldReplacement createDialogFieldReplacement(String line, Map<String, String> g0PinFields) {
        int fieldDirectiveIndex = line.indexOf(FIELD_DIRECTIVE);
        if (fieldDirectiveIndex < 0) {
            return null;
        }

        String indent = line.substring(0, fieldDirectiveIndex);
        List<String> tokens = splitTopLevelCsv(line.substring(fieldDirectiveIndex + FIELD_DIRECTIVE.length()));
        if (tokens.size() < 2) {
            return null;
        }

        String fieldName = tokens.get(1).trim();
        String alternateFieldName = g0PinFields.get(fieldName);
        if (alternateFieldName == null) {
            return null;
        }

        List<String> whenPresent = new ArrayList<>(trimmedCopy(tokens));
        List<String> whenMissing = new ArrayList<>(whenPresent);
        whenMissing.set(1, alternateFieldName);

        applyVisibilityCondition(whenPresent, G0_PRESENT_FIELD);
        applyVisibilityCondition(whenMissing, "!" + G0_PRESENT_FIELD);

        return new DialogFieldReplacement(
                indent + FIELD_DIRECTIVE + " " + String.join(", ", whenPresent),
                indent + FIELD_DIRECTIVE + " " + String.join(", ", whenMissing)
        );
    }

    private static List<String> trimmedCopy(List<String> tokens) {
        List<String> result = new ArrayList<>(tokens.size());
        for (String token : tokens) {
            result.add(token.trim());
        }
        return result;
    }

    private static void applyVisibilityCondition(List<String> tokens, String condition) {
        if (tokens.size() == 2) {
            tokens.add(wrapCondition(condition, null));
            return;
        }

        int visibilityIndex = -1;
        if (isExpressionToken(tokens.get(2))) {
            visibilityIndex = 2;
        } else if (tokens.size() > 3 && isExpressionToken(tokens.get(3))) {
            visibilityIndex = 3;
        }

        if (visibilityIndex == -1) {
            tokens.add(wrapCondition(condition, null));
        } else {
            tokens.set(visibilityIndex, wrapCondition(condition, tokens.get(visibilityIndex)));
        }
    }

    private static boolean isExpressionToken(String token) {
        String trimmed = token.trim();
        return trimmed.startsWith("{") && trimmed.endsWith("}");
    }

    private static String wrapCondition(String condition, String existingToken) {
        if (existingToken == null) {
            return "{ " + condition + " }";
        }

        String trimmed = existingToken.trim();
        String expression = trimmed.substring(1, trimmed.length() - 1).trim();
        return "{ (" + condition + ") && (" + expression + ") }";
    }

    private static boolean isG0OptionToken(String token) {
        String trimmed = token.trim();
        return trimmed.matches("\\d+=\"G0 [^\"]+\"");
    }

    private static List<String> splitTopLevelCsv(String line) {
        List<String> tokens = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        boolean inQuotes = false;
        int braceDepth = 0;
        int bracketDepth = 0;
        int parenDepth = 0;

        for (int i = 0; i < line.length(); i++) {
            char ch = line.charAt(i);

            if (ch == '"' && (i == 0 || line.charAt(i - 1) != '\\')) {
                inQuotes = !inQuotes;
            } else if (!inQuotes) {
                switch (ch) {
                    case '{':
                        braceDepth++;
                        break;
                    case '}':
                        braceDepth--;
                        break;
                    case '[':
                        bracketDepth++;
                        break;
                    case ']':
                        bracketDepth--;
                        break;
                    case '(':
                        parenDepth++;
                        break;
                    case ')':
                        parenDepth--;
                        break;
                    case ',':
                        if (braceDepth == 0 && bracketDepth == 0 && parenDepth == 0) {
                            tokens.add(current.toString().trim());
                            current.setLength(0);
                            continue;
                        }
                        break;
                    default:
                        break;
                }
            }

            current.append(ch);
        }

        tokens.add(current.toString().trim());
        return tokens;
    }

    private static class AlternatePinField {
        private final String originalName;
        private final String alternateName;
        private final String definitionLine;

        private AlternatePinField(String originalName, String alternateName, String definitionLine) {
            this.originalName = originalName;
            this.alternateName = alternateName;
            this.definitionLine = definitionLine;
        }
    }

    private static class DialogFieldReplacement {
        private final String whenPresent;
        private final String whenMissing;

        private DialogFieldReplacement(String whenPresent, String whenMissing) {
            this.whenPresent = whenPresent;
            this.whenMissing = whenMissing;
        }
    }

    @Override
    public void endFile() throws IOException {
        writeTunerStudioFile(tsPath, getContent());
    }

    @Override
    public void handleEndStruct(ReaderState readerState, ConfigStructure structure) {
        state.getVariableRegistry().register(structure.getName() + "_size", structure.getTotalSize());
        totalTsSize = tsOutput.run(readerState, structure, 0);

        if (state.isStackEmpty()) {
            state.getVariableRegistry().register("TOTAL_CONFIG_SIZE", totalTsSize);
        }
    }

    public String getContent() {
        return tsOutput.getContent();
    }
}
