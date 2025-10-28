package com.rusefi.tracing;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import static com.rusefi.tracing.EnumNames.TypeNames;

public class Entry {
    private final String name;
    private final Phase phase;
    private final int isr;
    private final int thread;
    private long timestampNs;

    public Entry(String name, Phase phase, long timestampNs, int isr, int thread) {
        this.name = name;
        this.phase = phase;
        this.isr = isr;
        this.thread = thread;
        this.timestampNs = timestampNs;
    }

    private static void AppendKeyValuePair(StringBuilder sb, String x, String y) {
        sb.append('"');
        sb.append(x);
        sb.append("\":\"");
        sb.append(y);
        sb.append('"');
    }

    private static void AppendKeyValuePair(StringBuilder sb, String x, int y) {
        sb.append('"');
        sb.append(x);
        sb.append("\":");
        sb.append(y);
    }

    private static void AppendKeyValuePair(StringBuilder sb, String x, double y) {
        sb.append('"');
        sb.append(x);
        sb.append("\":");
        sb.append(y);
    }

    public static int readInt(DataInputStream in) throws IOException {
        int ch1 = in.read();
        int ch2 = in.read();
        int ch3 = in.read();
        int ch4 = in.read();
        if ((ch1 | ch2 | ch3 | ch4) < 0)
            throw new EOFException();
        return ((ch4 << 24) + (ch3 << 16) + (ch2 << 8) + ch1);
    }

    public static List<Entry> parseBuffer(byte[] packet) {
        List<Entry> result = new ArrayList<>();

        try {
            DataInputStream is = new DataInputStream(new ByteArrayInputStream(packet));
            is.readByte(); // skip TS result code
            long firstTimeStamp = 0;
            for (int i = 0; i < packet.length - 1; i += 8) {
                byte type = is.readByte();
                byte phase = is.readByte();
                byte isr = is.readByte();
                byte thread = is.readByte();

                long timestampNs = readInt(is);
                if (i == 0) {
                    firstTimeStamp = timestampNs;
                } else {
                    if (timestampNs < firstTimeStamp) {
                        System.out.println("Dropping the remainder of the packet at " + i + " due to "
                                + timestampNs + " below " + firstTimeStamp);
                        break;
                    }
                }

                String name;
                if (type == 1) {
                    name = "ISR: " + thread;
                }
                else
                {
                    name = TypeNames[type];
                }

                Entry e = new Entry(name, Phase.decode(phase), timestampNs, isr, thread);
                result.add(e);
            }
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
        return result;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder();

        sb.append("{");
        AppendKeyValuePair(sb, "name", name);

        sb.append(",");
        AppendKeyValuePair(sb, "ph", phase.toString());
        sb.append(",");
        AppendKeyValuePair(sb, "tid", thread);
        sb.append(",");
        AppendKeyValuePair(sb, "pid", isr);
        sb.append(",");
        double timestampUs = 1e-3 * timestampNs;
        AppendKeyValuePair(sb, "ts", timestampUs);
        sb.append("}");

        return sb.toString();
    }
}
