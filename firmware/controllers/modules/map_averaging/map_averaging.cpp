/**
 * @file	map_averaging.cpp
 *
 * In order to have best MAP estimate possible, we real MAP value at a relatively high frequency
 * and average the value within a specified angle position window for each cylinder
 *
 * @date Dec 11, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"

#include "trigger_central.h"

// not have a real physical pin - it's only used for engine sniffer
static NamedOutputPin mapAveragingPin("map");

// allow smoothing up to number of cylinders
#define MAX_MAP_BUFFER_LENGTH (MAX_CYLINDER_COUNT)
// in MAP units, not voltage!
static float averagedMapRunningBuffer[MAX_MAP_BUFFER_LENGTH];
int mapMinBufferLength = 0;
static int averagedMapBufIdx = 0;

/**
 * here we have averaging start and averaging end points for each cylinder
 */
struct sampler {
	scheduling_s timer;
	uint8_t cylinderNumber;
};

static CCM_OPTIONAL sampler samplers[MAX_CYLINDER_COUNT];

static void endAveraging(MapAverager* arg);

static size_t currentMapAverager = 0;

static void startAveraging(sampler* s) {
	float duration = engine->engineState.mapAveragingDuration;
	if (duration == 0) {
		// Zero duration means the engine wasn't spinning or something, abort
		return;
	}

	// TODO: set currentMapAverager based on cylinder bank
	auto& averager = getMapAvg(currentMapAverager);
	averager.start(s->cylinderNumber);

	mapAveragingPin.setHigh();

	scheduleByAngle(&s->timer, getTimeNowNt(), duration,
		{ endAveraging, &averager });
}

void MapAverager::showInfo(const char* sensorName) const {
	const auto value = get();
	efiPrintf("Sensor \"%s\" is MAP averager: valid: %s value: %.2f averaged sample count: %d", sensorName, boolToString(value.Valid), value.Value, m_lastCounter);
}

void MapAverager::start(uint8_t cylinderNumber) {
	chibios_rt::CriticalSectionLocker csl;

	m_counter = 0;
	m_sum = 0;
	m_isAveraging = true;
	m_cylinderNumber = cylinderNumber;
}

SensorResult MapAverager::submit(float volts) {
	auto result = m_function ? m_function->convert(volts) : unexpected;

	if (m_isAveraging && result) {
		chibios_rt::CriticalSectionLocker csl;

		m_counter++;
		m_sum += result.Value;
	}

	return result;
}

void MapAverager::stop() {
	chibios_rt::CriticalSectionLocker csl;

	m_isAveraging = false;

	engine->outputChannels.mapAveragingSamples = m_counter;

	if (m_counter > 0) {
		float averageMap = m_sum / m_counter;
		m_lastCounter = m_counter;

		onSample(averageMap, m_cylinderNumber);
	} else {
#if EFI_PROD_CODE
		warning(ObdCode::CUSTOM_UNEXPECTED_MAP_VALUE, "No MAP values");
#endif
	}
}

void MapAverager::onSample(float map, uint8_t cylinderNumber) {
	if (cylinderNumber < efi::size(engine->engineState.mapPerCylinder)) {
		engine->engineState.mapPerCylinder[cylinderNumber] = map;

		// correct the reading by this cylinder's MAP offset
		map -= engine->engineState.mapCylinderBalance[cylinderNumber];
	}

	// TODO: this should be per-sensor, not one for all MAP sensors
	averagedMapRunningBuffer[averagedMapBufIdx] = map;
	// increment circular running buffer index
	averagedMapBufIdx = (averagedMapBufIdx + 1) % mapMinBufferLength;
	// find min. value (only works for pressure values, not raw voltages!)
	float minPressure = averagedMapRunningBuffer[0];
	for (int i = 1; i < mapMinBufferLength; i++) {
		if (averagedMapRunningBuffer[i] < minPressure) {
			minPressure = averagedMapRunningBuffer[i];
		}
	}

	setValidValue(minPressure, getTimeNowNt());
}

