#include "pch.h"

#include "init.h"
#include "adc_subscription.h"
#include "functional_sensor.h"
#include "table_func.h"

static FunctionalSensor fuelSensor(SensorType::FuelLevel, /* timeout = */ MS2NT(500));

// extract the type of the elements in the bin/value arrays
using BinType = std::remove_extent_t<decltype(config->fuelLevelBins)>;
using ValueType = std::remove_extent_t<decltype(config->fuelLevelValues)>;

static TableFunc fuelCurve(config->fuelLevelBins, config->fuelLevelValues);

void initFuelLevel() {
	adc_channel_e channel = engineConfiguration->fuelLevelSensor;

	if (!isAdcChannelValid(channel)) {
		return;
	}

	fuelSensor.setFunction(fuelCurve);

	// Filtering with such a small bandwidth helps prevent noisy data from fuel tank slosh
	AdcSubscription::SubscribeSensor(fuelSensor, channel, /*lowpassCutoff =*/ 0.05f);
	fuelSensor.Register();
}
