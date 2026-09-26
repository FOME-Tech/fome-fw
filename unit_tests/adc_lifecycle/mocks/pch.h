#pragma once

#include "hal.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

#define EFI_PROD_CODE 0

#ifdef ADC_TEST_V4
#define EFI_USE_FAST_ADC 0
#else
#define EFI_USE_FAST_ADC 1
#endif
#define EFI_SOFTWARE_KNOCK 1
#define NO_CACHE
#define CCM_OPTIONAL
#define ADC_MAX_VALUE 4095
#define ADC_BUF_DEPTH_FAST 4
#define GPT_FREQ_FAST 1000000
#define GPT_PERIOD_FAST 100
#define EFI_IRQ_ADC_PRIORITY 5
#define PRIO_KNOCK_PROCESS 1
#define PAL_MODE_INPUT_ANALOG 0

using efitick_t = int64_t;
using FastAdcToken = size_t;
using adc_channel_e = int;
constexpr adc_channel_e EFI_ADC_0 = 0;
namespace efi {
using std::size;
}
inline float clampF(float lo, float value, float hi) {
	return std::clamp(value, lo, hi);
}
inline efitick_t getTimeNowNt() {
	return 1234;
}
inline bool isAdcChannelValid(adc_channel_e ch) {
	return ch >= 0 && ch < 16;
}
inline void assertInterruptPriority(const char*, int) {}
inline void firmwareError(const char*, ...) {
	assert(false);
}
inline std::vector<std::string> consoleLines;
inline std::function<void()> afterConsolePrint;
inline void efiPrintf(const char* format, ...) {
	assert(lockDepth == 0);
	char text[256];
	va_list args;
	va_start(args, format);
	int count = vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	assert(count >= 0 && count < 250); // Allow room for the production msg delimiters.
	consoleLines.emplace_back(text);
	if (afterConsolePrint) {
		afterConsolePrint();
	}
}
inline void (*adcStatsCommand)() = nullptr;
inline void addConsoleAction(const char* name, void (*action)()) {
	assert(std::string(name) == "adc_stats");
	adcStatsCommand = action;
}

enum class Gpio {
	A3,
	F4,
	F5
};
inline void efiSetPadMode(const char*, Gpio, int) {}
struct NamedOutputPin {
	NamedOutputPin(const char*, const char*) {}
	bool high = false;
	void setHigh() {
		high = true;
	}
	void setLow() {
		high = false;
	}
};
struct EngineConfiguration {
	bool enableSoftwareKnock = true;
	float adcVcc = 3.3f;
	float knockBandCustom = 0;
	float cylinderBore = 86;
};
inline EngineConfiguration configuration;
inline auto* engineConfiguration = &configuration;
struct KnockController {
	int results = 0;
	void onKnockSenseCompleted(uint8_t, uint8_t, float, efitick_t) {
		++results;
	}
};
inline KnockController knockController;
struct Engine {
	template <typename T>
	T* module() {
		return &knockController;
	}
};
inline Engine engineInstance;
inline auto* engine = &engineInstance;

struct Stm32AdcProviderBase {
	virtual const char* name() const = 0;
	virtual float get(size_t) const = 0;
};
inline void registerAdcProvider(Stm32AdcProviderBase&, int, int) {}

struct GPTDriver {};
struct GPTConfig {
	int frequency;
	void (*callback)(GPTDriver*);
	int cr2;
	int dier;
};
inline GPTDriver timer;
inline GPTDriver GPTD3;
#define EFI_INTERNAL_FAST_ADC_GPT (&timer)
inline const GPTConfig* timerConfig = nullptr;
inline void gptStart(GPTDriver*, const GPTConfig* config) {
	timerConfig = config;
}
inline void gptStartContinuous(GPTDriver*, int) {}

inline int fastResults = 0;
inline adcsample_t* lastFastSamples = nullptr;
inline void onFastAdcComplete(adcsample_t* samples) {
	++fastResults;
	lastFastSamples = samples;
}
void onKnockSamplingComplete();

namespace chibios_rt {
struct CriticalSectionLocker {
	CriticalSectionLocker() {
		osalSysLock();
	}
	~CriticalSectionLocker() {
		osalSysUnlock();
	}
};
struct BinarySemaphore {
	explicit BinarySemaphore(bool) {}
	int signals = 0;
	void signalI() {
		assert(lockDepth > 0);
		++signals;
	}
	void wait() {}
};
} // namespace chibios_rt
inline void chSysLockFromISR() {
	osalSysLock();
}
inline void chSysUnlockFromISR() {
	osalSysUnlock();
}

template <int Size>
struct ThreadController {
	ThreadController(const char*, int) {}
	virtual void ThreadTask() = 0;
	void startThread() {}
};
enum class PE {
	SoftwareKnockProcess
};
struct ScopePerf {
	explicit ScopePerf(PE) {}
};

template <typename T, size_t N, typename V>
void setArrayValues(T (&values)[N], V value) {
	std::fill(std::begin(values), std::end(values), value);
}
