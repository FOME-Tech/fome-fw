package com.rusefi.newparse.layout;

import com.rusefi.ConfigDefinition;
import com.rusefi.newparse.outputs.TsMetadata;
import com.rusefi.newparse.parsing.FieldOptions;
import com.rusefi.newparse.parsing.ScalarField;
import com.rusefi.newparse.parsing.Type;

import java.io.PrintStream;

public class ScalarLayout extends Layout {
    public final String name;
    private final Type type;
    private final FieldOptions options;
    private final boolean autoscale;

    public ScalarLayout(ScalarField field) {
        this.name = field.name;
        this.options = field.options;
        this.type = field.type;
        this.autoscale = field.autoscale;
    }

    @Override
    public int getSize() {
        return this.type.size;
    }

    @Override
    public String toString() {
        return "Scalar " + type.cType + " " + super.toString();
    }

    private void printBeforeArrayLength(PrintStream ps, TsMetadata meta, StructNamePrefixer prefixer, String fieldType, int offsetAdd) {
        String name = prefixer.get(this.name);
        ps.print(name);
        ps.print(" = " + fieldType + ", ");
        ps.print(this.type.tsType);
        ps.print(", ");
        ps.print(this.offset + offsetAdd);
        ps.print(", ");

        meta.addComment(name, this.options.comment);
    }

    private void printAfterArrayLength(PrintStream ps) {
        options.printTsFormat(ps);

        ps.println();
    }

    @Override
    protected void writeTunerstudioLayout(PrintStream ps, TsMetadata meta, StructNamePrefixer prefixer, int offsetAdd, int[] arrayLength) {
        if (arrayLength[0] == 0) {
            // Skip zero length arrays, they may be used for dynamic padding but TS doesn't like them
            return;
        } else if (arrayLength[0] == 1) {
            // For 1-length arrays, emit as a plain scalar instead
            writeTunerstudioLayout(ps, meta, prefixer, offsetAdd);
            return;
        }

        printBeforeArrayLength(ps, meta, prefixer, "array", offsetAdd);

        ps.print("[");
        ps.print(arrayLength[0]);

        for (int i = 1; i < arrayLength.length; i++) {
            if (arrayLength[i] == 1) {
                continue;
            }

            ps.print('x');
            ps.print(arrayLength[i]);
        }

        ps.print("], ");

        printAfterArrayLength(ps);
    }

    @Override
    protected void writeTunerstudioLayout(PrintStream ps, TsMetadata meta, StructNamePrefixer prefixer, int offsetAdd) {
        printBeforeArrayLength(ps, meta, prefixer, "scalar", offsetAdd);
        printAfterArrayLength(ps);
    }

    private String makeScaleString() {
        double scale = this.options.scale;

        long mul, div;

        if (scale < 1) {
            mul = Math.round(1 / scale);
            div = 1;
        } else {
            mul = 1;
            div = Math.round(scale);
        }

        double actualScale = (double)mul / div;

        if (mul < 1 || div < 1 || (Math.abs(scale - actualScale) < 0.0001)) {
            throw new RuntimeException("assertion failure: scale string generation failure for " + this.name + " mul " + mul + " div " + div);
        }

        return mul + ", " + div;
    }

    @Override
    public void writeCLayout(PrintStream ps) {
        this.writeCOffsetHeader(ps, this.options.comment, this.options.units);

        String cTypeName = this.type.cType.replaceAll("^int32_t$", "int");

        if (this.autoscale) {
            cTypeName = "scaled_channel<" + cTypeName + ", " + makeScaleString() + ">";
        }

        ps.print("\t" + cTypeName + " " + this.name);

        if (ConfigDefinition.needZeroInit) {
            ps.print(" = (" + this.type.cType.replaceAll("^int32_t$", "int") + ")0");
        }

        ps.println(";");
    }

