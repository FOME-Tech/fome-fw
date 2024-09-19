/*
 * @file engine_types.h
 *
 * See 'integration API' note in rusefi_enums.h
 * TODO: spell out which enums belong here and which enums belong in rusefi_enums.h for what reasons
 *
 * @date Oct 23, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#pragma once

#include "rusefi_generated.h"


/**
 * http://rusefi.com/wiki/index.php?title=Manual:Engine_Type
 */
enum class engine_type_e : uint32_t {
	DEFAULT_FRANKENSO = 0,

	MIATA_PROTEUS_TCU = 1,

	/**
	 * 1995 Dodge Neon
	 * http://rusefi.com/forum/viewtopic.php?t=360
	 */
	DODGE_NEON_1995 = 2,
	/**
	 * 1996 1.3 Ford Aspire
	 * http://rusefi.com/forum/viewtopic.php?t=375
	 */
	FORD_ASPIRE_1996 = 3,

	NISSAN_PRIMERA = 5,
	PROTEUS_HARLEY = 6,
	FORD_INLINE_6_1995 = 7,
	/**
	 * one cylinder engine
	 * 139qmb 50-90cc
	 * http://rusefi.com/forum/viewtopic.php?f=3&t=332
	 */
	GY6_139QMB = 8,

	PROTEUS_E65_6H_MAN_IN_THE_MIDDLE = 9,
	HELLEN_NA8_96 = 10,
	ET_UNUSED_66 = 66,

	TEST_ROTARY = 19,
	// Frankenso board
	FRANKENSO_MIATA_NA6_MAP = 41,
	ET_UNUSED_20 = 20,
	ET_UNUSED_21 = 21,

	FORD_ESCORT_GT = 14,

	MITSU_4G93 = 16,

	TEST_33816 = 18,


	SUBARU_2003_WRX = 22,

	/**
	 * microRusEFI used as Body Control Module BCM BCU
	 */
	MRE_BODY_CONTROL = 23,
	BMW_M73_M = 24,

	PROTEUS_LUA_DEMO = 25,

	TEST_ENGINE = 26,

	PROTEUS_GM_LS_4 = 27,

	TEST_CRANK_ENGINE = 28,

	SACHS = 29,

	PROTEUS_ANALOG_PWM_TEST = 30,

	MRE_BOARD_NEW_TEST = 31,

	VW_ABA = 32,

	HELLEN_NA6 = 34,

	CAMARO_4 = 35,

	HELLEN_128_MERCEDES_4_CYL = 36,

	MRE_SUBARU_EJ18 = 37,

	DISCOVERY_PDM = 38,

	PROTEUS_VW_B6 = 39,

	FRANKENSO_BMW_M73_F = 40,

	PROTEUS_QC_TEST_BOARD = 42,

	TOYOTA_2JZ_GTE_VVTi = 44,

	TEST_ENGINE_VVT = 45,

	DODGE_NEON_2003_CRANK = 46,

	/**
	 * proper NB2 setup, 2003 red test mule car
	 */
	FRANKENSO_MAZDA_MIATA_2003 = 47,

	// todo: add wastegate case into HW CI
	TEST_DC_WASTEGATE_DISCOVERY = 48,

	FRANKENSO_QA_ENGINE = 49,

	BMW_M73_MRE = 50,

	BMW_M73_MRE_SLAVE = 51,


	TEST_ISSUE_366_BOTH = 52,
	TEST_ISSUE_366_RISE = 53,

	/**
	 * green Hunchback race car - VVT engine on a NA body with NA return fuel lines which
	 * means different fuel pressure situation
	 */
	MAZDA_MIATA_2003_NA_RAIL = 54,

	MAZDA_MIATA_2003_BOARD_TEST = 55,

	FRANKENSO_MAZDA_MIATA_NA8 = 56,

	ETB_BENCH_ENGINE = 58,

	TLE8888_BENCH_ENGINE = 59,

	L9779_BENCH_ENGINE = 60,

	MRE_VW_B6 = 62,

	PROTEUS_BMW_M73 = 63,

