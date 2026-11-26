package com.rusefi;

/**
 * Connector roles on the HW CI loopback rig, by their TunerStudio pin names (see hw_ci_plan.md, "Wiring").
 *
 * Proteus and Atlas share these names on purpose: Atlas is made to drop in where a Proteus goes, hardware and
 * tune compatible. So one set of roles serves both, and each board's generated Fields map the names to its pins.
 * Use with {@link com.rusefi.functional_tests.EcuTestHelper#setField(com.rusefi.config.Field, String)}.
 */
public class Rig {
    /**
     * Drives the RC filter into {@link #ANALOG_LOOP_IN}: duty shows up as voltage
     */
    public static final String ANALOG_LOOP_OUT = "Ign 10";
    public static final String ANALOG_LOOP_IN = "Analog Volt 2";

    public static final String CRANK_OUT = "Ign 11";
    public static final String CRANK_IN = "Digital 1";

    public static final String CAM_OUT = "Ign 12";
    public static final String CAM_IN = "Digital 2";

    public static final String FREQUENCY_A_OUT = "Ign 9";
    public static final String FREQUENCY_A_IN = "Digital 3";

    public static final String FREQUENCY_B_OUT = "Ign 8";
    public static final String FREQUENCY_B_IN = "Digital 5";

    public static final String NONE = "NONE";
}
