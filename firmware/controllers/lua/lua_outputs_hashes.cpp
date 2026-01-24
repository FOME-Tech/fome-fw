#include "lua_outputs_hashes.h"
#include <array>
#include "fuel_math.h"

static constexpr std::array<char const*, 10> outputNames = {
	"TPS",
	"FuelFlow",
	"Lambda",
	"OilPressure",
	"OilTemp",
	"BatteryVoltage",
	"AFR",
	"VSS",
	"CoolantTemp",
	"InjectorDutyCycle"
};

constexpr auto makeOutputHashes() {
	std::array<int, 10> result = {};

	for (size_t i = 0; i < result.size(); i++) {
		result[i] = djb2lowerCase(outputNames[i]);
	}

	return result;
}

static constexpr auto outputHashes = makeOutputHashes();

float getOuputValueByName(const char* name) {
	auto hash = djb2lowerCase(name);

	for (size_t i = 0; i < outputHashes.size(); i++) {
		if (hash == outputHashes[i]) {
			switch (i) {
				case 0: return Sensor::getOrZero(SensorType::Tps1);
				case 1: return engine->module<TripOdometer>()->getConsumptionGramPerSecond();
				case 2: return Sensor::getOrZero(SensorType::Lambda1);
				case 3: return Sensor::getOrZero(SensorType::OilPressure);
				case 4: return Sensor::getOrZero(SensorType::OilTemperature);
				case 5: return Sensor::getOrZero(SensorType::BatteryVoltage);
				case 6: return (Sensor::getOrZero(SensorType::Lambda1) * engineConfiguration->stoichRatioPrimary);
				case 7: return Sensor::getOrZero(SensorType::VehicleSpeed);
				case 8: return Sensor::getOrZero(SensorType::Clt);
				case 9: return getInjectorDutyCycle(Sensor::getOrZero(SensorType::Rpm));
				default: return 0.0f;
			}
		}
	}

	return 0.0f;
}