	DODGE_RAM = 64,
	CITROEN_TU3JP = 65,

	MRE_M111 = 68,

	HELLEN_NB2 = 69,

	SUBARUEG33_DEFAULTS = 70,

	HELLEN_121_VAG_4_CYL = 71,
	HELLEN_121_NISSAN_6_CYL = 72,
	HELLEN_55_BMW = 73,
	HELLEN_88_BMW = 74,
	HELLEN_134_BMW = 75,
	HELLEN_154_VAG = 76,

	HELLEN_121_VAG_5_CYL = 77,
	HELLEN_121_VAG_V6_CYL = 78,
	HELLEN_121_VAG_VR6_CYL = 79,
	HELLEN_121_VAG_8_CYL = 81,

	HELLEN_NA94 = 80,

	// 82
	HELLEN_154_HYUNDAI_COUPE_BK1 = 82,
	HELLEN_NB1 = 83,
	// 84
	HELLEN_121_NISSAN_4_CYL = 84,

	HELLEN_121_NISSAN_8_CYL = 85,

	HELLEN_NB2_36 = 86,

	HELLEN_121_NISSAN_ALMERA_N16  = 87,

	HELLEN_128_MERCEDES_6_CYL = 88,

	HELLEN_128_MERCEDES_8_CYL = 89,

	PROTEUS_HONDA_K = 90,

	PROTEUS_HONDA_OBD2A = 91,

	ET_UNUSED_92 = 92,

	ET_UNUSED_93 = 93,

	HELLEN_NB1_36 = 94,

	HELLEN_154_HYUNDAI_COUPE_BK2 = 95,

	WASTEGATE_PROTEUS_TEST = 96,

	// Polygonus PnP presets
	POLYGONUS_MIATA_NA6 = 4,
	POLYGONUS_MIATA_NB1 = 11,
	POLYGONUS_MIATA_NB2 = 67,
	POLYGONUS_MIATA_MSM = 12,

	ET_UNUSED_97 = 97,
	ET_UNUSED_98 = 98,
	ET_UNUSED_17 = 17,
	ET_UNUSED_13 = 13,
	ET_UNUSED_15 = 15,
	ET_UNUSED_33 = 33,
	ET_UNUSED_43 = 43,
	ET_UNUSED_57 = 57,
	ET_UNUSED_61 = 61,

	/**
	 * this configuration has as few pins configured as possible
	 */
	MINIMAL_PINS = 99,

	TEST_100 = 100,
	TEST_101 = 101,
	TEST_102 = 102,
};

/**
 * https://rusefi.com//wiki/index.php?title=Manual:Debug_fields
 */
typedef enum __attribute__ ((__packed__)) {
	DBG_0 = 0,
	DBG_1 = 1,
	DBG_2 = 2,
	DBG_STEPPER_IDLE_CONTROL = 3,
	DBG_EL_ACCEL = 4,
	DBG_TRIGGER_COUNTERS = 5,
	DBG_6 = 6,
	DBG_7 = 7,
	DBG_8 = 8,
	DBG_9 = 9,
	DBG_10 = 10,
	DBG_11 = 11,
	DBG_12 = 12,
	DBG_13 = 13,
	DBG_SR5_PROTOCOL = 14,
	DBG_15 = 15,
	DBG_16 = 16,
	/**
	 * See also DBG_ELECTRONIC_THROTTLE_EXTRA
	 */
	DBG_ELECTRONIC_THROTTLE_PID = 17,
	DBG_EXECUTOR = 18,

	DBG_19 = 19,
	DBG_20 = 20,
	DBG_21 = 21,

	DBG_22 = 22,
	UNUSED23 = 23,
	DBG_24 = 24,
	DBG_25 = 25,
	DBG_26 = 26,
	DBG_27 = 27,
	DBG_METRICS = 28,
	DBG_29 = 29,
	DBG_ION = 30,
	DBG_TLE8888 = 31,
	DBG_32 = 32,
	DBG_33 = 33,
	DBG_34 = 34,
	DBG_35 = 35,
	DBG_36 = 36,
	DBG_37 = 37,
	DBG_38 = 38,
	DBG_ETB_AUTOTUNE = 39,
	DBG_COMPOSITE_LOG = 40,
	DBG_UNUSED41 = 41,
	DBG_UNUSED_42 = 42,
	DBG_43 = 43,
	DBG_DYNO_VIEW = 44,
	DBG_LOGIC_ANALYZER = 45,
	DBG_46 = 46,
	DBG_TCU = 47,
	DBG_LUA = 48,

} debug_mode_e;

