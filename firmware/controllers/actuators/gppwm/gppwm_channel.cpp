
#include "pch.h"

#include "gppwm_channel.h"

#include "ac_control.h"
#include "boost_control.h"
#include "table_helper.h"
#include <rusefi/expected.h>

static expected<float> readBoostTarget() {
#if EFI_ENGINE_CONTROL
	// Only closed loop boost control has a target
	if (!engineConfiguration->isBoostControlEnabled || engineConfiguration->boostType != CLOSED_LOOP) {
		return unexpected;
	}

	return engine->module<BoostController>()->getLastTarget();
#else
	return unexpected;
#endif
}

expected<float> readGppwmChannel(gppwm_channel_e channel) {
	switch (channel) {
		case GPPWM_Zero:
			return 0;
		case GPPWM_Rpm:
			return Sensor::get(SensorType::Rpm);
		case GPPWM_Tps:
			return Sensor::get(SensorType::Tps1);
		case GPPWM_Map:
			return Sensor::get(SensorType::Map);
		case GPPWM_Clt:
			return Sensor::get(SensorType::Clt);
		case GPPWM_Iat:
			return Sensor::get(SensorType::Iat);
		case GPPWM_LuaGauge1:
			return Sensor::get(SensorType::LuaGauge1);
		case GPPWM_LuaGauge2:
			return Sensor::get(SensorType::LuaGauge2);
		case GPPWM_LuaGauge3:
			return Sensor::get(SensorType::LuaGauge3);
		case GPPWM_LuaGauge4:
			return Sensor::get(SensorType::LuaGauge4);
		case GPPWM_LuaGauge5:
			return Sensor::get(SensorType::LuaGauge5);
		case GPPWM_LuaGauge6:
			return Sensor::get(SensorType::LuaGauge6);
		case GPPWM_LuaGauge7:
			return Sensor::get(SensorType::LuaGauge7);
		case GPPWM_LuaGauge8:
			return Sensor::get(SensorType::LuaGauge8);
		case GPPWM_FuelLoad:
			return getFuelingLoad();
		case GPPWM_IgnLoad:
			return getIgnitionLoad();
		case GPPWM_AuxTemp1:
			return Sensor::get(SensorType::AuxTemp1);
		case GPPWM_AuxTemp2:
			return Sensor::get(SensorType::AuxTemp2);
		case GPPWM_AccelPedal:
			return Sensor::get(SensorType::AcceleratorPedal);
		case GPPWM_Vbatt:
			return Sensor::get(SensorType::BatteryVoltage);
#if EFI_SHAFT_POSITION_INPUT
		case GPPWM_VVT_1I:
			return engine->triggerCentral.getVVTPosition(/*bankIndex*/ 0, /*camIndex*/ 0);
		case GPPWM_VVT_1E:
			return engine->triggerCentral.getVVTPosition(/*bankIndex*/ 0, /*camIndex*/ 1);
		case GPPWM_VVT_2I:
			return engine->triggerCentral.getVVTPosition(/*bankIndex*/ 1, /*camIndex*/ 0);
		case GPPWM_VVT_2E:
			return engine->triggerCentral.getVVTPosition(/*bankIndex*/ 1, /*camIndex*/ 1);
#else
		case GPPWM_VVT_1I:
		case GPPWM_VVT_1E:
		case GPPWM_VVT_2I:
		case GPPWM_VVT_2E:
			return 0;
#endif // EFI_SHAFT_POSITION_INPUT
		case GPPWM_EthanolPercent:
			return Sensor::get(SensorType::FuelEthanolPercent);
		case GPPWM_AuxLinear1:
			return Sensor::get(SensorType::AuxLinear1);
		case GPPWM_AuxLinear2:
			return Sensor::get(SensorType::AuxLinear2);
		case GPPWM_AuxLinear3:
			return Sensor::get(SensorType::AuxLinear3);
		case GPPWM_AuxLinear4:
			return Sensor::get(SensorType::AuxLinear4);
		case GPPWM_GppwmOutput1:
			return (float)engine->outputChannels.gppwmOutput[0];
		case GPPWM_GppwmOutput2:
			return (float)engine->outputChannels.gppwmOutput[1];
		case GPPWM_GppwmOutput3:
			return (float)engine->outputChannels.gppwmOutput[2];
		case GPPWM_GppwmOutput4:
			return (float)engine->outputChannels.gppwmOutput[3];
		case GPPWM_DetectedGear:
			return Sensor::get(SensorType::DetectedGear);
		case GPPWM_BaroPressure:
			return Sensor::get(SensorType::BarometricPressure);
		case GPPWM_Egt1:
			return engine->outputChannels.egt[0];
		case GPPWM_Egt2:
			return engine->outputChannels.egt[1];
		case GPPWM_VehicleSpeed:
			return Sensor::getOrZero(SensorType::VehicleSpeed);
		case GPPWM_OilPressure:
			return Sensor::get(SensorType::OilPressure);
		case GPPWM_OilTemp:
			return Sensor::get(SensorType::OilTemperature);
		case GPPWM_AcState:
			return 100.0f * engine->module<AcController>()->acCompressorState;
		case GPPWM_ClutchPosition:
			return Sensor::get(SensorType::ClutchPosition);
		case GPPWM_FuelTemp:
			return Sensor::get(SensorType::FuelTemperature);
		case GPPWM_FuelLevel:
			return Sensor::get(SensorType::FuelLevel);
		case GPPWM_EcuTemp:
			return Sensor::get(SensorType::EcuInternalTemperature);
		case GPPWM_AuxSpeed1:
			return Sensor::get(SensorType::AuxSpeed1);
		case GPPWM_AuxSpeed2:
			return Sensor::get(SensorType::AuxSpeed2);
		case GPPWM_AccelLat:
			return Sensor::get(SensorType::AccelLat);
		case GPPWM_AccelLon:
			return Sensor::get(SensorType::AccelLon);
		case GPPWM_BoostTarget:
			return readBoostTarget();
		case GPPWM_BoostError: {
			// Positive when below target, same sign as the boost PID error
			auto target = readBoostTarget();
			auto map = Sensor::get(SensorType::Map);
			if (!target || !map) {
				return unexpected;
			}

			return target.Value - map.Value;
		}
		case GPPWM_FuelPressure:
			return Sensor::get(SensorType::FuelPressureLow);
		case GPPWM_TurboSpeed: {
			// In thousands of RPM, raw RPM would overflow the axis bins
			auto rpm = Sensor::get(SensorType::TurbochargerSpeed);
			if (!rpm) {
				return unexpected;
			}

			return rpm.Value / 1000.0f;
		}
	}

	return unexpected;
}

