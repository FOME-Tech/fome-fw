/**
 * @file	engine.h
 *
 * @date May 21, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "global_shared.h"
#include "engine_module.h"
#include "engine_state.h"
#include "rpm_calculator.h"
#include "event_registry.h"
#include "table_helper.h"
#include "listener_array.h"
#include "accel_enrichment.h"
#include "trigger_central.h"
#include "local_version_holder.h"
#include "buttonshift.h"
#include "gear_controller.h"
#include "high_pressure_fuel_pump.h"
#include "limp_manager.h"
#include "pin_repository.h"
#include "ac_control.h"
#include "knock_logic.h"
#include "idle_state_generated.h"
#include "dc_motors_generated.h"
#include "idle_thread.h"
#include "injector_model.h"
#include "launch_control.h"
#include "antilag_system.h"
#include "trigger_scheduler.h"
#include "main_relay.h"
#include "ac_control.h"
#include "type_list.h"
#include "boost_control.h"
#include "ignition_controller.h"
#include "alternator_controller.h"
#include "harley_acr.h"
#include "dfco.h"
#include "fuel_computer.h"
#include "advance_map.h"
#include "ignition_state.h"
#include "sensor_checker.h"
#include "fuel_schedule.h"
#include "prime_injection.h"
#include "throttle_model.h"
#include "lambda_monitor.h"
#include "vvt.h"

#ifndef EFI_BOOTLOADER
#include "engine_modules_generated.h"
#endif

#include <functional>

#ifndef EFI_UNIT_TEST
#error EFI_UNIT_TEST must be defined!
#endif

#ifndef EFI_SIMULATOR
#error EFI_SIMULATOR must be defined!
#endif

#ifndef EFI_PROD_CODE
#error EFI_PROD_CODE must be defined!
#endif

#if EFI_SIGNAL_EXECUTOR_ONE_TIMER
// PROD real firmware uses this implementation
#include "single_timer_executor.h"
#endif /* EFI_SIGNAL_EXECUTOR_ONE_TIMER */
#if EFI_SIGNAL_EXECUTOR_SLEEP
#include "signal_executor_sleep.h"
#endif /* EFI_SIGNAL_EXECUTOR_SLEEP */
#if EFI_UNIT_TEST
#include "global_execution_queue.h"
#endif /* EFI_UNIT_TEST */

struct AirmassModelBase;

#define MAF_DECODING_CACHE_SIZE 256

#define MAF_DECODING_CACHE_MULT (MAF_DECODING_CACHE_SIZE / 5.0)

/**
 * I am not sure if this needs to be configurable.
 *
 * Also technically the whole feature might be implemented as cranking fuel coefficient curve by TPS.
 */
// todo: not great location for these
#define CLEANUP_MODE_TPS 90
#define STEPPER_PARKING_TPS CLEANUP_MODE_TPS

class IEtbController;

class LedBlinkingTask : public EngineModule {
public:
	void onSlowCallback() override;

private:
	void updateRunningLed();
	void updateWarningLed();
	void updateCommsLed();
	void updateErrorLed();

	size_t m_commBlinkCounter = 0;
	size_t m_errorBlinkCounter = 0;
};

class OneCylinder final {
public:
	void updateCylinderNumber(uint8_t index, uint8_t cylinderNumber);
	void invalidCylinder();

	// Get this cylinder's offset, in positive degrees, from cylinder 1
	angle_t getAngleOffset() const;

	// **************************
	//           Fuel
	// **************************

	// Get the angle to open this cylinder's injector, in engine cycle angle, relative to #1 TDC
	expected<angle_t> computeInjectionAngle(injection_mode_e mode) const;

	// This cylinder's per-cycle injection mass, uncorrected for injection mode (may be split in to multiple injections later)
	mass_t getInjectionMass() const {
		return m_injectionMass;
	}

	void setInjectionMass(mass_t m) {
		m_injectionMass = m;
	}

	// **************************
	//         Ignition
	// **************************
	void setIgnitionTimingBtdc(angle_t deg) {
		m_timingAdvance = deg;
	}

