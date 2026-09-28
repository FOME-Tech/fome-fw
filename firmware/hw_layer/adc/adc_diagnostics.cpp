#include "pch.h"

#if HAL_USE_ADC
#include "adc_diagnostics.h"

AdcDiagnostics fastAdcDiagnostics;
AdcDiagnostics knockAdcDiagnostics;
static bool fastIsCircular;

AdcDiagnosticsSnapshot getAdcDiagnostics() {
	// Timer/scheduler updates are already locked. ADC/DMA callbacks use
	// EFI_IRQ_ADC_PRIORITY, which this system lock masks; it also prevents
	// the knock processing thread from running during the copy.
	chibios_rt::CriticalSectionLocker lock;
	return {fastAdcDiagnostics, knockAdcDiagnostics};
}

static void printCounters(const char* name, const AdcDiagnostics& counters) {
	efiPrintf(
			"adc %s started=%lu completed=%lu errors=%lu last_error=%lu processed=%lu",
			name,
			static_cast<unsigned long>(counters.started),
			static_cast<unsigned long>(counters.completed),
			static_cast<unsigned long>(counters.errors),
			static_cast<unsigned long>(counters.lastError),
			static_cast<unsigned long>(counters.processed));
	efiPrintf(
			"adc %s skip_active=%lu skip_complete=%lu skip_not_ready=%lu",
			name,
			static_cast<unsigned long>(counters.skippedActive),
			static_cast<unsigned long>(counters.skippedComplete),
			static_cast<unsigned long>(counters.skippedNotReady));
	efiPrintf(
			"adc %s skip_no_channels=%lu skip_invalid_channels=%lu skip_disabled=%lu skip_pending=%lu",
			name,
			static_cast<unsigned long>(counters.skippedNoChannels),
			static_cast<unsigned long>(counters.skippedInvalidChannels),
			static_cast<unsigned long>(counters.skippedDisabled),
			static_cast<unsigned long>(counters.skippedPending));
}

static void printAdcDiagnostics() {
	const auto snapshot = getAdcDiagnostics();
	// Formatting and console I/O must stay outside the critical section.
	efiPrintf(
			"adc mode=%s started_unit=%s completed_unit=buffer counters=uint32_since_boot knock_compiled=%d",
			fastIsCircular ? "circular" : "timer",
			fastIsCircular ? "stream" : "conversion",
#if EFI_SOFTWARE_KNOCK
			1
#else
			0
#endif
	);
	printCounters("fast", snapshot.fast);
	printCounters("knock", snapshot.knock);
}

void initAdcDiagnostics(bool circular) {
	fastIsCircular = circular;
	addConsoleAction("adc_stats", printAdcDiagnostics);
}
#endif // HAL_USE_ADC
