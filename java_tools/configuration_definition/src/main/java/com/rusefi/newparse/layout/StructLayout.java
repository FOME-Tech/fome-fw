package com.rusefi.newparse.layout;

import com.rusefi.newparse.outputs.TsMetadata;
import com.rusefi.newparse.parsing.*;

import java.io.PrintStream;
import java.util.ArrayList;
import java.util.List;

public class StructLayout extends Layout {
    /*private*/public final List<Layout> children = new ArrayList<>();

    public final String typeName;
    private final String name;
    private final Boolean noPrefix;
    private final int size;

    private static int getAlignedOffset(int offset, int alignment) {
        // Align each element to its own size
        if ((offset % alignment) != 0) {
            return offset + alignment - (offset % alignment);
        } else {
            return offset;
        }
    }

    int padOffsetWithUnused(int offset, int align) {
        int alignedOffset = getAlignedOffset(offset, align);

        int needsUnused = alignedOffset - offset;

        if (needsUnused > 0) {
            UnusedLayout ul = new UnusedLayout(needsUnused);
            ul.setOffset(offset);
            ul.setOffsetWithinStruct(offset - this.offset);
            children.add(ul);
            return alignedOffset;
        }

        return offset;
    }

    public StructLayout(int offset, String name, Struct parsedStruct) {
        setOffset(offset);

        this.typeName = parsedStruct.name;
        this.name = name;

        // TODO: should comment be used?
        String comment = parsedStruct.comment;
        this.noPrefix = parsedStruct.noPrefix;

        int initialOffest = offset;

        for (Field f : parsedStruct.fields) {
            if (f instanceof ArrayField) {
                ArrayField asf = (ArrayField)f;

                // If not a scalar, you must iterate
                assert(asf.prototype instanceof ScalarField || asf.iterate);

                if (asf.iterate) {
                    if (asf.prototype instanceof StructField) {
                        // Struct: special case of a struct array
                        offset = addItem(offset, new ArrayIterateStructLayout((StructField)asf.prototype, asf.length));
                    } else {
                        // array of scalars (or enums)
                        offset = addItem(offset, new ArrayIterateScalarLayout(asf.prototype, asf.length));
                    }
                } else /* not iterate */ {
                    // If not a scalar, you must iterate
                    assert(asf.prototype instanceof ScalarField);

                    ScalarField prototype = (ScalarField)asf.prototype;
                    offset = addItem(offset, new ArrayLayout(prototype, asf.length));
                }
            } else {
                offset = addItem(offset, f);
            }
        }

        // Structs are always a multiple of 4 bytes long, pad the end appropriately
        offset = padOffsetWithUnused(offset, 4);

        size = offset - initialOffest;
    }

    private int addItem(int offset, Field f) {
        if (f instanceof StructField) {
            // Special case for structs - we have to compute base offset first
            StructField sf = (StructField) f;

            return addStruct(offset, sf.struct, sf.name);
        }

        Layout l;
        if (f instanceof ScalarField) {
            l = new ScalarLayout((ScalarField)f);
        } else if (f instanceof EnumField) {
            l = new EnumLayout((EnumField)f);
        } else if (f instanceof UnusedField) {
            l = new UnusedLayout((UnusedField) f);
        } else if (f instanceof BitGroup) {
            l = new BitGroupLayout((BitGroup) f);
        } else if (f instanceof StringField) {
            l = new StringLayout((StringField) f);
        } else if (f instanceof Union) {
            l = new UnionLayout((Union)f);
        } else {
            throw new RuntimeException("unexpected field type during layout");
        }

        return addItem(offset, l);
    }

    private int addItem(int offset, Layout l) {
        // Slide the offset up by the required alignment of this element
        offset = padOffsetWithUnused(offset, l.getAlignment());

        // place the element
        l.setOffset(offset);
        l.setOffsetWithinStruct(offset - this.offset);
        children.add(l);

        return offset + l.getSize();
    }