    @Override
    public void writeCLayout(PrintStream ps, int[] arrayLength) {
        this.writeCOffsetHeader(ps, this.options.comment, this.options.units);

        StringBuilder al = new StringBuilder();

        al.append(arrayLength[0]);

        for (int i = 1; i < arrayLength.length; i++) {
            al.append("][");
            al.append(arrayLength[i]);
        }

        String cTypeName = this.type.cType.replaceAll("^int32_t$", "int");

        if (this.autoscale) {
            cTypeName = "scaled_channel<" + cTypeName + ", " + makeScaleString() + ">";
        }

        ps.println("\t" + cTypeName + " " + this.name + "[" + al + "];");
    }

    @Override
    public void writeCOffsetCheck(PrintStream ps, String parentTypeName) {
        ps.print("static_assert(offsetof(");
        ps.print(parentTypeName);
        ps.print(", ");
        ps.print(this.name);
        ps.print(") == ");
        ps.print(this.offsetWithinStruct);
        ps.println(");");
    }

    private void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd, int idx) {
        String nameWithoutSpace = prefixer.get(idx > 0 ? (this.name + idx) : this.name);
        String nameWithSpace = prefixer.get(idx > 0 ? (this.name + " " + idx) : this.name);

        ps.print(nameWithoutSpace);
        //ps.print(" = " + fieldType + ", ");
        ps.print(" = scalar, ");
        ps.print(this.type.tsType);
        ps.print(", ");
        ps.print(this.offset + offsetAdd);
        ps.print(", ");
        ps.print(this.options.units);
        ps.print(", ");
        ps.print(FieldOptions.tryRound(this.options.scale));
        ps.print(", ");
        ps.print(FieldOptions.tryRound(this.options.offset));
        ps.println();

        if (this.name.startsWith("unused")) {
            return;
        }

        psDatalog.print("entry = ");
        psDatalog.print(nameWithoutSpace);
        psDatalog.print(", \"");

        String commentWithIndex = (idx <= 0 || options.comment.isEmpty()) ? options.comment : options.comment + " " + idx;

        writeDatalogName(psDatalog, nameWithSpace, commentWithIndex);
        psDatalog.print("\", ");

        if (this.type.tsType.equals("F32") || this.options.scale != 1) {
            psDatalog.print("float,  \"%.3f\"");
        } else {
            psDatalog.print("int,    \"%d\"");
        }

        psDatalog.println();
    }

    @Override
    protected void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd) {
        writeOutputChannelLayout(ps, psDatalog, prefixer, offsetAdd, -1);
    }

    @Override
    protected void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd, int[] arrayLength) {
        if (arrayLength.length != 1) {
            throw new IllegalStateException("Output channels don't support multi dimension arrays");
        }

        int elementOffset = offsetAdd;

        for (int i = 0; i < arrayLength[0]; i++) {
            writeOutputChannelLayout(ps, psDatalog, prefixer, elementOffset, i + 1);
            elementOffset += type.size;
        }
    }

    @Override
    protected void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String sourceName) {
        writeSdLogLayout(ps, prefixer, "", "", sourceName);
    }

    @Override
    protected void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String sourceName, int[] arrayLength) {
        if (arrayLength.length != 1) {
            throw new IllegalStateException("Output channels don't support multi dimension arrays");
        }

        for (int i = 0; i < arrayLength[0]; i++) {
            writeSdLogLayout(ps, prefixer, "[" + i + "]", " " + (i + 1), sourceName);
        }
    }

    private void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String arraySub, String commentSuffix, String sourceName) {
        if (this.name.startsWith("unused")) {
            return;
        }
        
        // {engine->outputChannels.mafMeasured, "MAF", "kg/h", 1},

        final String prefixedName = prefixer.get(this.name);

        ps.print("\t{");
        ps.print(sourceName);
        ps.print(prefixedName);
        ps.print(arraySub);
        ps.print(", \"");

        String comment = this.options.comment;

        // default to name in case of no comment
        if (comment == null || comment.length() == 0) {
            comment = prefixedName;
        }

        ps.print(comment);
        ps.print(commentSuffix);
        ps.print("\", ");
        ps.print(this.options.units);
        ps.print(", ");
        ps.print(this.options.digits);
        ps.println("},");
    }
}
