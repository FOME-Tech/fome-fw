package com.rusefi.newparse.parsing;

import java.io.PrintStream;

public class FieldOptions {
    public double min;
    public double max;
    public double scale;
    public double offset;
    public int digits;
    public String units;
    public String comment;

    public FieldOptions() {
        min = 0;
        max = 0;
        scale = 1;
        offset = 0;
        digits = 0;
        units = "\"\"";
        comment = "";
    }

    // Return a copy of these options reinterpreting the same stored (Celsius) value as Fahrenheit.
    // TS computes display = (raw + offset) * scale, so to satisfy F = 1.8*C + 32 we scale by 9/5
    // and shift the translate accordingly. See firmware/f_vs_c_example.ini for the reference math.
    public FieldOptions celsiusToFahrenheit() {
        FieldOptions f = copy();

        f.units = "\"F\"";
        f.scale = this.scale * 9.0 / 5.0;
        f.offset = this.offset + 32.0 / f.scale;
        f.min = this.min * 9.0 / 5.0 + 32.0;
        f.max = this.max * 9.0 / 5.0 + 32.0;
        // digits unchanged

        return f;
    }

    // Produce a deep copy of this object
    public FieldOptions copy() {
        FieldOptions other = new FieldOptions();

        other.min = this.min;
        other.max = this.max;
        other.scale = this.scale;
        other.offset = this.offset;
        other.digits = this.digits;
        other.units = this.units;
        other.comment = this.comment;

        return other;
    }

    public static String tryRound(double value) {
        long longVal = Math.round(value);

        // If the rounded value can exactly represent this float, then print as an integer
        if (value == longVal) {
            return Long.toString(longVal);
        } else {
            return Double.toString(value);
        }
    }

    public void printTsFormat(PrintStream ps) {
        ps.print(units);
        ps.print(", ");
        ps.print(tryRound(scale));
        ps.print(", ");
        ps.print(tryRound(offset));
        ps.print(", ");
        ps.print(tryRound(min));
        ps.print(", ");
        ps.print(tryRound(max));
        ps.print(", ");
        ps.print(digits);
    }
}