    private int addStruct(int offset, Struct struct, String name) {
        offset = padOffsetWithUnused(offset, 4);

        // Recurse and build this new struct
        StructLayout sl = new StructLayout(offset, name, struct);

        sl.setOffsetWithinStruct(offset - this.offset);
        this.children.add(sl);

        // Update offset with the struct size - it's guaranteed to be a multiple of 4 bytes
        int structSize = sl.getSize();
        return offset + structSize;
    }

    @Override
    public int getSize() {
        return this.size;
    }

    @Override
    public int getAlignment() {
        // All structs should be aligned on a 4 byte boundary
        return 4;
    }

    @Override
    public String toString() {
        return "Struct " + this.typeName + " " + super.toString();
    }


    @Override
    protected void writeTunerstudioLayout(PrintStream ps, TsMetadata meta, StructNamePrefixer prefixer, int offsetAdd) {
        if (!this.noPrefix) {
            prefixer.push(this.name);
        }

        // print all children in sequence
        this.children.forEach(c -> c.writeTunerstudioLayout(ps, meta, prefixer, offsetAdd));

        if (!this.noPrefix) {
            prefixer.pop();
        }
    }

    @Override
    public void writeCLayout(PrintStream ps) {
        this.writeCOffsetHeader(ps, null, null);
        ps.println("\t" + this.typeName + " " + this.name + ";");
    }

    @Override
    public void writeCLayout(PrintStream ps, int[] arrayLength) {
        this.writeCOffsetHeader(ps, null, null);
        ps.println("\t" + this.typeName + " " + this.name + "[" + arrayLength[0] + "];");
    }

    public void writeCLayoutRoot(PrintStream ps) {
        ps.println("struct " + this.typeName + " {");

        this.children.forEach(c -> c.writeCLayout(ps));

        ps.println("};");
        ps.println("static_assert(sizeof(" + this.typeName + ") == " + getSize() + ");");

        // Emit assertions to check that the offset of each child is correct according to the C++ compiler
        this.children.forEach(c -> c.writeCOffsetCheck(ps, this.typeName));

        ps.println();
    }

    private void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd, String name) {
        if (!this.noPrefix) {
            prefixer.push(name);
        }

        this.children.forEach(c -> c.writeOutputChannelLayout(ps, psDatalog, prefixer, offsetAdd));

        if (!this.noPrefix) {
            prefixer.pop();
        }
    }

    @Override
    protected void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd) {
        writeOutputChannelLayout(ps, psDatalog, prefixer, offsetAdd, this.name);
    }

    @Override
    protected void writeOutputChannelLayout(PrintStream ps, PrintStream psDatalog, StructNamePrefixer prefixer, int offsetAdd, int[] arrayLength) {
        if (arrayLength.length != 1) {
            throw new IllegalStateException("Output channels don't support multi dimension arrays");
        }

        int elementOffset = offsetAdd;

        for (int i = 0; i < arrayLength[0]; i++) {
            writeOutputChannelLayout(ps, psDatalog, prefixer, elementOffset, this.name + (i + 1));
            elementOffset += this.size;
        }
    }

    private void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String sourceName, String name) {
        if (!this.noPrefix) {
            prefixer.push(name);
        }

        this.children.forEach(c -> c.writeSdLogLayout(ps, prefixer, sourceName));

        if (!this.noPrefix) {
            prefixer.pop();
        }
    }

    @Override
    protected void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String sourceName) {
        writeSdLogLayout(ps, prefixer, sourceName, this.name);
    }

    @Override
    protected void writeSdLogLayout(PrintStream ps, StructNamePrefixer prefixer, String sourceName, int[] arrayLength) {
        if (arrayLength.length != 1) {
            throw new IllegalStateException("Output channels don't support multi dimension arrays");
        }

        // TODO: This doesn't quite work, as it's unclear how to make automatic naming work properly
        // for (int i = 0; i < arrayLength[0]; i++) {
        //     writeSdLogLayout(ps, prefixer, sourceName, this.name + "[" + i + "]");
        // }
    }
}
