package com.rusefi.newparse.outputs;

import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.layout.StructLayout;
import com.rusefi.newparse.parsing.Struct;

import java.io.IOException;
import java.io.PrintStream;
import java.nio.file.Files;
import java.nio.file.Paths;

public class CStructWriter {
    public void writeCStructs(ParseState parser, String outputFile) throws IOException {
        writeCStructs(parser, new PrintStreamAlwaysUnix(Files.newOutputStream(Paths.get(outputFile))));
    }

    public void writeCStructs(ParseState parser, PrintStream ps) {
        ps.println(
                "#pragma once\n" +
                "#include \"rusefi_types.h\""
        );

        CStructsVisitor v = new CStructsVisitor();

        for (Struct s : parser.getStructs()) {
            StructLayout sl = new StructLayout(0, "root", s);

            v.visitRoot(sl, ps);
        }

        ps.close();
    }
}