/**
 * @see http://rusefi.com/wiki/index.php?title=Manual:Software:Trigger
 */
enum class trigger_type_e : uint32_t {
	TT_TOOTHED_WHEEL = 0,
	TT_FORD_ASPIRE = 1,
	TT_DODGE_NEON_1995 = 2,
	/**
	 * https://rusefi.com/wiki/index.php?title=Manual:Software:Trigger#Mazda_Miata_NA
	 */
	TT_MAZDA_MIATA_NA = 3,
	TT_MITSUBISHI_MESS = 4,
	TT_GM_7X = 5,
	TT_DAIHATSU = 6,
	TT_MAZDA_SOHC_4 = 7,
	/**
	 * "60/2"
	 * See also TT_ONE_PLUS_TOOTHED_WHEEL_60_2
	 */
	TT_TOOTHED_WHEEL_60_2 = 8,
	TT_TOOTHED_WHEEL_36_1 = 9,

	TT_MERCEDES_2_SEGMENT = 10,

	// todo: this really looks to be same as Miata_NA shall we remove?
	TT_MITSUBISHI_4G93 = 11,

	// cam-based
	TT_DODGE_NEON_2003_CAM = 14,

	TT_MAZDA_DOHC_1_4 = 15,

	/**
	 * "1+1" - one tooth on primary channel, one tooth on secondary channel
	 * this trigger is used only by unit tests
	 * see also TT_ONE a bit below
	 */
	TT_ONE_PLUS_ONE = 16,
	/**
	 * VVT for 2JZ
	 * three evenly spaces teeth
	 */
	TT_VVT_TOYOTA_3_TOOTH = 17,
	// just one channel with just one tooth
	TT_ONE = 18,

	TT_DODGE_RAM = 19,
	/**
	 * It looks like this is the VR shape if you have your wires flipped
	 */
	TT_60_2_VW = 20,

	TT_BENELLI_TRE = 21,

	TT_DODGE_STRATUS = 22,

	/**
	 * Subaru but also Mazda RX-8
	 * We suspect that it's VR
	 */
	TT_36_2_2_2 = 23,

	/**
	 * only the 4 tooth signal, without the 360 signal
	 * 8,2,2,2 Nissan pattern
	 */
	TT_NISSAN_SR20VE = 24,

	TT_2JZ_3_34 = 25,

	TT_ROVER_K = 26,

	TT_HONDA_CBR_600 = 28,

	TT_CHRYSLER_NGC_36_2_2 = 30,

	// skipped 3/1 with cam sensor for testing
	TT_3_1_CAM = 31,

	// crank-based in case your cam is broken
	TT_DODGE_NEON_2003_CRANK = 32,

	/**
	 * this takes care of crank sensor, VVT sensor should be configured separately
	 * for VVT simulated trigger signal we have https://github.com/rusefi/rusefi/issues/566 gap
	 * See also TT_MAZDA_MIATA_VVT_TEST
	 */
	TT_MIATA_VVT = 33,

	/**
	 * a version of NB1 with shifted CAM, useful for VVT testing & development
	 */
	TT_MAZDA_MIATA_VVT_TEST = 35,

	TT_SUBARU_7_6 = 36,

	// this one is 6 cylinder, see TT_JEEP_4_cyl for 4 cylinders
	TT_JEEP_18_2_2_2 = 37,

	TT_12_TOOTH_CRANK = 38,

	TT_DODGE_NEON_1995_ONLY_CRANK = 39,

	// Jeep XJ 2500cc 4 cylinder. See also TT_JEEP_18_2_2_2 for 6 cylinders
	TT_JEEP_4_CYL = 40,

