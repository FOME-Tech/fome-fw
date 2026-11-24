#include "pch.h"

#include "init.h"
#include "adc_subscription.h"
#include "functional_sensor.h"
#include "table_func.h"
#include "func_chain.h"

static FunctionalSensor maf(SensorType::Maf, /* timeoutPeriod = */ MS2NT(50));
static FunctionalSensor maf2(SensorType::Maf2, /* timeoutPeriod = */ MS2NT(50));

// Just check min/max allowed voltage
struct MafVoltageCheck : public SensorConverter {
	SensorResult convert(float input) const override {
		if (input > engineConfiguration->mafMaxVoltage) {
			return UnexpectedCode::High;
		}

		if (input < engineConfiguration->mafMinVoltage) {
			return UnexpectedCode::Low;
		}

		return input;
	}
};

// This function converts volts -> kg/h
static TableFunc mafCurve(config->mafDecodingBins, config->mafDecoding);

// grumble grumble func_chain doesn't do constructor parameters so we need an adapter
struct MafTable : public SensorConverter {
	SensorResult convert(float input) const override {
		return mafCurve.convert(input);
	}
};

static FuncChain<MafVoltageCheck, MafTable> mafFunction;

static void initMaf(adc_channel_e channel, FunctionalSensor& m) {
	if (!isAdcChannelValid(channel)) {
		return;
	}

	m.setFunction(mafFunction);

	AdcSubscription::SubscribeSensor(m, channel, /*lowpassCutoff =*/50);
	m.Register();
}

void initMaf() {
	initMaf(engineConfiguration->mafAdcChannel, maf);
	initMaf(engineConfiguration->maf2AdcChannel, maf2);
}

void deinitMaf() {
	AdcSubscription::UnsubscribeSensor(maf, engineConfiguration->mafAdcChannel);
	AdcSubscription::UnsubscribeSensor(maf2, engineConfiguration->maf2AdcChannel);
}
