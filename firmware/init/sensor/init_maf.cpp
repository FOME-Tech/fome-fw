#include "pch.h"

#include "init.h"
#include "adc_subscription.h"
#include "functional_sensor.h"
#include "table_func.h"
#include "func_chain.h"
#include "maf_filter.h"

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

SensorResult MafFilter::convert(float input) const {
	engine->outputChannels.mafMeasured_preFilter = input;

	float param = engineConfiguration->mafFilterParameter;
	if (param == 0) {
		return input;
	}

	float rpm = Sensor::getOrZero(SensorType::Rpm);

	if (rpm == 0) {
		m_lastValue = input;
		return input;
	}

	// This runs once per ADC update, not on the fast callback
	constexpr float updatePeriodSec = 1e-3 * loopPeriodMs(ADC_UPDATE_RATE);

	float invTimeConstant = rpm / param;
	float alpha = updatePeriodSec * invTimeConstant;

	if (alpha < 0.001f) {
		// Limit to 0.001 to avoid numerical issues
		alpha = 0.001f;
	} else if (alpha > 0.98f) {
		// alpha > 0.98 (engine very fast and/or small manifold)
		//     -> disable filtering entirely
		m_lastValue = input;
		return input;
	}

	m_lastValue = alpha * input + (1 - alpha) * m_lastValue;

	return m_lastValue;
}

static FuncChain<MafVoltageCheck, MafTable, MafFilter> mafFunction;

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