void EngineState::updateMapCylinderOffsets() {
	// First pass: compute average MAP for all cylinders
	auto cylCount = engineConfiguration->cylindersCount;

	float avgMap = 0;
	for (int i = 0; i < cylCount; i++) {
		avgMap += mapPerCylinder[i];
	}

	avgMap /= cylCount;

	// Second pass: calculate deviation of each cylinder from the average
	for (int i = 0; i < cylCount; i++) {
		mapCylinderBalance[i] = mapPerCylinder[i] - avgMap;
	}
}

/**
 * This method is invoked from ADC callback.
 * @note This method is invoked OFTEN, this method is a potential bottleneck - the implementation should be
 * as fast as possible
 */
void MapAveragingModule::submitSample(float voltsMap1, float /*voltsMap2*/) {
	SensorResult mapResult = getMapAvg(currentMapAverager).submit(voltsMap1);

	float instantMap = mapResult.value_or(0);
#if EFI_TUNER_STUDIO
	engine->outputChannels.instantMAPValue = instantMap;
#endif // EFI_TUNER_STUDIO
}

static void endAveraging(MapAverager* arg) {
	arg->stop();

	mapAveragingPin.setLow();
}

static void applyMapMinBufferLength() {
	// check range
	mapMinBufferLength = maxI(minI(engineConfiguration->mapMinBufferLength, MAX_MAP_BUFFER_LENGTH), 1);
	// reset index
	averagedMapBufIdx = 0;
	// fill with maximum values
	for (int i = 0; i < mapMinBufferLength; i++) {
		averagedMapRunningBuffer[i] = FLT_MAX;
	}
}

void MapAveragingModule::onFastCallback() {
	engine->engineState.updateMapCylinderOffsets();

	float rpm = Sensor::getOrZero(SensorType::Rpm);

	MAP_sensor_config_s * c = &engineConfiguration->map;

	angle_t start = interpolate2d(rpm, c->samplingAngleBins, c->samplingAngle);
	efiAssertVoid(ObdCode::CUSTOM_ERR_MAP_START_ASSERT, !std::isnan(start), "start");

	for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
		float cylinderStart = start + engine->cylinders[i].getAngleOffset();
		wrapAngle(cylinderStart, "cylinderStart", ObdCode::CUSTOM_ERR_6562);
		engine->engineState.mapAveragingStart[i] = cylinderStart;
	}

	angle_t duration = interpolate2d(rpm, c->samplingWindowBins, c->samplingWindow);
	assertAngleRange(duration, "samplingDuration", ObdCode::CUSTOM_ERR_6563);

	// Clamp the duration to slightly less than one cylinder period
	float cylinderPeriod = engine->engineState.engineCycle / engineConfiguration->cylindersCount;
	engine->engineState.mapAveragingDuration = clampF(10, duration, cylinderPeriod - 10);
}

// Callback to schedule the start of map averaging for each cylinder
void MapAveragingModule::onEnginePhase(float /*rpm*/, const EnginePhaseInfo& phase) {
	if (!engineConfiguration->isMapAveragingEnabled) {
		return;
	}

	ScopePerf perf(PE::MapAveragingTriggerCallback);

	int samplingCount = engineConfiguration->measureMapOnlyInOneCylinder ? 1 : engineConfiguration->cylindersCount;

	for (int i = 0; i < samplingCount; i++) {
		angle_t samplingStart = engine->engineState.mapAveragingStart[i];

		if (!isPhaseInRange(EngPhase{samplingStart}, phase)) {
			continue;
		}

		float angleOffset = samplingStart - phase.currentEngPhase.angle;
		if (angleOffset < 0) {
			angleOffset += engine->engineState.engineCycle;
		}

		auto& s = samplers[i];
		scheduleByAngle(&s.timer, phase.timestamp, angleOffset, { startAveraging, &s });
	}
}

void MapAveragingModule::onConfigurationChange(engine_configuration_s const * previousConfig) {
	if (!previousConfig || engineConfiguration->mapMinBufferLength != previousConfig->mapMinBufferLength) {
		applyMapMinBufferLength();
	}
}

void initMapAveraging() {
	for (size_t i = 0; i < efi::size(samplers); i++) {
		samplers[i].cylinderNumber = i;
	}

	applyMapMinBufferLength();
}
