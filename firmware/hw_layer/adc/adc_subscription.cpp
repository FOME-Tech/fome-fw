#include "pch.h"

#include "adc_subscription.h"
#include "adc_provider.h"
#include "adc_inputs.h"

#include "biquad.h"

struct AdcSubscriptionEntry {
	FunctionalSensor* Sensor;
	float VoltsPerAdcVolt;
	Biquad Filter;
	adc_channel_e Channel;
	bool HasUpdated = false;
};

#ifndef ADC_SUBSCRIPTION_SLOTS
#define ADC_SUBSCRIPTION_SLOTS 16
#endif

static AdcSubscriptionEntry s_entries[ADC_SUBSCRIPTION_SLOTS];

static AdcSubscriptionEntry* findEntry(FunctionalSensor* sensor) {
	for (size_t i = 0; i < efi::size(s_entries); i++) {
		if (s_entries[i].Sensor == sensor) {
			return &s_entries[i];
		}
	}

	return nullptr;
}

static AdcSubscriptionEntry* findEntry() {
	// Find an entry with no sensor set
	return findEntry(nullptr);
}

/*static*/ void AdcSubscription::SubscribeSensor(
		FunctionalSensor& sensor, adc_channel_e channel, float lowpassCutoff, float voltsPerAdcVolt /*= 0.0f*/) {
	// Don't subscribe null channels
	if (!isAdcChannelValid(channel)) {
		return;
	}

	// If you passed the same sensor again, resubscribe it with the new parameters
	auto entry = findEntry(&sensor);

	if (entry) {
		// If the channel didn't change, we're already set
		if (entry->Channel == channel) {
			return;
		}

		// avoid updates to this while we're mucking with the configuration
		entry->Sensor = nullptr;
	} else {
		// If not already registered, get an empty (new) entry
		entry = findEntry();
	}

	const char* name = sensor.getSensorName();

	// Ensure that a free entry was found
	if (!entry) {
		firmwareError(ObdCode::CUSTOM_INVALID_ADC, "too many ADC subscriptions subscribing %s", name);
		return;
	}

	// Enable the input pin
	AdcProvider::acquire(channel);

	// if 0, default to the board's divider coefficient for given channel
	if (voltsPerAdcVolt == 0) {
		voltsPerAdcVolt = getAnalogInputDividerCoefficient(channel);
	}

	// Populate the entry
	entry->VoltsPerAdcVolt = voltsPerAdcVolt;
	entry->Channel = channel;
	entry->Filter.configureLowpass(hzForPeriod(ADC_UPDATE_RATE), lowpassCutoff);
	entry->HasUpdated = false;

	// Set the sensor last - it's the field we use to determine whether this entry is in use
	entry->Sensor = &sensor;
}

/*static*/ void AdcSubscription::UnsubscribeSensor(FunctionalSensor& sensor) {
	auto entry = findEntry(&sensor);

	if (!entry) {
		// This sensor wasn't configured, skip it
		return;
	}

	AdcProvider::release(entry->Channel);

	sensor.unregister();

	// clear the sensor first to mark this entry not in use
	entry->Sensor = nullptr;

	entry->VoltsPerAdcVolt = 0;
	entry->Channel = EFI_ADC_NONE;
}

/*static*/ void AdcSubscription::UnsubscribeSensor(FunctionalSensor& sensor, adc_channel_e channel) {
	// Find the old sensor
	auto entry = findEntry(&sensor);

	// if the channel changed, unsubscribe!
	if (entry && entry->Channel != channel) {
		AdcSubscription::UnsubscribeSensor(sensor);
	}
}

void AdcSubscription::UpdateSubscribers(efitick_t nowNt) {
	ScopePerf perf(PE::AdcSubscriptionUpdateSubscribers);

	for (size_t i = 0; i < efi::size(s_entries); i++) {
		auto& entry = s_entries[i];

		if (!entry.Sensor) {
			// Skip unconfigured entries
			continue;
		}

		float mcuVolts = AdcProvider::getVoltage(entry.Channel);
		float sensorVolts = mcuVolts * entry.VoltsPerAdcVolt;

		// On the very first update, preload the filter as if we've been
		// seeing this value for a long time.  This prevents a slow ramp-up
		// towards the correct value just after startup
		if (!entry.HasUpdated) {
			entry.Filter.cookSteadyState(sensorVolts);
			entry.HasUpdated = true;
		}

		float filtered = entry.Filter.filter(sensorVolts);

		entry.Sensor->postRawValue(filtered, nowNt);
	}
}

void AdcSubscription::PrintInfo() {
	for (size_t i = 0; i < efi::size(s_entries); i++) {
		auto& entry = s_entries[i];

		if (!entry.Sensor) {
			// Skip unconfigured entries
			continue;
		}

		const auto name = entry.Sensor->getSensorName();
		float mcuVolts = AdcProvider::getVoltage(entry.Channel);
		float sensorVolts = mcuVolts * entry.VoltsPerAdcVolt;
		auto channel = entry.Channel;

		char pinNameBuffer[16];

		efiPrintf(
				"%s ADC%d %s adc=%.2f/input=%.2fv/divider=%.2f",
				name,
				channel,
				getPinNameByAdcChannel(name, channel, pinNameBuffer),
				mcuVolts,
				sensorVolts,
				entry.VoltsPerAdcVolt);
	}
}
