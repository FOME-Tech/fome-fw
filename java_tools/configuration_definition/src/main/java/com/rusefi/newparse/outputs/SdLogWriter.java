package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.parsing.Struct;

import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.PrintStream;

public class SdLogWriter {
    private final PrintStream ps;

    public SdLogWriter(String outputFile) throws FileNotFoundException {
        this.ps = new PrintStreamAlwaysUnix(new FileOutputStream(outputFile));
    }

    public SdLogWriter(PrintStream ps) {
        this.ps = ps;
    }

    public void writeSdLogs(ParseState parser) {
        for (Struct s : parser.getStructs()) {
            StructLayout sl = new StructLayout(0, "root", s);
            sl.writeSdLogLayout(ps);
        }
    }
}
