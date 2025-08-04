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

    public Field(String name, int offset, FieldType type) {
        this(name, offset, type, NO_BIT_OFFSET);
    }

    public Field(String name, int offset, FieldType type, int bitOffset) {
        this(name, offset, 0, type, bitOffset);
    }

    public Field(String name, int offset, int stringSize, FieldType type, int bitOffset) {
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

    public String getName() {
        return name;
    }

    public String setCommand() {
        if (type == FieldType.BIT)
            return "set_bit " + getOffset() + " " + bitOffset;
        return getType().getStoreCommand() + " " + getOffset();
    }

    public String getCommand() {
        if (type == FieldType.BIT)
            return "get_bit " + getOffset() + " " + bitOffset;
        return type.getLoadCommand() + " " + getOffset();
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

    public int getBitOffset() {
        return bitOffset;
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

    @NotNull
    public ByteBuffer getByteBuffer(ConfigurationImage ci) {
        return ci.getByteBuffer(getOffset(), 4);
    }

    public static Field create(String name, int offset, FieldType type, int bitOffset) {
        return new Field(name, offset, type, bitOffset);
    }

    public static Field create(String name, int offset, int stringSize, FieldType type) {
        return new Field(name, offset, stringSize, type, 0);
    }

    public static Field create(String name, int offset, FieldType type) {
        return new Field(name, offset, type);
    }

    public Field setScale(double scale) {
        return this;
    }

    public Field setBaseOffset(int baseOffset) {
        this.baseOffset = baseOffset;
        return this;
    }
}