#include "pch.h"

#include "init.h"
#include "functional_sensor.h"
#include "flex_sensor.h"
#include "fuel_computer.h"

#include "digital_input_exti.h"

static FunctionalSensor flexSensor(SensorType::FuelEthanolPercent, MS2NT(30000));
static StoredValueSensor flexFuelTemp(SensorType::FuelTemperature, MS2NT(30000));
static FlexConverter converter;

static Biquad flexTempFilter;

static Timer flexFreq, flexPulse;

static void flexCallback(void*, efitick_t nowNt, bool value) {
	if (value) {
		// End pulse timing on rising edge
		float pulseWidthUs = flexPulse.getElapsedUs(nowNt);

		if (pulseWidthUs < 900) {
			flexFuelTemp.invalidate(UnexpectedCode::Low);
		} else if (pulseWidthUs > 5100) {
			flexFuelTemp.invalidate(UnexpectedCode::High);
		} else {
			// -40C = 1000us
			// 125C = 5000us
			float tempC = interpolateClamped(1000, -40, 5000, 125, pulseWidthUs);
			tempC = flexTempFilter.filter(tempC);
			flexFuelTemp.setValidValue(tempC, nowNt);
		}
	} else {
		float frequency = 1 / flexFreq.getElapsedSecondsAndReset(nowNt);
		flexSensor.postRawValue(frequency, nowNt);

		// Start timing pulse width on falling edge
		flexPulse.reset(nowNt);
	}
}

static Gpio flexPin = Gpio::Unassigned;

// https://rusefi.com/forum/viewtopic.php?p=37452&sid=829804c90d5b2e1fecd1b900cf1b1811#p37452

static bool hasAnalogFuelTemp() {
	return isAdcChannelValid(engineConfiguration->fuelTempSensor.adcChannel);
}

void initFlexSensor() {
	auto pin = engineConfiguration->flexSensorPin;
	if (!isBrainPinValid(pin)) {
		return;
	}

	// Already running on this pin: carry on with the filters as they are
	if (pin != flexPin) {
		flexPin = pin;

		// 0.01 means filter bandwidth of ~1hz with ~100hz sensor
		flexTempFilter.configureLowpass(1, 0.01f);
		flexSensor.setFunction(converter);

		// Start the flex filter out at the last ethanol content we saw, so that fueling is correct
		// immediately at startup instead of ramping up as the filter settles
		if (auto storedFlex = getStoredFlexEthanolPercent()) {
			converter.preloadFilter(storedFlex.Value);
		}

#if EFI_PROD_CODE
		efiExtiEnablePin("flex", flexPin, PAL_EVENT_MODE_BOTH_EDGES, flexCallback, nullptr);
#endif // EFI_PROD_CODE
	}

	flexSensor.Register();

	// If an analog fuel temp sensor is configured, don't use the flex sensor for fuel temp
	if (!hasAnalogFuelTemp()) {
		flexFuelTemp.Register();
	}
}

void deInitFlexSensor() {
	auto newPin = engineConfiguration->flexSensorPin;

	// Make way for an analog fuel temperature sensor
	if (newPin != flexPin || hasAnalogFuelTemp()) {
		flexFuelTemp.unregister();
	}

	if (!isBrainPinValid(flexPin) || newPin == flexPin) {
		return;
	}

	flexSensor.unregister();

#if EFI_PROD_CODE
	efiExtiDisablePin(flexPin);
#endif

	flexPin = Gpio::Unassigned;
}
