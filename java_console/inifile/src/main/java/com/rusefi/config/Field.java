package com.rusefi.config;

import com.opensr5.ConfigurationImage;
import org.jetbrains.annotations.NotNull;

import java.nio.ByteBuffer;
import java.util.Objects;

import static com.rusefi.config.FieldType.*;

/**
 * @see Fields
 */

public class Field {
    public static final int NO_BIT_OFFSET = -1;
    private final String name;
    private final int offset;
    private final FieldType type;
    private final int bitOffset;
    /**
     * LiveData fragments go one after another in the overall "outputs" region
     */
    private int baseOffset;
    /**
     * For an enum field, its value names newline separated (as generated), position = value
     */
    private String enumValuesList;
    private String[] enumValues;

    public Field(String name, int offset, FieldType type) {
        this(name, offset, type, NO_BIT_OFFSET);
    }

    public Field(String name, int offset, FieldType type, int bitOffset) {
        this.name = name;
        this.offset = offset;
        this.type = type;
        this.bitOffset = bitOffset;
    }

    public static String niceToString(double value, int precision) {
        int scale = (int) Math.log10(value);
        int places = 1 + Math.max(0, precision - scale);
        double toScale = Math.pow(10, places);
        return Double.toString(Math.round(value * toScale) / toScale);
    }

    public String setCommand() {
        if (type == FieldType.BIT)
            return "set_bit " + getOffset() + " " + bitOffset;
        return getType().getStoreCommand() + " " + getOffset();
    }

    /**
     * todo: replace all (?) usages with #getTotalOffset?
     */
    public int getOffset() {
        return offset;
    }

    public int getTotalOffset() {
        return baseOffset + offset;
    }

    public FieldType getType() {
        return type;
    }

    @Override
    public String toString() {
        return "Field{" +
                name +
                ", o=" + offset +
                ", type=" + type +
                '}';
    }

    /**
     * each usage is a potential bug?! we are supposed to have explicit multiplier for each field
     */
    @NotNull
    @Deprecated
    public Double getValue(ConfigurationImage ci) {
        return getValue(ci, 1);
    }

    // todo: rename to getNumberValue?
    @NotNull
    public Double getValue(ConfigurationImage ci, double multiplier) {
        Objects.requireNonNull(ci, "ConfigurationImage");
        Number value;
        ByteBuffer wrapped = ci.getByteBuffer(getOffset(), type.getStorageSize());
        if (bitOffset != NO_BIT_OFFSET) {
            int packed = wrapped.getInt();
            value = (packed >> bitOffset) & 1;
        } else if (type == INT8) {
            value = wrapped.get();
        } else if (type == UINT8) {
            byte signed = wrapped.get();
            value = signed & 0xFF;
        } else if (type == INT) {
            value = wrapped.getInt();
        } else if (type == INT16) {
            value = wrapped.getShort();
        } else if (type == UINT16) {
            short signed = wrapped.getShort();
            value = signed & 0xFFFF;
        } else {
            value = wrapped.getFloat();
        }
        return value.doubleValue() * multiplier;
    }

    /**
     * Write a raw (storage, unscaled) value into the image, the reverse of {@link #getValue(ConfigurationImage, double)}
     *
     * @throws IllegalArgumentException if the value doesn't fit the field's storage
     */
    public void setValue(ConfigurationImage ci, double rawValue) {
        Objects.requireNonNull(ci, "ConfigurationImage");
        ByteBuffer wrapped = ci.getByteBuffer(getOffset(), type.getStorageSize());
        if (bitOffset != NO_BIT_OFFSET) {
            if (rawValue != 0 && rawValue != 1)
                throw new IllegalArgumentException(name + ": bit value must be 0 or 1, got " + rawValue);
            wrapped.mark();
            int packed = wrapped.getInt();
            packed = rawValue == 1 ? packed | (1 << bitOffset) : packed & ~(1 << bitOffset);
            wrapped.reset();
            wrapped.putInt(packed);
            return;
        }
        if (type == FLOAT) {
            wrapped.putFloat((float) rawValue);
            return;
        }

        long value = (long) rawValue;
        if (value != rawValue)
            throw new IllegalArgumentException(name + ": integer field, got " + rawValue);
        // The generated Fields don't say whether an integer is signed (U16 and S16 are both INT16),
        // so accept anything that fits the width either way
        int bits = 8 * type.getStorageSize();
        long min = -(1L << (bits - 1));
        long max = (1L << bits) - 1;
        if (value < min || value > max)
            throw new IllegalArgumentException(name + ": " + value + " doesn't fit in " + bits + " bits");

        switch (type.getStorageSize()) {
            case 1:
                wrapped.put((byte) value);
                break;
            case 2:
                wrapped.putShort((short) value);
                break;
            case 4:
                wrapped.putInt((int) value);
                break;
            default:
                throw new IllegalArgumentException(name + ": can't write " + type);
        }
    }

    public String getName() {
        return name;
    }

    public Field setEnumValues(String newlineSeparatedNames) {
        this.enumValuesList = newlineSeparatedNames;
        this.enumValues = null;
        return this;
    }

    public boolean isEnum() {
        return enumValuesList != null;
    }

    /**
     * The names of an enum field's values, as TunerStudio shows them: position = value. Values that mean nothing,
     * like a pin this board doesn't route anywhere, are named {@link #INVALID_ENUM_NAME}.
     */
    public String[] getEnumValues() {
        if (enumValuesList == null)
            throw new IllegalStateException(name + " is not an enum field");
        if (enumValues == null)
            enumValues = enumValuesList.split("\n", -1);
        return enumValues;
    }

    public static final String INVALID_ENUM_NAME = "INVALID";

    /**
     * @return the value of the enum name, e.g. a pin's TunerStudio name ("Ign 11") to its pin number
     * @throws IllegalArgumentException if the name is not exactly one of this field's values
     */
    public int enumIndex(String valueName) {
        String[] values = getEnumValues();
        int found = -1;
        for (int i = 0; i < values.length; i++) {
            if (!values[i].equals(valueName))
                continue;
            if (found != -1)
                throw new IllegalArgumentException(name + ": [" + valueName + "] is ambiguous, values " + found + " and " + i);
            found = i;
        }
        if (found == -1 || INVALID_ENUM_NAME.equals(valueName))
            throw new IllegalArgumentException(name + ": no value named [" + valueName + "]");
        return found;
    }

    /**
     * @return the name of an enum value, or null if it's outside the list
     */
    public String enumName(int value) {
        String[] values = getEnumValues();
        return value >= 0 && value < values.length ? values[value] : null;
    }

    public static Field create(String name, int offset, FieldType type, int bitOffset) {
        return new Field(name, offset, type, bitOffset);
    }

    public static Field create(String name, int offset, FieldType type) {
        return new Field(name, offset, type);
    }

    public Field setBaseOffset(int baseOffset) {
        this.baseOffset = baseOffset;
        return this;
    }
}