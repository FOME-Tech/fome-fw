#include "pch.h"
#include "gtest/gtest.h"

// Keep the production callers (including static timer/callback functions) intact.
// Only their surrounding hardware/services are mocked, not ADC state transitions.
#include "../../firmware/ext/ChibiOS/os/hal/src/hal_adc.c"
#include "../../firmware/hw_layer/adc/adc_diagnostics.cpp"
#ifdef ADC_TEST_V4
#include "../../firmware/hw_layer/ports/stm32/stm32_adc_v4.cpp"
#else
#include "../../firmware/hw_layer/ports/stm32/stm32_adc_v2.cpp"
#endif
#include "../../firmware/controllers/sensors/impl/software_knock.cpp"

ADCDriver ADCD1{};
ADCDriver ADCD2{};
ADCDriver ADCD3{};
static int starts;
static int stops;
void adc_lld_init() {}
void adc_lld_start(ADCDriver*) {}
void adc_lld_stop(ADCDriver*) {}
void adc_lld_start_conversion(ADCDriver*) {
	++starts;
}
void adc_lld_stop_conversion(ADCDriver*) {
	++stops;
}
void adcSTM32EnableTSVREFE() {}

class AdcLifecycle : public testing::Test {
protected:
	void SetUp() override {
		lockDepth = starts = stops = fastResults = knockController.results = 0;
		configuration = {};
		fastAdcDiagnostics = {};
		knockAdcDiagnostics = {};
		consoleLines.clear();
		afterConsolePrint = {};
		adcObjectInit(&ADCD1);
		adcObjectInit(&ADCD2);
		adcObjectInit(&ADCD3);
		portInitAdc();
#ifdef ADC_TEST_V4
		didStart = false;
#else
		adcgrpcfgFast.num_channels = 1;
#endif
		knockNeedsProcess = false;
		knockSem.signals = 0;
		knockSnifferPin.setLow();
		lastFastSamples = nullptr;
	}

	void startKnock() {
		// The scheduler invokes the knock callback with the system locked.
		chibios_rt::CriticalSectionLocker lock;
		onStartKnockSampling(2, 0.002f, 0);
	}

	void tickFast() {
#ifdef ADC_TEST_V4
		readSlowAnalogInputs();
#else
		timerConfig->callback(&timer);
#endif
	}
};

class AdcStartState : public AdcLifecycle, public testing::WithParamInterface<adcstate_t> {};

#ifndef ADC_TEST_V4
TEST_P(AdcStartState, FastTimer) {
	ADCD2.state = GetParam();
	tickFast();
	bool allowed = GetParam() == ADC_READY || GetParam() == ADC_ERROR;
	EXPECT_EQ(allowed ? 1 : 0, starts);
	EXPECT_EQ(allowed ? ADC_ACTIVE : GetParam(), ADCD2.state);
	EXPECT_EQ(0, lockDepth);
	if (allowed) {
		EXPECT_EQ(&adcgrpcfgFast, ADCD2.grpp);
		EXPECT_EQ(fastAdcSampleBuf, ADCD2.samples);
		EXPECT_EQ(size_t{ADC_BUF_DEPTH_FAST}, ADCD2.depth);
	}
}

#endif

TEST_P(AdcStartState, KnockScheduler) {
	KNOCK_ADC.state = GetParam();
	startKnock();
	bool allowed = GetParam() == ADC_READY || GetParam() == ADC_ERROR;
	EXPECT_EQ(allowed ? 1 : 0, starts);
	EXPECT_EQ(allowed ? ADC_ACTIVE : GetParam(), KNOCK_ADC.state);
	EXPECT_EQ(allowed, knockSnifferPin.high);
	EXPECT_EQ(0, lockDepth);
	if (allowed) {
		EXPECT_EQ(getKnockConversionGroup(0), KNOCK_ADC.grpp);
		EXPECT_EQ(knockSampleBuffer, KNOCK_ADC.samples);
		EXPECT_GE(KNOCK_ADC.depth, 100U);
		EXPECT_EQ(0U, KNOCK_ADC.depth % 2);
	}
}

INSTANTIATE_TEST_SUITE_P(
		AllStates,
		AdcStartState,
		testing::Values(ADC_UNINIT, ADC_STOP, ADC_READY, ADC_ACTIVE, ADC_COMPLETE, ADC_ERROR));

#ifndef ADC_TEST_V4
TEST_F(AdcLifecycle, FastRejectsInvalidChannelCounts) {
	for (auto count : {0U, 9U}) {
		adcgrpcfgFast.num_channels = count;
		tickFast();
		EXPECT_EQ(0, starts);
		EXPECT_EQ(ADC_READY, ADCD2.state);
	}
}

#endif