	// magneti marelli Fiat/Lancia IAW P8 from the 90', 2.0 16 v turbo engine - Lancia Coupe
	// https://rusefi.com/forum/viewtopic.php?f=5&t=1440
	TT_FIAT_IAW_P8 = 41,

	TT_FORD_ST170 = 42,

	/**
	 * cam sensor of Mazda Miata NB2 - the VVT signal shape
	 */
	TT_VVT_MIATA_NB = 43,

	TT_RENIX_44_2_2 = 44,

	/**
	 * Same as TT_RENIX_44_2_2 but repeated three times, not two.
	 */
	TT_RENIX_66_2_2_2 = 45,

	/**
	 * Honda K crank shape
	 */
	TT_HONDA_K_CRANK_12_1 = 46,

	TT_VVT_BOSCH_QUICK_START = 47,

	TT_TOOTHED_WHEEL_36_2 = 48,

	TT_SUBARU_SVX = 49,

	TT_1_16 = 50,

	// todo: remove this trigger once we have https://github.com/rusefi/rusefi/issues/2073
	TT_SUBARU_7_WITHOUT_6 = 51,

	TT_NISSAN_MR18_CAM_VVT = 52,

	// https://rusefi.com/forum/viewtopic.php?f=5&t=1912
	TT_TRI_TACH = 53,

	TT_GM_60_2_2_2 = 54,

	/**
	 * https://rusefi.com/forum/viewtopic.php?f=5&t=1937
	 * HALL sensor, and can be used on all Skoda's engines (from 1000MB to 130, Favorit, Felicia)
	 */
	TT_SKODA_FAVORIT = 55,

	TT_VVT_BARRA_3_PLUS_1 = 56,

	TT_KAWA_KX450F = 57,

	TT_NISSAN_VQ35 = 58,

	TT_VVT_NISSAN_VQ35 = 59,

	TT_NISSAN_VQ30 = 60,

	TT_NISSAN_QR25 = 61,

	TT_VVT_MITSUBISHI_3A92 = 62,

	TT_SUBARU_SVX_CRANK_1 = 63,

	TT_SUBARU_SVX_CAM_VVT = 64,

	TT_FORD_TFI_PIP = 65,

	TT_SUZUKI_G13B = 66,

	/**
	 * Honda K exhaust cam shaft
	 */
	TT_HONDA_K_CAM_4_1 = 67,

	TT_NISSAN_MR18_CRANK = 68,

	TT_TOOTHED_WHEEL_32_2 = 69,

	// Mitsubishi 4B11 would be this with half moon camshaft
	TT_36_2_1 = 70,

	// Mitsubishi 3 cyl and 6 cyl
	TT_36_2_1_1 = 71,


	TT_VVT_MITSUBISHI_6G75 = 72,

	TT_VVT_TOYOTA_4_1 = 73,

	// GM 24x with 5/10 degree gaps
	TT_GM_24x = 27,

	// GM 24x with 3/12 degree gaps
	TT_GM_24x_2 = 74,

	TT_SUBARU_EZ30 = 12,
	TT_VVT_MAZDA_SKYACTIV = 13,
	TT_MITSU_4G9x_CAM = 29,
	TT_MITSU_4G63_CAM = 34,

	TT_VVT_MAZDA_L = 75,

	// do not forget to edit "#define trigger_type_e_enum" line in integration/rusefi_config.txt file to propogate new value to rusefi.ini TS project
	// do not forget to invoke "gen_config.bat" once you make changes to integration/rusefi_config.txt
	// todo: one day a hero would integrate some of these things into Makefile in order to reduce manual magic
	//
	// Another point: once you add a new trigger, run get_trigger_images.bat which would run fome_test.exe from unit_tests
	//
	TT_UNUSED = 76, // this is used if we want to iterate over all trigger types
};

