package com.rusefi;

import com.rusefi.atlas.OnboardSensorsTest;
import com.rusefi.common.ConfigFieldsTest;
import com.rusefi.common.MiscTest;
import com.rusefi.proteus.FlexLoopTest;
import com.rusefi.proteus.FrequencyLoopTest;
import com.rusefi.proteus.HardwareTriggerTest;
import com.rusefi.proteus.OutputFrequencyTest;
import com.rusefi.proteus.RigPresetTest;

public class HwCiAtlas {
    public static void main(String[] args) {
        CmdJUnitRunner.runHardwareTestAndExit(new Class[] {
                MiscTest.class,
                ConfigFieldsTest.class,
                RigPresetTest.class,
                FlexLoopTest.class,
                OnboardSensorsTest.class,
                FrequencyLoopTest.class,
                OutputFrequencyTest.class,
                HardwareTriggerTest.class,
        });
    }
}