TEST_F(AdcLifecycle, DisabledKnockDoesNotStart) {
	engineConfiguration->enableSoftwareKnock = false;
	startKnock();
	EXPECT_EQ(0, starts);
	EXPECT_EQ(ADC_READY, KNOCK_ADC.state);
}

#ifndef ADC_TEST_V4
TEST_F(AdcLifecycle, FastHalfAndFullBufferThenNextConversion) {
	tickFast();
	_adc_isr_half_code(&ADCD2);
	EXPECT_EQ(0, fastResults);
	EXPECT_EQ(0U, getAdcDiagnostics().fast.completed);
	EXPECT_EQ(ADC_ACTIVE, ADCD2.state);
	_adc_isr_full_code(&ADCD2);
	EXPECT_EQ(1, fastResults);
	EXPECT_EQ(fastAdcSampleBuf, lastFastSamples);
	EXPECT_EQ(1U, getAdcDiagnostics().fast.completed);
	EXPECT_EQ(ADC_READY, ADCD2.state);
	EXPECT_EQ(1, stops);
	tickFast();
	EXPECT_EQ(2, starts);
	EXPECT_EQ(ADC_ACTIVE, ADCD2.state);
}

#endif

TEST_F(AdcLifecycle, KnockHalfAndFullBufferThenProcessingAndNextConversion) {
	startKnock();
	_adc_isr_half_code(&KNOCK_ADC);
	EXPECT_EQ(0, knockSem.signals);
	EXPECT_FALSE(knockNeedsProcess);
	EXPECT_TRUE(knockSnifferPin.high);
	_adc_isr_full_code(&KNOCK_ADC);
	EXPECT_EQ(1, knockSem.signals);
	EXPECT_TRUE(knockNeedsProcess);
	EXPECT_FALSE(knockSnifferPin.high);
	EXPECT_EQ(ADC_READY, KNOCK_ADC.state);
	// HAL is ready, but the knock thread still owns the previous sample buffer.
	startKnock();
	EXPECT_EQ(1, starts);
	processLastKnockEvent();
	EXPECT_EQ(1, knockController.results);
	processLastKnockEvent();
	EXPECT_EQ(1, knockController.results);
	startKnock();
	EXPECT_EQ(2, starts);
	EXPECT_EQ(ADC_ACTIVE, KNOCK_ADC.state);
}

#ifndef ADC_TEST_V4
TEST_F(AdcLifecycle, FastTickDuringCompletionSkipsAndRecovers) {
	tickFast();
	// Reproduce timer preemption inside the real HAL's ADC_COMPLETE interval.
	ADCConversionGroup group = adcgrpcfgFast;
	group.end_cb = [](ADCDriver* adc) {
		EXPECT_EQ(ADC_COMPLETE, adc->state);
		timerConfig->callback(&timer);
		EXPECT_EQ(1, starts);
		EXPECT_EQ(1U, getAdcDiagnostics().fast.skippedComplete);
		EXPECT_EQ(0U, getAdcDiagnostics().fast.errors);
		adc_callback_fast(adc);
	};
	ADCD2.grpp = &group;
	_adc_isr_full_code(&ADCD2);
	EXPECT_EQ(1, fastResults);
	EXPECT_EQ(ADC_READY, ADCD2.state);
	tickFast();
	EXPECT_EQ(2, starts);
}

#endif

TEST_F(AdcLifecycle, KnockWindowDuringCompletionSkipsAndRecovers) {
	startKnock();
	ADCConversionGroup group = *KNOCK_ADC.grpp;
	group.end_cb = [](ADCDriver* adc) {
		EXPECT_EQ(ADC_COMPLETE, adc->state);
		{
			chibios_rt::CriticalSectionLocker lock;
			onStartKnockSampling(3, 0.002f, 0);
		}
		EXPECT_EQ(1, starts);
		EXPECT_EQ(2, currentCylinderNumber);
		EXPECT_EQ(1U, getAdcDiagnostics().knock.skippedComplete);
		EXPECT_EQ(0U, getAdcDiagnostics().knock.errors);
		knockCompletionCallback(adc);
	};
	KNOCK_ADC.grpp = &group;
	_adc_isr_full_code(&KNOCK_ADC);
	EXPECT_EQ(1, knockSem.signals);
	EXPECT_EQ(ADC_READY, KNOCK_ADC.state);
	processLastKnockEvent();
	EXPECT_EQ(1, knockController.results);
	startKnock();
	EXPECT_EQ(2, starts);
}

static uint32_t skipped(const AdcDiagnostics& counters) {
	return counters.skippedActive + counters.skippedComplete + counters.skippedNotReady + counters.skippedNoChannels +
		   counters.skippedInvalidChannels + counters.skippedDisabled + counters.skippedPending;
}

