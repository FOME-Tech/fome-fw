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
    private final boolean isIterate;
    private final ReaderStateImpl state;
    private final boolean hasAutoscale;
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
                           boolean isIterate,
                           boolean hasAutoscale,
                           String trueName,
                           String falseName) {
        this.hasAutoscale = hasAutoscale;
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
        this.isIterate = isIterate;
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
    public int getSize(ConfigField next) {
        if (isBit() && next.isBit()) {
            // we have a protection from 33+ bits in a row in BitState, see BitState.TooManyBitsInARow
            return 0;
        }
        if (isBit())
            return 4;
        int size = getElementSize();
        for (int s : arraySizes) {
            size *= s;
        }
        return size;
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
    public int getElementSize() {
        return isVoid() ? 0 : TypesHelper.getElementSize(state, type);
    }

    /**
     * this property of array expands field into a bunch of variables like field1 field2 field3 etc
     */
    @Override
    public boolean isIterate() {
        return isIterate;
    }

    @Override
    public ReaderState getState() {
        return state;
    }

    @Override
    public Pair<Integer, Integer> autoscaleSpecPair() {
        if (!hasAutoscale) {
            return null;
        }
        if (tsInfo == null)
            throw new IllegalArgumentException("tsInfo expected with autoscale: " + this);
        String[] tokens = getTokens();
        if (tokens.length < 2)
            throw new IllegalArgumentException("Second comma-separated token expected in [" + tsInfo + "] for " + name);

        String scale = tokens[1].trim();
        double factor;
        if (scale.startsWith("{") && scale.endsWith("}")) {
            // Handle just basic division, not a full fledged eval loop
            scale = scale.substring(1, scale.length() - 1);
            String[] parts = scale.split("/");
            if (parts.length != 2)
                throw new IllegalArgumentException(name + ": Two parts of division expected in " + scale);
            factor = Double.parseDouble(parts[0]) / Double.parseDouble(parts[1]);
        } else {
            factor = Double.parseDouble(scale);
        }
        int mul, div;
        if (factor < 1.d) {
            mul = (int) Math.round(1. / factor);
            div = 1;
        } else {
            mul = 1;
            div = (int) factor;
        }
        // Verify accuracy
        double factor2 = ((double) div) / mul;
        double accuracy = Math.abs((factor2 / factor) - 1.);
        if (accuracy > 0.0000001) {
            // Don't want to deal with exception propogation; this should adequately not compile
            throw new IllegalStateException("$*@#$* Cannot accurately represent autoscale for " + tokens[1]);
        }

        return new Pair<>(mul, div);
    }

    private String[] getTokens() {
        if (tsInfo == null)
            return new String[0];
        return tsInfo.split(",");
    }

    @Override
    public double getMin() {
        String[] tokens = getTokens();
        if (tokens.length < 4)
            return -1;
        return Double.parseDouble(tokens[3]);
    }

    @Override
    public double getMax() {
        String[] tokens = getTokens();
        if (tokens.length < 5)
            return -1;
        return Double.parseDouble(tokens[4]);
    }

    @Override
    public int getDigits() {
        String[] tokens = getTokens();
        if (tokens.length < 6)
            return 0;
        return Integer.parseInt(tokens[5].trim());
    }

    @Override
    public boolean isFromIterate() {
        return isFromIterate;
    }
}

