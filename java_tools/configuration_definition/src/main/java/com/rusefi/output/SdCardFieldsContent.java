package com.rusefi.output;

import com.rusefi.ConfigField;
import com.rusefi.ReaderState;

import java.io.IOException;

import static com.rusefi.output.JavaSensorsConsumer.quote;
import static com.rusefi.VariableRegistry.unquote;

public class SdCardFieldsContent {
    private final StringBuilder body = new StringBuilder();

    public String home = "engine->outputChannels";
    public Boolean isPtr = false;

    public void handleEndStruct(ReaderState state, ConfigStructure structure) throws IOException {
        if (state.isStackEmpty()) {
            PerFieldWithStructuresIterator iterator = new PerFieldWithStructuresIterator(state, structure.getTsFields(), "",
                    (configField, prefix, prefix2) -> processOutput(prefix, prefix2), ".");
            iterator.loop();
            String content = iterator.getContent();
            body.append(content);
        }
    }

    private String processOutput(ConfigField configField, String prefix) {
        if (configField.getName().startsWith(ConfigStructureImpl.ALIGNMENT_FILL_AT))
            return "";
        if (configField.getName().startsWith(ConfigStructure.UNUSED_ANYTHING_PREFIX))
            return "";
        if (configField.isBit())
            return "";

        if (configField.isFromIterate()) {
            String name = configField.getIterateOriginalName() + "[" + (configField.getIterateIndex() - 1) + "]";
            return getLine(configField, prefix, prefix + name);
        } else {
            return getLine(configField, prefix, prefix + configField.getName());
        }
    }

    private String getLine(ConfigField configField, String prefix, String name) {
        String categoryStr = configField.getCategory();

        if(categoryStr == null) {
            categoryStr = "";
        } else {
            categoryStr = ", " + categoryStr;
        }

        return "\t{" + home + (isPtr ? "->" : ".") + name +
                ", "
                + getHumanGaugeName(prefix, configField) +
                ", " +
                quote(configField.getUnits()) +
                ", " +
                configField.getDigits() +
                categoryStr +
                "},\n";
    }

    public String getBody() {
        return body.toString();
    }

    // https://github.com/rusefi/web_backend/issues/166
    private static final int MSQ_LENGTH_LIMIT = 34;

    private static String getFirstLine(String comment) {
        String[] comments = comment == null ? new String[0] : unquote(comment).split("\\\\n");
        comment = (comments.length > 0) ? comments[0] : "";
        return comment;
    }

    private static String getHumanGaugeName(String prefix, ConfigField configField) {
        String comment = configField.getCommentTemplated();
        comment = getFirstLine(comment);

        if (comment.isEmpty()) {
            /**
             * @see ConfigFieldImpl#getCommentOrName()
             */
            comment = prefix + unquote(configField.getName());
        }
        if (comment.length() > MSQ_LENGTH_LIMIT)
            throw new IllegalStateException("[" + comment + "] is too long for log files at " + comment.length());

        if (comment.charAt(0) != '"')
            comment = quote(comment);
        return comment;
    }
}
