package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;
import com.rusefi.newparse.parsing.Struct;

import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.nio.ByteBuffer;
import java.util.List;

public class SdLogWriter {
    private final PrintStream ps;

    private final SdLogVisitor visitor = new SdLogVisitor();


    public SdLogWriter(String outputFile) throws FileNotFoundException {
        this(new PrintStreamAlwaysUnix(new FileOutputStream(outputFile)));
    }

    public SdLogWriter(PrintStream ps) {
        this.ps = ps;
    }

    public void endFile() {
        ps.println("static const unsigned char binaryLogHeader[] = {");

        // magic number file identifier
        ps.println("'M', 'L', 'V', 'L', 'G', 0,");

        // File format version
        ps.println("0, 2, // File format version");

        // Timestamp - all zeroes
        ps.println("0, 0, 0, 0, // Timestamp");

        // Info data start - zero if no such data
        ps.println("0, 0, 0, 0, // Info data start");

        // Data begin index - skip to 64k to make header initialization easier
        ps.println("0, 1, 0, 0, // Data begin index");

        // Record length - number of data bytes in each log record
        int recordLength = visitor.getTotalRecordLength();
        for (int i = 1; i >= 0; i--) {
            int b = (recordLength >> 8 * i) & 0xFF;
            ps.print(b + ", ");
        }
        ps.println("// Record length: " + recordLength);

        // Number of fields
        int numFields = visitor.getNumFields();
        for (int i = 1; i >= 0; i--) {
            int b = (numFields >> 8 * i) & 0xFF;
            ps.print(b + ", ");
        }
        ps.println("// Number of fields: " + numFields);

        int bitGroupNameOffsetInFile = 24 + 89 * numFields;
        int totalBitFieldNameSize = 0;

        // Calculate sizing for bit field names as we have to patch
        // the offsets in fields before we write them
        for (SdLogVisitor.BitGroupPatchup bitGroup : visitor.getBitGroupPatchups()) {
            bitGroup.setStartOffset(bitGroupNameOffsetInFile);

            for (String name : bitGroup.bitNames) {
                int bytesWritten = name.length() + 1;
                bitGroupNameOffsetInFile += bytesWritten;
                totalBitFieldNameSize += bytesWritten;
            }
        }

        // Write all fields
        for (ByteBuffer fieldBuffer : visitor.getFieldBuffers()) {
            for (byte b : fieldBuffer.array()) {
                ps.print((b & 0xFF) + ", ");
            }

            ps.println();
        }

        // Now make a second pass through bit field names to print them
        for (SdLogVisitor.BitGroupPatchup bitGroup : visitor.getBitGroupPatchups()) {
            for (String name : bitGroup.bitNames) {
                for (int i = 0; i < name.length(); i++) {
                    ps.print((byte) name.charAt(i) + ", ");
                }

                // null terminate string
                ps.println("0, // Bit field name: " + name);
            }
        }

        ps.println("};");

        ps.println("static_assert(sizeof(binaryLogHeader) == 24 + 89 * " + numFields + " + " + totalBitFieldNameSize + ");");
        ps.println("static_assert(TS_TOTAL_OUTPUT_SIZE == " + recordLength + ");");
    }

    public void writeSdLogs(ParseState parser) {
        // Assume the last struct is the one we want...
        Struct s = parser.getStructs().get(parser.getStructs().size() - 1);

        StructLayout sl = new StructLayout(0, "root", s);

        StructNamePrefixer prefixer = new StructNamePrefixer('_');

        visitor.visit(sl, ps, prefixer, 0, new int[0]);
    }
}
