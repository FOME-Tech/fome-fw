package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.layout.StructNamePrefixer;
import com.rusefi.newparse.parsing.Struct;
import com.rusefi.util.LazyOutputStream;

import java.io.IOException;
import java.io.PrintStream;

public class SdLogWriter {
    private final PrintStream ps;

    public SdLogWriter(String outputFile) throws IOException {
        this(new PrintStreamAlwaysUnix(new LazyOutputStream(outputFile)));
    }

    public SdLogWriter(PrintStream ps) {
        this.ps = ps;

        ps.println("static constexpr LogField fields[] = {");
        ps.println("\t{packedTime, GAUGE_NAME_TIME, \"sec\", 0},");
    }

    public void endFile() {
        ps.println("};");
        ps.close();
    }

    // baseOffset is where this struct starts within the whole output channel space, so each field's
    // absolute offset is baseOffset + its offset within the struct.
    public void writeSdLogs(ParseState parser, int baseOffset, String category) {
        // Assume the last struct is the one we want...
        Struct s = parser.getStructs().get(parser.getStructs().size() - 1);

        StructLayout sl = new StructLayout(0, "root", s);

        SdLogVisitor v = new SdLogVisitor(category);
        StructNamePrefixer prefixer = new StructNamePrefixer('.');

        v.visit(sl, ps, prefixer, baseOffset, new int[0]);
    }
}
