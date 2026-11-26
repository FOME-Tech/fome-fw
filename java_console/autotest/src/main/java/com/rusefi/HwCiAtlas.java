package com.rusefi;

import com.rusefi.common.MiscTest;

public class HwCiAtlas {
    public static void main(String[] args) {
        CmdJUnitRunner.runHardwareTestAndExit(new Class[] {
                MiscTest.class,
        });
    }
}
