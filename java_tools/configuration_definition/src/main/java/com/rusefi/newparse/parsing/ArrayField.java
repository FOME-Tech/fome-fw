package com.rusefi.newparse.parsing;

public class ArrayField<PrototypeType extends PrototypeField> implements Field {
    public final int[] length;
    public final Boolean iterate;
    public final PrototypeType prototype;

    /** Non-null when TunerStudio should see a dynamic shape instead of the allocated {@link #length}. */
    public final TsShape tsShape;

    public ArrayField(PrototypeType prototype, int[] length, Boolean iterate) {
        this(prototype, length, iterate, null);
    }

    public ArrayField(PrototypeType prototype, int[] length, Boolean iterate, TsShape tsShape) {
        this.length = length;
        this.iterate = iterate;
        this.prototype = prototype;
        this.tsShape = tsShape;
    }
}