float GppwmChannel::setOutput(float result) {
	// Not init yet, nothing to do.
	if (!m_config) {
		return result;
	}

	if (m_usePwm) {
		efiAssert(ObdCode::OBD_PCM_Processor_Fault, m_usePwm, "m_usePwm null", 0);
		m_pwm->setSimplePwmDutyCycle(std::clamp<float>(result * 0.01f, 0, 1));

		return result;
	} else {
		efiAssert(ObdCode::OBD_PCM_Processor_Fault, m_output, "m_output null", 0);
		if (m_config->offBelowDuty > m_config->onAboveDuty) {
			firmwareError(
					ObdCode::CUSTOM_ERR_6122,
					"You can't have off below %d greater than on above %d",
					m_config->offBelowDuty,
					m_config->onAboveDuty);
		}
		// Apply hysteresis with provided values
		if (m_state && result < m_config->offBelowDuty) {
			m_state = false;
		} else if (!m_state && result > m_config->onAboveDuty) {
			m_state = true;
		}

		m_output->setValue(m_state);

		// Return the actual output value with hysteresis
		return m_state ? 100 : 0;
	}
}

void GppwmChannel::init(
		bool usePwm, IPwm* pwm, OutputPin* outputPin, const ValueProvider3D* table, const gppwm_channel* cfg) {
	m_usePwm = usePwm;
	m_pwm = pwm;
	m_output = outputPin;
	m_table = table;
	m_config = cfg;
}

GppwmResult GppwmChannel::getOutput() const {
	expected<float> xAxisValue = readGppwmChannel(m_config->rpmAxis);
	expected<float> yAxisValue = readGppwmChannel(m_config->loadAxis);

	GppwmResult result{(float)m_config->dutyIfError, xAxisValue.value_or(0), yAxisValue.value_or(0)};

	// If we couldn't get load axis value, fall back on error value
	if (!xAxisValue || !yAxisValue) {
		return result;
	}

	float resultVal = m_table->getValue(xAxisValue.Value, yAxisValue.Value);

	if (std::isnan(result.Result)) {
		return result;
	}

	result.Result = resultVal;
	return result;
}

GppwmResult GppwmChannel::update() {
	// Without a config, nothing to do.
	if (!m_config) {
		return {};
	}

	auto output = getOutput();
	output.Result = setOutput(output.Result);

	return output;
}
