#include "pch.h"

#include "lua_getchannel.h"
#include "fuel_math.h"

expected<float> getChannelByName(const char* name) {
	switch (djb2lowerCase(name)) {
		case djb2lowerCase("FuelFlow"):
			return engine->module<TripOdometer>()->getConsumptionGramPerSecond();
		case djb2lowerCase("AFR"):
			return (Sensor::getOrZero(SensorType::Lambda1) * engineConfiguration->stoichRatioPrimary);
		case djb2lowerCase("InjectorDutyCycle"):
			return getInjectorDutyCycle(Sensor::getOrZero(SensorType::Rpm));
		case djb2lowerCase("InjectorPulseWidth"):
			return static_cast<float>(engine->outputChannels.actualLastInjection);
		default:
			return unexpected;
	}
}
