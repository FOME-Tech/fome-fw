package com.rusefi;

import com.rusefi.common.MiscTest;
import com.rusefi.proteus.ProteusAnalogTest;

public class HwCiAtlas {
    public static void main(String[] args) {
        // TODO: run some actual tests once an Atlas is plugged in
        CmdJUnitRunner.runHardwareTestAndExit(new Class[] {
                // MiscTest.class,
                // ProteusAnalogTest.class,
        });
    }
}
