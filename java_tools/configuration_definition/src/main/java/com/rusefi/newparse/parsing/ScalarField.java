package com.rusefi.newparse.parsing;

public class ScalarField extends PrototypeField {
    public final Type type;
    public final FieldOptions options;
    public final Boolean autoscale;
    public final boolean autotemp;

    /**
     * Emitted as a defaultValue in [ConstantsExtensions] when set. Resizable tables need this so
     * TunerStudio has a valid table size before it has ever talked to an ECU or loaded a tune.
     */
    public Integer tsDefaultValue = null;

    public ScalarField(Type type, String name, FieldOptions options, boolean autoscale, boolean autotemp) {
        super(name);

        this.type = type;
        this.options = options;
        this.autoscale = autoscale;
        this.autotemp = autotemp;
    }

    @Override
    public String toString() {
        return type.cType + " " + name;
    }
}