typedef enum {
	TS_DEBUG_MODE = 0,
	TS_UNUSED_1 = 1,
	TS_UNUSED_2 = 2,
	TS_UNUSED_3 = 3,
	TS_UNUSED_4 = 4,
	TS_UNUSED_5 = 5,
	TS_UNUSED_6 = 6,
	TS_UNUSED_7 = 7,
	TS_UNUSED_8 = 8,
	TS_UNUSED_9 = 9,
	TS_UNUSED_10 = 10,
	TS_UNUSED_11 = 11,
	TS_UNUSED_12 = 12,
	TS_UNUSED_13 = 13,
	TS_UNUSED_14 = 14,
	TS_UNUSED_15 = 15,
	TS_UNUSED_16 = 16,
	TS_CLEAR_WARNINGS = 17,
	TS_IGNITION_CATEGORY = 18,
	TS_INJECTOR_CATEGORY = 19,
	TS_X14 = 20,
	TS_WIDEBAND = 21,
	TS_BENCH_CATEGORY = 22,
	TS_UNUSED_23 = 23,
	TS_UNUSED_24 = 24,
	TS_SOLENOID_CATEGORY = 25,
	TS_UNUSED_26 = 26,
	TS_UNUSED_27 = 27,
	TS_UNUSED_28 = 28,
	TS_UNUSED_29 = 29,
	TS_SET_ENGINE_TYPE = 30,
	TS_SET_DEFAULT_ENGINE = 31,
	TS_LUA_OUTPUT_CATEGORY = 32,
} ts_command_e;

typedef enum {
	BENCH_MAIN_RELAY,
	BENCH_STARTER_ENABLE_RELAY,
	BENCH_STARTER_DISABLE_RELAY,
	BENCH_FAN_RELAY,
	BENCH_FAN_RELAY_2,
	BENCH_AC_COMPRESSOR_RELAY,
	BENCH_CHECK_ENGINE_LIGHT,
	BENCH_IDLE_VALVE,
	BENCH_HPFP_VALVE,
	BENCH_GPPWM1_VALVE,
	BENCH_GPPWM2_VALVE,
	BENCH_GPPWM3_VALVE,
	BENCH_GPPWM4_VALVE,
	BENCH_SECOND_IDLE_VALVE,
	BENCH_CANCEL,
	BENCH_FUEL_PUMP_ON,
	BENCH_FUEL_PUMP_OFF,
	BENCH_VVT_1_TARGET_BUMP,
	BENCH_VVT_2_TARGET_BUMP,
	BENCH_VVT_3_TARGET_BUMP,
	BENCH_VVT_4_TARGET_BUMP,
} bench_mode_e;

typedef enum {
	COMMAND_X14_UNUSED_0 = 0x00,
	COMMAND_X14_UNUSED_1 = 0x01,
	COMMAND_X14_UNUSED_2 = 0x02,
	COMMAND_X14_UNUSED_3 = 0x03,
	COMMAND_X14_UNUSED_4 = 0x04,
	COMMAND_X14_UNUSED_5 = 0x05,
	COMMAND_X14_GRAB_PEDAL_UP = 0x06,
	COMMAND_X14_GRAB_PEDAL_DOWN = 0x07,
	COMMAND_X14_RESET_TLE8888 = 0x08,
	COMMAND_X14_UNUSED_9 = 0x09,
	COMMAND_X14_WRITE_CONFIG = 0x0A,
	COMMAND_X14_UNUSED_11 = 0x0B,
	COMMAND_X14_ETB_AUTOTUNE = 0x0C,
	COMMAND_X14_ENABLE_SELF_STIM = 0x0D,
	COMMAND_X14_ETB_AUTO_CALIBRATE = 0x0E,
	COMMAND_X14_DISABLE_SELF_STIM = 0x0F,
	COMMAND_X14_ETB_AUTOTUNE_STOP = 0x10,
	COMMAND_X14_ETB2_AUTO_CALIBRATE = 0x11,
	COMMAND_X14_WIDEBAND_FIRMWARE_UPDATE = 0x12,
	COMMAND_X14_ENABLE_EXTERNAL_STIM = 0x13,
	COMMAND_X14_ETB_DISABLE_JAM_DETECT = 0x14,
	COMMAND_X14_BURN_WITHOUT_FLASH = 0x15,
} command_x14_e;
