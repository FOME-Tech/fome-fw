package com.rusefi;

import com.rusefi.common.ConfigFieldsTest;
import com.rusefi.common.MiscTest;
import com.rusefi.proteus.ProteusAnalogTest;
import com.rusefi.proteus.RigPresetTest;

public class HwCiF4Proteus {
    public static void main(String[] args) {
        CmdJUnitRunner.runHardwareTestAndExit(new Class[]{
                MiscTest.class,
                ConfigFieldsTest.class,
                RigPresetTest.class,
                ProteusAnalogTest.class,
        });
    }
}
