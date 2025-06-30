#include "pch.h"

#include "biquad.h"
#include "thread_controller.h"
#include "knock_logic.h"
#include "software_knock.h"

#if EFI_SOFTWARE_KNOCK

#include "knock_config.h"
#include "ch.hpp"

static int8_t currentCylinderNumber = 0;
static efitick_t lastKnockSampleTime;
static Biquad knockFilter;

static volatile bool knockIsSampling = false;
static volatile bool knockNeedsProcess = false;
static volatile size_t sampleCount = 0;

chibios_rt::BinarySemaphore knockSem(/* taken =*/ true);

static NamedOutputPin knockSnifferPin("knock window", "kn");


void onKnockSamplingComplete() {
	knockSnifferPin.setLow();

	knockNeedsProcess = true;

	// Notify the processing thread that it's time to process this sample
	chSysLockFromISR();
	knockSem.signalI();
	chSysUnlockFromISR();
}


void onStartKnockSampling(uint8_t cylinderNumber, float samplingSeconds, uint8_t channelIdx) {
	if (!engineConfiguration->enableSoftwareKnock) {
		return;
	}

	// Cancel if ADC isn't ready
	if (!((KNOCK_ADC.state == ADC_READY) ||
			(KNOCK_ADC.state == ADC_COMPLETE) ||
			(KNOCK_ADC.state == ADC_ERROR))) {
		return;
	}

	// If there's pending processing, skip this event
	if (knockNeedsProcess) {
		return;
	}

	// Convert sampling time to number of samples
	constexpr int sampleRate = KNOCK_SAMPLE_RATE;
	sampleCount = 0xFFFFFFFE & static_cast<size_t>(clampF(100, samplingSeconds * sampleRate, efi::size(knockSampleBuffer)));

	// Select the appropriate conversion group - it will differ depending on which sensor this cylinder should listen on
	auto conversionGroup = getKnockConversionGroup(channelIdx);

	// Stash the current cylinder's number so we can store the result appropriately
	currentCylinderNumber = cylinderNumber;

	adcStartConversionI(&KNOCK_ADC, conversionGroup, knockSampleBuffer, sampleCount);
	lastKnockSampleTime = getTimeNowNt();
	knockSnifferPin.setHigh();
}

class KnockThread : public ThreadController<256> {
public:
	KnockThread() : ThreadController("knock", PRIO_KNOCK_PROCESS) {}
	void ThreadTask() override;
};

static CCM_OPTIONAL KnockThread kt;

void initSoftwareKnock() {
	if (engineConfiguration->enableSoftwareKnock) {
		float freqKhz;

		if (engineConfiguration->knockBandCustom != 0) {
			freqKhz = engineConfiguration->knockBandCustom;
		} else {
			float bore = engineConfiguration->cylinderBore;

			if (bore == 0) {
				efiPrintf("Knock sense disabled due to invalid freq/bore");
				return;
			}

			if (bore < 10 || bore > 200) {
				firmwareError(ObdCode::OBD_PCM_Processor_Fault, "Invalid knock cylinder bore: %.1f", bore);
				return;
			}

			// derived from https://phormula.com/knock-frequency-calculator/
			freqKhz = 1140.0f / bore;
		}

		efiPrintf("Knock sense configuring filter with frequency %.2f khz", freqKhz);

		knockFilter.configureBandpass(KNOCK_SAMPLE_RATE, 1000 * freqKhz, 3);

		efiSetPadMode("knock ch1", KNOCK_PIN_CH1, PAL_MODE_INPUT_ANALOG);
#if KNOCK_HAS_CH2		
		efiSetPadMode("knock ch2", KNOCK_PIN_CH2, PAL_MODE_INPUT_ANALOG);
#endif
		kt.start();
	}
}

static void processLastKnockEvent() {
	if (!knockNeedsProcess) {
		return;
	}

	float sumSq = 0;

	float vcc = engineConfiguration->adcVcc;

	// Ratio in units of volts per ADC count
	float ratio = vcc / ADC_MAX_VALUE;

	size_t localCount = sampleCount;

	// Prepare the steady state at vcc/2 so that there isn't a step
	// when samples begin
	knockFilter.cookSteadyState(vcc / 2);

	// Compute the sum of squares
	for (size_t i = 0; i < localCount; i++) {
		float volts = ratio * knockSampleBuffer[i];

		float filtered = knockFilter.filter(volts);

		sumSq += filtered * filtered;
	}

	// take a local copy
	auto lastKnockTime = lastKnockSampleTime;

	// We're done with inspecting the buffer, another sample can be taken
	knockNeedsProcess = false;

	// mean of squares (not yet root)
	float meanSquares = sumSq / localCount;

	// RMS
	float db = 10 * log10(meanSquares);

	// clamp to reasonable range
	db = clampF(-100, db, 100);

	engine->module<KnockController>()->onKnockSenseCompleted(currentCylinderNumber, db, lastKnockTime);
}

void KnockThread::ThreadTask() {
	while (1) {
		knockSem.wait();

		ScopePerf perf(PE::SoftwareKnockProcess);
		processLastKnockEvent();
	}
}

#endif // EFI_SOFTWARE_KNOCK