	angle_t getIgnitionTimingBtdc() const {
		return m_timingAdvance;
	}

	// Get angle of the spark firing in engine cycle coordinates, relative to #1 TDC
	angle_t getSparkAngle(angle_t lateAdjustment) const;

private:
	bool m_valid = false;

	// This cylinder's position in the firing order (0-based)
	uint8_t m_cylinderIndex = 0;
	// This cylinder's physical cylinder number (0-based)
	uint8_t m_cylinderNumber = 0;

	// This cylinder's mechanical TDC offset in degrees after #1
	angle_t m_baseAngleOffset;

	mass_t m_injectionMass = 0;

	// 10 means 10 degrees BTDC
	angle_t m_timingAdvance = 0;
};

union IgnitionContext;

class Engine final : public TriggerStateListener {
public:
	Engine();

	TunerStudioOutputChannels outputChannels;

	/**
	 * Sometimes for instance during shutdown we need to completely supress CAN TX
	 */
	bool allowCanTx = true;

	// used by HW CI
	bool isPwmEnabled = true;

	PinRepository pinRepository;

	IEtbController *etbControllers[ETB_COUNT] = {nullptr};

	FuelComputer fuelComputer;

	type_list<
		Mockable<InjectorModelPrimary>,
		Mockable<InjectorModelSecondary>,
#if EFI_IDLE_CONTROL
		Mockable<IdleController>,
#endif // EFI_IDLE_CONTROL
		TriggerScheduler,
#if EFI_HPFP && EFI_ENGINE_CONTROL
		HpfpController,
#endif // EFI_HPFP && EFI_ENGINE_CONTROL
		Mockable<ThrottleModel>,
#if EFI_ALTERNATOR_CONTROL
		AlternatorController,
#endif /* EFI_ALTERNATOR_CONTROL */
		MainRelayController,
		Mockable<IgnitionController>,
		Mockable<AcController>,
		PrimeController,
		DfcoController,
		HarleyAcr,
		Mockable<WallFuelController>,
		KnockController,
		SensorChecker,
		LimpManager,
#if EFI_VVT_PID
		VvtController1,
		VvtController2,
		VvtController3,
		VvtController4,
#endif // EFI_VVT_PID
#if EFI_BOOST_CONTROL
		BoostController,
#endif // EFI_BOOST_CONTROL
		LedBlinkingTask,
		TpsAccelEnrichment,

		#ifndef EFI_BOOTLOADER
		#include "modules_list_generated.h"
		#endif

		EngineModule // dummy placeholder so the previous entries can all have commas
		> engineModules;

	/**
	 * Slightly shorter helper function to keep the code looking clean.
	 */
	template<typename get_t>
	constexpr auto & module() {
		return engineModules.get<get_t>();
	}

#if EFI_TCU
	GearControllerBase *gearController;
#endif
	
#if EFI_LAUNCH_CONTROL
	LaunchControlBase launchController;
	SoftSparkLimiter softSparkLimiter;
#endif // EFI_LAUNCH_CONTROL

#if EFI_ANTILAG_SYSTEM
	AntilagSystemBase antilagController;
#endif // EFI_ANTILAG_SYSTEM

#if EFI_ANTILAG_SYSTEM
	SoftSparkLimiter ALSsoftSparkLimiter;
#endif /* EFI_ANTILAG_SYSTEM */

	LambdaMonitor lambdaMonitor;

	IgnitionState ignitionState;
	void resetLua();

	efitick_t startStopStateLastPushTime;

#if EFI_SHAFT_POSITION_INPUT
	void OnTriggerStateProperState(efitick_t nowNt) override;
	void OnTriggerSyncronization(bool wasSynchronized, bool isDecodingError) override;
	void OnTriggerSynchronizationLost() override;
#endif

	void setConfig();

	AuxActor auxValves[AUX_DIGITAL_VALVE_COUNT][2];

#if EFI_UNIT_TEST
	bool needTdcCallback = true;
#endif /* EFI_UNIT_TEST */


