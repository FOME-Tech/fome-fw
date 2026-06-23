package com.rusefi;

import com.devexperts.logging.Logging;
import com.opensr5.ini.field.EnumIniField;
import com.rusefi.core.Pair;
import com.rusefi.output.ConfigStructure;
import org.jetbrains.annotations.Nullable;

import java.util.Arrays;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.output.JavaSensorsConsumer.quote;

/**
 * This is an immutable model of an individual field
 * Andrey Belomutskiy, (c) 2013-2020
 * 1/15/15
 */
public class ConfigFieldImpl implements ConfigField {
    public static final String VOID_NAME = "";
    public static final String BOOLEAN_T = "boolean";
    public static final String DIRECTIVE_T = "directive";

    private final String name;
    private final String comment;
    public final String arraySizeVariableName;
    private final String type;
    private final int[] arraySizes;

    private final String tsInfo;
    private final ReaderStateImpl state;
    private boolean isFromIterate;

    /**
     * todo: one day someone should convert this into a builder
     */
    public ConfigFieldImpl(ReaderStateImpl state,
                           String name,
                           String comment,
                           String arraySizeAsText,
                           String type,
                           int[] arraySizes,
                           @Nullable String tsInfo,
                           boolean hasAutoscale,
                           String trueName,
                           String falseName) {
        Objects.requireNonNull(name, comment + " " + type);
        assertNoWhitespaces(name);
        this.name = name;

        if (!isVoid())
            Objects.requireNonNull(state);
        this.state = state;
        this.comment = comment;

        if (!isVoid())
            Objects.requireNonNull(type);
        this.type = type;
        this.arraySizeVariableName = arraySizeAsText;
        this.arraySizes = arraySizes;
        this.tsInfo = tsInfo == null ? null : state.getVariableRegistry().applyVariables(tsInfo);
        if (tsInfo != null) {
            String[] tokens = getTokens();
            if (tokens.length > 1) {
                String scale = tokens[1].trim();
                double scaleDouble;
                try {
                    scaleDouble = Double.parseDouble(scale);
                } catch (NumberFormatException ignore) {
                    scaleDouble = -1.0;
                }
                if (!hasAutoscale && scaleDouble != 1) {
                    throw new IllegalStateException("Unexpected scale of " + scale + " without autoscale on " + this);
                }
            }
        }
    }

    @Override
    public boolean isArray() {
        return arraySizeVariableName != null || arraySizes.length != 0;
    }

    @Override
    public boolean isBit() {
        return BOOLEAN_T.equalsIgnoreCase(type);
    }

    @Override
    public boolean isDirective() {
        return DIRECTIVE_T.equalsIgnoreCase(type);
    }

    private boolean isVoid() {
        return name.equals(VOID_NAME);
    }

    public static void assertNoWhitespaces(String name) {
        if (name.contains(" ") || name.contains("\t"))
            throw new IllegalArgumentException("Invalid name: " + name);
    }

    @Override
    public String toString() {
        return "ConfigField{" +
                "name='" + name + '\'' +
                ", type='" + type + '\'' +
                ", arraySizes=" + Arrays.toString(arraySizes) +
                '}';
    }

    @Override
    public String getComment() {
        if (comment == null)
            return null;
        return comment.trim();
    }

    /**
     * field name without structure name
     *
     * @see JavaFieldsConsumer#writeJavaFields prefix parameter for structure name
     */
    @Override
    public String getName() {
        return name;
    }

    /**
     * @see com.rusefi.newparse.parsing.Type
     */
    @Override
    public String getType() {
        return type;
    }

    @Override
    public ReaderState getState() {
        return state;
    }

    private String[] getTokens() {
        if (tsInfo == null)
            return new String[0];
        return tsInfo.split(",");
    }

    @Override
    public boolean isFromIterate() {
        return isFromIterate;
    }
}

