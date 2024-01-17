package com.rusefi.test.newParse;

import com.rusefi.RusefiParseErrorStrategy;
import com.rusefi.newparse.ParseState;
import com.rusefi.newparse.outputs.CStructWriter;
import com.rusefi.newparse.outputs.OutputChannelWriter;
import com.rusefi.newparse.outputs.PrintStreamAlwaysUnix;
import com.rusefi.newparse.outputs.TsWriter;

import java.io.*;
import java.nio.charset.StandardCharsets;

public class NewParseHelper {
    public static ParseState parse(String input) {
        ParseState parseState = new ParseState();
        RusefiParseErrorStrategy.parseDefinitionString(parseState.getListener(), input);
        return parseState;
    }

    public static String parseToTs(String input) throws IOException {
        ParseState state = parse(input);

        TsWriter writer = new TsWriter();

        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        final String utf8 = StandardCharsets.UTF_8.name();

        PrintStream ps = new PrintStreamAlwaysUnix(baos, true, utf8);

        writer.writeLayoutAndComments(state, ps);

        return baos.toString(utf8);
    }

    public static String parseToOutputChannels(String input) throws IOException {
        ParseState state = parse(input);

        final String utf8 = StandardCharsets.UTF_8.name();

        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        ByteArrayOutputStream baos2 = new ByteArrayOutputStream();

        PrintStream ps = new PrintStreamAlwaysUnix(baos, true, utf8);
        PrintStream ps2 = new PrintStreamAlwaysUnix(baos2, true, utf8);

        OutputChannelWriter writer = new OutputChannelWriter(ps, ps2);
        writer.writeOutputChannels(state, null);

        return baos.toString(utf8);
    }

    public static String parseToC(String input) throws IOException {
        ParseState state = parse(input);

        CStructWriter writer = new CStructWriter();

        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        final String utf8 = StandardCharsets.UTF_8.name();

        PrintStream ps = new PrintStreamAlwaysUnix(baos, true, utf8);

        writer.writeCStructs(state, ps);

        return baos.toString(utf8);
    }
}
