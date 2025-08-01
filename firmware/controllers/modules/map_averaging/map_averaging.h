/**
 * @file	map_averaging.h
 *
 * @date Dec 11, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "sensor_converter_func.h"

void initMapAveraging();

// allow smoothing up to number of cylinders
#define MAX_MAP_BUFFER_LENGTH (MAX_CYLINDER_COUNT)

class MapAverager : public StoredValueSensor {
public:
	MapAverager(SensorType type, efidur_t timeout)
		: StoredValueSensor(type, timeout)
	{
	}

	void start(uint8_t cylinderNumber);
	void stop();

	SensorResult submit(float sensorVolts);

	void onSample(float map, uint8_t cylinderNumber);

	void setFunction(SensorConverter& func) {
		m_function = &func;
	}

	void showInfo(const char* sensorName) const override;

private:
	SensorConverter* m_function = nullptr;

	bool m_isAveraging = false;
	size_t m_counter = 0;
	size_t m_lastCounter = 0;
	float m_sum = 0;
	uint8_t m_cylinderNumber = 0;
};

MapAverager& getMapAvg(size_t idx);

class MapAveragingModule : public EngineModule {
public:
	void onConfigurationChange(engine_configuration_s const * previousConfig) override;

	void onFastCallback() override;
	void onEnginePhase(float rpm, const EnginePhaseInfo& phase) override;

	void submitSample(float voltsMap1, float voltsMap2);
};