TEST_P(AdcStartState, KnockStateCounters) {
	KNOCK_ADC.state = GetParam();
	startKnock();
	const auto counters = getAdcDiagnostics().knock;
	const bool allowed = GetParam() == ADC_READY || GetParam() == ADC_ERROR;
	EXPECT_EQ(allowed ? 1U : 0U, counters.started);
	EXPECT_EQ(allowed ? 0U : 1U, skipped(counters));
	EXPECT_EQ(GetParam() == ADC_ACTIVE ? 1U : 0U, counters.skippedActive);
	EXPECT_EQ(GetParam() == ADC_COMPLETE ? 1U : 0U, counters.skippedComplete);
	EXPECT_EQ(GetParam() == ADC_UNINIT || GetParam() == ADC_STOP ? 1U : 0U, counters.skippedNotReady);
	EXPECT_EQ(0U, counters.errors); // Starting in ADC_ERROR is not a new HAL error.
	EXPECT_EQ(0U, counters.completed);
}

#ifndef ADC_TEST_V4
TEST_P(AdcStartState, FastStateCounters) {
	ADCD2.state = GetParam();
	tickFast();
	const auto counters = getAdcDiagnostics().fast;
	const bool allowed = GetParam() == ADC_READY || GetParam() == ADC_ERROR;
	EXPECT_EQ(allowed ? 1U : 0U, counters.started);
	EXPECT_EQ(allowed ? 0U : 1U, skipped(counters));
	EXPECT_EQ(GetParam() == ADC_ACTIVE ? 1U : 0U, counters.skippedActive);
	EXPECT_EQ(GetParam() == ADC_COMPLETE ? 1U : 0U, counters.skippedComplete);
	EXPECT_EQ(GetParam() == ADC_UNINIT || GetParam() == ADC_STOP ? 1U : 0U, counters.skippedNotReady);
	EXPECT_EQ(0U, counters.errors);
}

TEST_F(AdcLifecycle, FastConfigurationSkipReasons) {
	adcgrpcfgFast.num_channels = 0;
	tickFast();
	adcgrpcfgFast.num_channels = 9;
	tickFast();
	auto counters = getAdcDiagnostics().fast;
	EXPECT_EQ(1U, counters.skippedNoChannels);
	EXPECT_EQ(1U, counters.skippedInvalidChannels);
	EXPECT_EQ(2U, skipped(counters));
	EXPECT_EQ(0U, counters.errors);
	EXPECT_EQ(0U, counters.started);
}
#endif

TEST_F(AdcLifecycle, KnockPendingDisabledAndFullBufferCounts) {
	engineConfiguration->enableSoftwareKnock = false;
	KNOCK_ADC.state = ADC_COMPLETE;
	startKnock(); // The first rejection reason wins, preserving the guard order.
	EXPECT_EQ(1U, getAdcDiagnostics().knock.skippedDisabled);
	EXPECT_EQ(0U, getAdcDiagnostics().knock.skippedComplete);
	engineConfiguration->enableSoftwareKnock = true;
	KNOCK_ADC.state = ADC_READY;
	startKnock();
	_adc_isr_half_code(&KNOCK_ADC);
	EXPECT_EQ(0U, getAdcDiagnostics().knock.completed);
	_adc_isr_full_code(&KNOCK_ADC);
	startKnock();
	auto counters = getAdcDiagnostics().knock;
	EXPECT_EQ(1U, counters.started);
	EXPECT_EQ(1U, counters.completed);
	EXPECT_EQ(1U, counters.skippedPending);
	EXPECT_EQ(0U, counters.errors);
	EXPECT_EQ(0U, counters.processed);
	processLastKnockEvent();
	processLastKnockEvent();
	EXPECT_EQ(1U, getAdcDiagnostics().knock.processed);
	startKnock();
	EXPECT_EQ(2U, getAdcDiagnostics().knock.started);
}

TEST_F(AdcLifecycle, KnockErrorIsObservedAndHalRecovers) {
	startKnock();
	_adc_isr_error_code(&KNOCK_ADC, ADC_ERR_DMAFAILURE);
	auto counters = getAdcDiagnostics().knock;
	EXPECT_EQ(1U, counters.started);
	EXPECT_EQ(1U, counters.errors);
	EXPECT_EQ(uint32_t{ADC_ERR_DMAFAILURE}, counters.lastError);
	EXPECT_EQ(0U, counters.completed);
	EXPECT_EQ(0U, counters.processed);
	EXPECT_EQ(0, knockSem.signals);
	EXPECT_EQ(ADC_READY, KNOCK_ADC.state);
	EXPECT_EQ(nullptr, KNOCK_ADC.grpp);
	startKnock();
	_adc_isr_error_code(&KNOCK_ADC, ADC_ERR_OVERFLOW);
	EXPECT_EQ(2U, getAdcDiagnostics().knock.errors);
	EXPECT_EQ(uint32_t{ADC_ERR_OVERFLOW}, getAdcDiagnostics().knock.lastError);
	startKnock();
	_adc_isr_full_code(&KNOCK_ADC);
	processLastKnockEvent();
	EXPECT_EQ(3U, getAdcDiagnostics().knock.started);
	EXPECT_EQ(1U, getAdcDiagnostics().knock.completed);
	EXPECT_EQ(1U, getAdcDiagnostics().knock.processed);
}