	int getGlobalConfigurationVersion(void) const;


	// a pointer with interface type would make this code nicer but would carry extra runtime
	// cost to resolve pointer, we use instances as a micro optimization
#if EFI_SIGNAL_EXECUTOR_ONE_TIMER
	SingleTimerExecutor scheduler;
#endif
#if EFI_SIGNAL_EXECUTOR_SLEEP
	SleepExecutor scheduler;
#endif
#if EFI_UNIT_TEST
	TestExecutor scheduler;

	std::function<void(const IgnitionContext&, bool)> onIgnitionEvent;
#endif // EFI_UNIT_TEST

#if EFI_ENGINE_CONTROL
	FuelSchedule injectionEvents;
	IgnitionEventList ignitionEvents;
	scheduling_s tdcScheduler[2];
	OneCylinder cylinders[MAX_CYLINDER_COUNT];
#endif /* EFI_ENGINE_CONTROL */

	// todo: move to electronic_throttle something?
	bool etbAutoTune = false;
	bool etbIgnoreJamProtection = false;

#if EFI_UNIT_TEST
	bool tdcMarkEnabled = true;
#endif // EFI_UNIT_TEST

	RpmCalculator rpmCalculator;

	Timer configBurnTimer;

	/**
	 * This counter is incremented every time user adjusts ECU parameters online (either via rusEfi console or other
	 * tuning software)
	 */
	int globalConfigurationVersion = 0;

#if EFI_SHAFT_POSITION_INPUT
	TriggerCentral triggerCentral;
#endif // EFI_SHAFT_POSITION_INPUT

	float stftCorrection[STFT_BANK_COUNT] = {0};

	void periodicFastCallback();
	void periodicSlowCallback();
	void updateSlowSensors();
	void updateSwitchInputs();
	void updateTriggerWaveform();

	bool isRunningPwmTest = false;

	/**
	 * are we running any kind of functional test? this affect
	 * some areas
	 */
	bool isFunctionalTestMode = false;

	void resetEngineSnifferIfInTestMode();

	EngineState engineState;

	dc_motors_s dc_motors;

	/**
	 * idle blip is a development tool: alternator PID research for instance have benefited from a repetitive change of RPM
	 */
	efitimeus_t timeToStopIdleTest = 0;


	SensorsState sensors;
	Timer mainRelayBenchTimer;


	void preCalculate();

	void efiWatchdog();

	/**
	 * Needed by EFI_MAIN_RELAY_CONTROL to shut down the engine correctly.
	 * This method cancels shutdown if the ignition voltage is detected.
	 */
	void checkShutdown();

	/**
	 * Allows to finish some long-term shutdown procedures (stepper motor parking etc.)
	   Called when the ignition switch is turned off (vBatt is too low).
	   Returns true if some operations are in progress on background.
	 */
	bool isInShutdownMode() const;

	bool isInMainRelayBench();

	/**
	 * The stepper does not work if the main relay is turned off (it requires +12V).
	 * Needed by the stepper motor code to detect if it works.
	 */
	bool isMainRelayEnabled() const;

	void onSparkFireKnockSense(uint8_t cylinderIndex, efitick_t nowNt);

#if EFI_UNIT_TEST
	AirmassModelBase* mockAirmassModel = nullptr;
#endif

private:
	void reset();

	void injectEngineReferences();
};

trigger_type_e getVvtTriggerType(vvt_mode_e vvtMode);

void applyNonPersistentConfiguration();
void prepareOutputSignals();

// todo: huh we also have validateConfig()?!
void validateConfiguration();
void scheduleReboot();
bool isLockedFromUser();
void unlockEcu(int password);

// These externs aren't needed for unit tests - everything is injected instead
#if !EFI_UNIT_TEST
extern Engine ___engine;
static constexpr Engine * const engine = &___engine;
#else // EFI_UNIT_TEST
extern Engine *engine;
#endif // EFI_UNIT_TEST
