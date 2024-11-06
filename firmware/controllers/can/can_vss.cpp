/**
 * @file	can_vss.cpp
 *
 * This file handles incoming vss values from can.
 *
 * @date Apr 19, 2020
 * @author Alex Miculescu, (c) 2020
 */

#include "pch.h"

#if EFI_CAN_SUPPORT
#include "can.h"
#include "dynoview.h"
#include "stored_value_sensor.h"

static bool isInit = false;

static int getTwoBytesLsb(const CANRxFrame& frame, int index) {
	uint8_t low = frame.data8[index];
	uint8_t high = frame.data8[index + 1] & 0x0F;
	return low | (high << 8);
}

/* Module specific processing functions */
/* source: http://z4evconversion.blogspot.com/2016/07/completely-forgot-but-it-does-live-on.html */
expected<float> processBMW_e46(const CANRxFrame& frame) {
	// BMW e46 ABS Message
	if (CAN_SID(frame) != 0x1F0) {
		return unexpected;
	}

	// average the rear wheels since those are the driven ones (more accurate gear detection!)
	uint16_t left =  getTwoBytesLsb(frame, 4);
	uint16_t right = getTwoBytesLsb(frame, 6);

	return (left + right) / (16 * 2);
}

expected<float> processBMW_e90Vss(const CANRxFrame& frame) {
	// BMW E90 ABS speed frame (not wheel speeds, vehicle speed)
	if (CAN_SID(frame) != 0x1A0) {
		return unexpected;
	}

	uint8_t low = frame.data8[0];
	uint8_t high = frame.data8[1] * 0x0F;

	return 0.1f * (low | (high << 8));
}

expected<float> processW202(const CANRxFrame& frame) {
	// W202 C180 ABS signal
	if (CAN_SID(frame) != 0x0200) {
		return unexpected;
	}

	uint16_t tmp = (frame.data8[2] << 8);
	tmp |= frame.data8[3];
	return tmp * 0.0625;
}

/* End of specific processing functions */

expected<float> tryDecodeVss(can_vss_nbc_e type, const CANRxFrame& frame) {
	switch (type) {
		case BMW_e46:
			return processBMW_e46(frame);
		case BMW_e90:
			return processBMW_e90Vss(frame);
		case W202:
			return processW202(frame);
		default:
			return unexpected;
	}
}

struct WssResult {
	float lf;
	float rf;
	float lr;
	float rr;
};

static constexpr float E90Wss(const uint8_t& data)
{
	return (*reinterpret_cast<const int16_t*>(&data)) * 0.0625f;
}

expected<WssResult> processBMW_e90Wss(const CANRxFrame& frame) {
	// E90 Wheel speed frame
	if (CAN_SID(frame) != 0x0ce) {
		return unexpected;
	}

	return WssResult{
		E90Wss(frame.data8[0]),
		E90Wss(frame.data8[2]),
		E90Wss(frame.data8[4]),
		E90Wss(frame.data8[6])
	};
}

expected<WssResult> tryDecodeWss(can_vss_nbc_e type, const CANRxFrame& frame) {
	switch (type) {
		case BMW_e90:
			return processBMW_e90Wss(frame);
		default:
			return unexpected;
	}
}

static StoredValueSensor canSpeed(SensorType::VehicleSpeed, MS2NT(500));
static StoredValueSensor wssLf(SensorType::WheelSpeedLF, MS2NT(500));
static StoredValueSensor wssRf(SensorType::WheelSpeedLF, MS2NT(500));
static StoredValueSensor wssLr(SensorType::WheelSpeedLF, MS2NT(500));
static StoredValueSensor wssRr(SensorType::WheelSpeedLF, MS2NT(500));

void processCanRxVss(const CANRxFrame& frame, efitick_t nowNt) {
	if (!engineConfiguration->enableCanVss || !isInit) {
		return;
	}

	auto type = engineConfiguration->canVssNbcType;

	if (auto speed = tryDecodeVss(type, frame)) {
		canSpeed.setValidValue(speed.Value * engineConfiguration->canVssScaling, nowNt);

#if EFI_DYNO_VIEW
		updateDynoViewCan();
#endif
	}

	if (auto wss = tryDecodeWss(type, frame)) {
		wssLf.setValidValue(wss.Value.lf * engineConfiguration->canVssScaling, nowNt);
		wssRf.setValidValue(wss.Value.rf * engineConfiguration->canVssScaling, nowNt);
		wssLr.setValidValue(wss.Value.lr * engineConfiguration->canVssScaling, nowNt);
		wssRr.setValidValue(wss.Value.rr * engineConfiguration->canVssScaling, nowNt);
	}
}

void initCanVssSupport() {
	isInit = false;

	if (engineConfiguration->enableCanVss) {
		auto type = engineConfiguration->canVssNbcType;
		if (type < CanVssLast) {
			canSpeed.Register();
			wssLf.Register();
			wssRf.Register();
			wssLr.Register();
			wssRr.Register();
			isInit = true;
		} else {
			firmwareError(ObdCode::OBD_Vehicle_Speed_SensorB, "Wrong Can DBC selected: %d", type);
		}
	}
}

void setCanVss(int type) {
	engineConfiguration->canVssNbcType = (can_vss_nbc_e)type;
}

#endif // EFI_CAN_SUPPORT
