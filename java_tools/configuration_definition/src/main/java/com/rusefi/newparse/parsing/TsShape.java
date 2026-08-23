package com.rusefi.newparse.parsing;

/**
 * Overrides the shape TunerStudio sees for an array, decoupling it from the array's allocated size.
 *
 * Used by resizable tables: the C array is allocated at its maximum size, but the ini describes its
 * shape with expressions referencing the generated row/column count fields, so TunerStudio only
 * reads/writes the part currently in use and can offer its resize tools.
 *
 * Dimension names are stored unprefixed - they're resolved through the StructNamePrefixer at ini
 * write time, so they pick up the same struct prefix as the array they size.
 */
public class TsShape {
    /** Names of the count fields, in TunerStudio's [columns x rows] order. */
    public final String[] dimensionFields;

    /**
     * Total cell budget for a resizable 2D array, emitted as maximumElements. Null for the 1D bin
     * arrays, which are bounded by their own count field alone.
     */
    public final Integer maxElements;

    public TsShape(String[] dimensionFields, Integer maxElements) {
        this.dimensionFields = dimensionFields;
        this.maxElements = maxElements;
    }
}