TEST_F(AdcLifecycle, FastErrorIsObservedWithoutPublishingSamples) {
	tickFast();
#ifdef ADC_TEST_V4
	auto* driver = &ADCD1;
#else
	auto* driver = &ADCD2;
#endif
	_adc_isr_error_code(driver, ADC_ERR_DMAFAILURE);
	const auto counters = getAdcDiagnostics().fast;
	EXPECT_EQ(1U, counters.started);
	EXPECT_EQ(1U, counters.errors);
	EXPECT_EQ(uint32_t{ADC_ERR_DMAFAILURE}, counters.lastError);
	EXPECT_EQ(0U, counters.completed);
	EXPECT_EQ(0, fastResults);
	EXPECT_EQ(ADC_READY, driver->state);
	EXPECT_EQ(nullptr, driver->grpp);
#ifndef ADC_TEST_V4
	tickFast();
	_adc_isr_full_code(driver);
	EXPECT_EQ(2U, getAdcDiagnostics().fast.started);
	EXPECT_EQ(1U, getAdcDiagnostics().fast.completed);
#else
	// Preserve the existing H7 behavior: didStart stays true after an error.
	// Diagnostics report the stopped stream; they must not silently restart it.
	tickFast();
	EXPECT_EQ(1U, getAdcDiagnostics().fast.started);
	EXPECT_EQ(ADC_READY, driver->state);
#endif
}

#ifdef ADC_TEST_V4
TEST_F(AdcLifecycle, CircularFastCountsBuffersNotStreamStarts) {
	tickFast();
	for (int i = 0; i < 3; i++) {
		_adc_isr_half_code(&ADCD1);
		EXPECT_EQ(static_cast<uint32_t>(i), getAdcDiagnostics().fast.completed);
		_adc_isr_full_code(&ADCD1);
		EXPECT_EQ(ADC_ACTIVE, ADCD1.state);
		tickFast();
	}
	EXPECT_EQ(1U, getAdcDiagnostics().fast.started);
	EXPECT_EQ(3U, getAdcDiagnostics().fast.completed);
	EXPECT_EQ(3, fastResults);
	EXPECT_EQ(0, stops);
}
#endif

TEST_F(AdcLifecycle, ConsoleUsesOneSnapshotOutsideTheLock) {
	startKnock();
	_adc_isr_full_code(&KNOCK_ADC);
	// Simulate new data arriving after the snapshot, during slow console output.
	afterConsolePrint = [] { knockAdcDiagnostics.completed = 100; };
	ASSERT_NE(nullptr, adcStatsCommand);
	adcStatsCommand();
	ASSERT_EQ(7U, consoleLines.size());
	EXPECT_NE(std::string::npos, consoleLines[0].find("knock_compiled=1"));
#ifdef ADC_TEST_V4
	EXPECT_NE(std::string::npos, consoleLines[0].find("started_unit=stream"));
#else
	EXPECT_NE(std::string::npos, consoleLines[0].find("started_unit=conversion"));
#endif
	EXPECT_EQ("adc knock started=1 completed=1 errors=0 last_error=0 processed=0", consoleLines[4]);
	EXPECT_EQ(100U, getAdcDiagnostics().knock.completed);
	EXPECT_EQ(0, lockDepth);
}

TEST_F(AdcLifecycle, CountersWrapAndConsoleFitsMaximumValues) {
	knockAdcDiagnostics.started = UINT32_MAX;
	startKnock();
	EXPECT_EQ(0U, getAdcDiagnostics().knock.started);
	knockAdcDiagnostics.completed = UINT32_MAX;
	_adc_isr_full_code(&KNOCK_ADC);
	EXPECT_EQ(0U, getAdcDiagnostics().knock.completed);
	AdcDiagnostics maximum{
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX,
			UINT32_MAX};
	fastAdcDiagnostics = knockAdcDiagnostics = maximum;
	adcStatsCommand(); // The mock checks formatting fits the actual 256-byte line buffer.
	EXPECT_EQ(7U, consoleLines.size());
}
