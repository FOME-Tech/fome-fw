#include "lua_getchannel_hashes.h"
#include "fuel_math.h"

float getChannelByName(const char *name)
{
	auto hash = djb2lowerCase(name);

	switch (hash)
	{
	case djb2lowerCase("FuelFlow"):
		return engine->module<TripOdometer>()->getConsumptionGramPerSecond();
	case djb2lowerCase("AFR"):
		return (Sensor::getOrZero(SensorType::Lambda1) * engineConfiguration->stoichRatioPrimary);
	case djb2lowerCase("InjectorDutyCycle"):
		return getInjectorDutyCycle(Sensor::getOrZero(SensorType::Rpm));
	case djb2lowerCase("InjectorPulseWidth"):
		return engine->outputChannels.actualLastInjection;
	default:
		return 0.0f;
	}

	return 0.0f;
}