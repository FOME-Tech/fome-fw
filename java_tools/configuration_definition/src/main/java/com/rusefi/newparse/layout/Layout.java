package com.rusefi.newparse.layout;

import com.rusefi.newparse.outputs.TsMetadata;

import java.io.PrintStream;

public abstract class Layout {
    public int offset = -1;
    public int offsetWithinStruct = -1;

    public abstract int getSize();
    public int getAlignment() {
        // Default to size
        return this.getSize();
    }

    public void setOffset(int offset) {
        this.offset = offset;
    }

    public void setOffsetWithinStruct(int offset) {
        offsetWithinStruct = offset;
    }

    @Override
    public String toString() {
        return "offset = " + offset + " size = " + this.getSize();
    }

    public final void writeTunerstudioLayout(PrintStream ps, TsMetadata meta) {
        writeTunerstudioLayout(ps, meta, new StructNamePrefixer('_'), 0);
    }

    protected void writeTunerstudioLayout(PrintStream ps, TsMetadata meta, StructNamePrefixer prefixer, int offsetAdd) {}

    protected void writeTunerstudioLayout(PrintStream ps, TsMetadata meta, StructNamePrefixer prefixer, int offsetAdd, int[] arrayLength) {
        throw new IllegalStateException("This type can't be in an array!");
    }

    protected void writeCOffsetHeader(PrintStream ps, String comment, String units) {
        if (comment != null) {
            comment = comment.replaceAll("[+]", "");
            comment = comment.replaceAll("\\n", "\n\t// ");
            if (comment.length() == 0) {
                comment = null;
            }
        }

        if (comment != null) {
            comment = comment.replaceAll("\\\\n", "\n\t// ");

            ps.println("\t// " + comment);
        }

        if (units != null && units.length() > 2) {
            ps.println("\t// " + units.replace("\"", ""));
        }

        ps.println("\t// offset " + this.offsetWithinStruct);
    }

    public void writeCLayout(PrintStream ps) { }

    public void writeCLayout(PrintStream ps, int[] arrayLength) {
        throw new IllegalStateException("This type can't be in an array!");
    }

    public void writeCOffsetCheck(PrintStream ps, String parentTypeName) { }

    public void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, String prefix, int offsetAdd) {
        StructNamePrefixer prefixer = new StructNamePrefixer('_');

        if (prefix != null) {
            prefixer.push(prefix);
        }

        writeOutputChannelLayout(ps, psDatalog, prefixer, offsetAdd);
    }

    protected void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd) { }

    protected void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd, int[] arrayLength) {
        throw new IllegalStateException("This type can't be in an array!");
    }

    protected static void writeDatalogName(PrintStream ps, String name, String comment) {
        String text = (comment == null || comment.isEmpty()) ? name : comment;

        // Delete anything after a newline
        text = text.split("\\\\n")[0];

        ps.print(text);
    }

    public void writeSdLogLayout(PrintStream ps, String sourceName) {
        // TODO
        final String prefix = null;

        StructNamePrefixer prefixer = new StructNamePrefixer('.');

        if (prefix != null) {
            prefixer.push(prefix);
        }

        writeSdLogLayout(ps, prefixer, sourceName);
    }

    protected void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String sourceName) { }

    protected void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String sourceName, int[] arrayLength) {
        throw new IllegalStateException("This type can't be in an array!");
    }
}
