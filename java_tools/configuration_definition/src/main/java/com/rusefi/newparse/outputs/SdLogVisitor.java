package com.rusefi.newparse.outputs;

import com.rusefi.newparse.layout.*;

import java.io.PrintStream;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public class SdLogVisitor extends OutputChannelVisitorBase {
    private int totalRecordLength = 0;

    public int getTotalRecordLength() {
        return totalRecordLength;
    }

    private int numFields = 0;

    private final List<ByteBuffer> fieldBuffers = new ArrayList<>();

    public List<ByteBuffer> getFieldBuffers() {
        return fieldBuffers;
    }

    public int getNumFields() {
        return numFields;
    }

    public class BitGroupPatchup {
        final ByteBuffer buffer;

        final List<String> bitNames;

        public BitGroupPatchup(ByteBuffer buffer, List<String> bitNames) {
            this.buffer = buffer;
            this.bitNames = bitNames;
        }

        public void setStartOffset(int offset) {
            buffer.putInt(47, offset);
        }
    }

    private final List<BitGroupPatchup> bitGroupPatchups = new ArrayList<>();

    public List<BitGroupPatchup> getBitGroupPatchups() {
        return bitGroupPatchups;
    }

    private static void writeString(ByteBuffer b, int start, int maxBytes, String s) {
        int stringBytes = Math.min(maxBytes, s.length());
        int i = 0;
        for(; i < stringBytes; i++) {
            b.put(start + i, (byte)s.charAt(i));
        }

        // pad with zeroes
        for (; i < maxBytes; i++) {
            b.put(start + i, (byte)0);
        }
    }

    private void writeScalarEntry(int type, int size, String name, String unit, double scale, double offset, int digits) {
        totalRecordLength += size;
        numFields++;

        ByteBuffer buffer = ByteBuffer.allocate(89);

        // Offset 0, length 1 = type
        buffer.put(0, (byte) type);

        // Offset 1, length 34 = name
        writeString(buffer, 1, 34, name);

        // Offset 35, length 10 = units
        writeString(buffer, 35, 10, unit);

        // Offset 45, length 1 = Display style
        // value 0 -> floating point number
        buffer.put(45, (byte) 0);

        // Offset 46, length 4 = Scale
        buffer.putFloat(46, (float) scale);

        // Offset 50, length 4 = shift before scaling (always 0)
        buffer.putFloat(50, (float) offset);

        // Offset 54, size 1 = digits to display
        buffer.put(54, (byte) digits);

        // Offset 55, length 34 = optional category string (not used)

        fieldBuffers.add(buffer);
    }

    private ByteBuffer writeBitGroupEntry(int bytes, int numBitFields) {
        totalRecordLength += bytes;
        numFields++;

        ByteBuffer buffer = ByteBuffer.allocate(89);

        int type;
        switch (bytes) {
            case 1: type = 0x10; break;
            case 2: type = 0x11; break;
            case 4: type = 0x12; break;
            default: throw new UnsupportedOperationException("Invalid bit group size: " + bytes);
        }

        // Offset 0, length 1 = type (10 = U08, 11 = U16, 12 = U32)
        buffer.put(0, (byte)type);

        // Offset 1, length 34 = name: if empty string, only individual bits are shown (?)

        // Offset 35, length 10 = units (what does this even mean for a bit group?

        // Offset 45, length 1 = display style, 2=bits
        buffer.put(45, (byte)2);

        // Offset 46, length 1 = bit field style (?)
        buffer.put(45, (byte)2);

        // Offset 47, length 4 = Index in file of bit field names start
        // This field is written later by BitGroupPatchup

        // Offset 51, length 1 = number of bits in this bit field
        buffer.put(51, (byte)numBitFields);

        // Offset 55, length 34 = optional category string (not used)

        fieldBuffers.add(buffer);

        return buffer;
    }

    @Override
    public void visit(EnumLayout e, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        // Output an enum as a scalar, since there's no TS support for enum output channels
        // Write the datalog entry as an integer, since there's no support for enums.
        writeScalarEntry(
                0,
                e.type.size,
                buildDatalogName(e.name, e.options.comment),
                "",
                1,
                0,
                0
        );
    }

    private void visit(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int idx) {
        if (scalar.name.startsWith("unused")) {
            return;
        }

        String nameWithSpace = prefixer.get(idx > 0 ? (scalar.name + " " + idx) : scalar.name);
        String commentWithIndex = (idx <= 0 || scalar.options.comment.isEmpty()) ? scalar.options.comment : scalar.options.comment + " " + idx;

        writeScalarEntry(
                0,
                scalar.type.size,
                buildDatalogName(nameWithSpace, commentWithIndex),
                scalar.options.units,
                scalar.options.scale,
                scalar.options.offset,
                scalar.options.digits
        );
    }

    @Override
    public void visit(ScalarLayout scalar, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        if (arrayDims.length == 0) {
            visit(scalar, ps, prefixer, offsetAdd, -1);
        } else if (arrayDims.length == 1) {
            int elementOffset = offsetAdd;

            for (int i = 0; i < arrayDims[0]; i++) {
                visit(scalar, ps, prefixer, elementOffset, i + 1);
                elementOffset += scalar.type.size;
            }
        } else {
            throw new IllegalStateException("Output channels don't support multi dimension arrays");
        }
    }

    @Override
    public void visit(BitGroupLayout b, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        int numBits = 0;

        final List<String> bitFieldNames = new ArrayList<>();

        for (int i = 0; i < b.bits.size(); i++) {
            BitGroupLayout.BitLayout bit = b.bits.get(i);

            numBits++;

            if (bit.name.startsWith("unused")) {
                bitFieldNames.add("INVALID");
            } else {
                bitFieldNames.add(buildDatalogName(prefixer.get(bit.name), bit.comment));
            }
        }

        ByteBuffer buffer = writeBitGroupEntry(4, numBits);

        bitGroupPatchups.add(new BitGroupPatchup(buffer, bitFieldNames));
    }

    @Override
    public void visit(UnusedLayout struct, PrintStream ps, StructNamePrefixer prefixer, int offsetAdd, int[] arrayDims) {
        // Pad unused using empty bit groups
        if (struct.size == 3) {
            writeBitGroupEntry(1, 0);
            writeBitGroupEntry(2, 0);
        } else {
            writeBitGroupEntry(struct.size, 0);
        }
    }
}
