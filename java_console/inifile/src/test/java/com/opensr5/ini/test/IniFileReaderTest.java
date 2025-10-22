package com.opensr5.ini.test;

import com.opensr5.ini.*;
import com.opensr5.ini.field.EnumIniField;
import com.opensr5.ini.field.IniField;
import org.jetbrains.annotations.NotNull;
import org.junit.Test;

import java.io.ByteArrayInputStream;

import static org.junit.Assert.*;

/**
 * Andrey Belomutskiy, (c) 2013-2020
 * 3/1/2017
 */
public class IniFileReaderTest {
    private static final String PAGE_READ = "    pageReadCommand     = \"X\",       \"X\",     \"X\"\n\n\n\n";
    private static final String SIGNATURE_UNIT_TEST = "  signature      = \"unit test\"\n";
    private static final double EPS = 0.00001;

    @Test
    public void parseExpressions() {
        assertEquals(0.1, IniField.parseDouble("{1/10}"), EPS);
        assertEquals(0.1, IniField.parseDouble("{0.1}"), EPS);
        assertEquals(0.1, IniField.parseDouble("0.1"), EPS);
    }

    @Test
    public void testSplitWithEmptyUnits() {
        {
            String[] s = IniFileReader.splitTokens("\tverboseCanBaseAddress\t\t\t= \"\", 1");
            assertEquals(3, s.length);
            assertEquals("", s[1]);
        }
        {
            String[] s = IniFileReader.splitTokens("\tverboseCanBaseAddress\t\t\t= scalar, U32,\t756,\t\"\", 1, 0, 0, 536870911, 0");
            assertEquals(10, s.length);
        }
    }

    @Test
    public void testSplit() {
        {
            String[] s = IniFileReader.splitTokens("1");
            assertEquals(s.length, 1);
            assertEquals("1", s[0]);
        }
        {
            String[] s = IniFileReader.splitTokens("hello");
            assertEquals(s.length, 1);
            assertEquals("hello", s[0]);
        }
        {
            String[] s = IniFileReader.splitTokens("\"hello\"");
            assertEquals(s.length, 1);
            assertEquals("hello", s[0]);
        }
        {
            String[] s = IniFileReader.splitTokens("\"hello\",\"w\"");
            assertEquals(s.length, 2);
            assertEquals("hello", s[0]);
            assertEquals("w", s[1]);
        }
    }

    @Test
    public void testQuotedTokens() {
        {
            String[] result = IniFileReader.splitTokens("\"hel  lo\"");
            assertEquals(result.length, 1);
            assertEquals("hel  lo", result[0]);
        }
    }

    @Test
    public void testRealLine() {
        String[] result = IniFileReader.splitTokens("\tdialog = engineChars,\t\"Base Engine Settings\"");
        assertEquals(result.length, 3);

        assertEquals("dialog", result[0]);
        assertEquals("engineChars", result[1]);
        assertEquals("Base Engine Settings", result[2]);
    }

    @Test
    public void testTotalPagesSize() {
        String string = "   nPages              = 3\n" +
                SIGNATURE_UNIT_TEST +
                PAGE_READ +
                "   pageSize            = 288,   64,     288\n";


        IniFileMetaInfo meta = new IniFileMetaInfo(fromString(string));

        assertEquals(3, meta.getnPages());
        assertEquals(IniFileMetaInfo.DEFAULT_BLOCKING_FACTOR, meta.getBlockingFactor());
        assertEquals(640, meta.getTotalSize());
        assertEquals("unit test", meta.getSignature());

        assertEquals(64, meta.getPageSize(1));
        assertEquals("X", meta.getPageReadCommand(1));
    }

    @NotNull
    private RawIniFile fromString(String string) {
        return IniFileReader.read(new ByteArrayInputStream(string.getBytes()));
    }

    @Test
    public void testSetBits() {
        assertEquals(0xFE, EnumIniField.setBitRange(0xFF, 0, 0, 1));
        assertEquals(0xF0, EnumIniField.setBitRange(0xFF, 0, 0, 4));
        assertEquals(0x0F, EnumIniField.setBitRange(0xFF, 0, 4, 4));

        assertEquals(0xff, EnumIniField.setBitRange(0xFF, 3, 0, 2));
        assertEquals(0xF3, EnumIniField.setBitRange(0xFF, 3, 0, 4));
        assertEquals(0x3F, EnumIniField.setBitRange(0xFF, 3, 4, 4));

        assertEquals(0x400, EnumIniField.setBitRange(0, 1, 10, 1));
    }

    @Test
    public void testBitLogic() {
        assertEquals(4, EnumIniField.getBitRange(4, 0, 8));
        assertEquals(4, EnumIniField.getBitRange(4, 0, 3));
        assertEquals(0, EnumIniField.getBitRange(4, 0, 2));
        assertEquals(3, EnumIniField.getBitRange(7, 0, 2));

        assertTrue(EnumIniField.getBit(0xff, 0));

        assertTrue(EnumIniField.getBit(0xf0, 4));
        assertEquals(2, EnumIniField.getBitRange(0xf0, 3, 2));
    }
}
