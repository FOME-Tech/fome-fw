package com.rusefi.newparse.parsing;

public class EnumTypedef extends Typedef {
    public final Type type;
    public final int endBit;
    public final EnumValues values;

    public EnumTypedef(String name, Type type, int endBit, EnumValues values) {
        super(name);

        this.type = type;
        this.endBit = endBit;
        this.values = values;
    }
}
