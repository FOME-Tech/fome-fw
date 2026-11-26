package com.rusefi.proteus;

import com.opensr5.ConfigurationImage;
import com.rusefi.Rig;
import com.rusefi.config.Field;
import com.rusefi.config.generated.Fields;

/**
 * Signal sources for the rig's two frequency loops: GPPWM 1 on loop A (Ign 9 -> Digital 3) and GPPWM 2 on loop B
 * (Ign 8 -> Digital 4), at different frequencies so a test can tell which loop an input is on.
 *
 * GPPWM pins and frequencies are only read at boot on master, so burn and reboot after writing these.
 */
class FrequencyLoops {
    static final int LOOP_A_HZ = 100;
    static final int LOOP_B_HZ = 150;

    /**
     * Fixed duty on both loops: the table axes read Aux Temp 1, which the rig doesn't have, so GPPWM uses dutyIfError
     */
    static void writeSources(ConfigurationImage image, int dutyPercent) {
        writeChannel(image, Fields.GPPWM1_PIN, Fields.GPPWM1_PWMFREQUENCY, Fields.GPPWM1_DUTYIFERROR,
                Fields.GPPWM1_LOADAXIS, Fields.GPPWM1_RPMAXIS, Rig.FREQUENCY_A_OUT, LOOP_A_HZ, dutyPercent);
        writeChannel(image, Fields.GPPWM2_PIN, Fields.GPPWM2_PWMFREQUENCY, Fields.GPPWM2_DUTYIFERROR,
                Fields.GPPWM2_LOADAXIS, Fields.GPPWM2_RPMAXIS, Rig.FREQUENCY_B_OUT, LOOP_B_HZ, dutyPercent);
    }

    private static void writeChannel(ConfigurationImage image, Field pin, Field frequency, Field duty, Field loadAxis,
                                     Field rpmAxis, String output, int hz, int dutyPercent) {
        pin.setValue(image, pin.enumIndex(output));
        frequency.setValue(image, hz);
        duty.setValue(image, dutyPercent);
        loadAxis.setValue(image, loadAxis.enumIndex("Aux Temp 1"));
        rpmAxis.setValue(image, rpmAxis.enumIndex("Aux Temp 1"));
    }
}
