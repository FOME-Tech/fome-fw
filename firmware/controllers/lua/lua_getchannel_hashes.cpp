#include "lua_getchannel_hashes.h"
#include <array>
#include "fuel_math.h"

float getChannelByName(const char *name)
{
	auto hash = djb2lowerCase(name);

	switch (hash)
	{
	case djb2lowerCase("TPS"):
		return Sensor::getOrZero(SensorType::Tps1);
	case djb2lowerCase("FuelFlow"):
		return engine->module<TripOdometer>()->getConsumptionGramPerSecond();
	case djb2lowerCase("Lambda"):
		return Sensor::getOrZero(SensorType::Lambda1);
	case djb2lowerCase("OilPressure"):
		return Sensor::getOrZero(SensorType::OilPressure);
	case djb2lowerCase("OilTemperature"):
		return Sensor::getOrZero(SensorType::OilTemperature);
	case djb2lowerCase("BatteryVoltage"):
		return Sensor::getOrZero(SensorType::BatteryVoltage);
	case djb2lowerCase("AFR"):
		return (Sensor::getOrZero(SensorType::Lambda1) * engineConfiguration->stoichRatioPrimary);
	case djb2lowerCase("VehicleSpeed"):
		return Sensor::getOrZero(SensorType::VehicleSpeed);
	case djb2lowerCase("Clt"):
		return Sensor::getOrZero(SensorType::Clt);
	case djb2lowerCase("InjectorDutyCycle"):
		return getInjectorDutyCycle(Sensor::getOrZero(SensorType::Rpm));
	default:
		return 0.0f;
	}

	return 0.0f;
}