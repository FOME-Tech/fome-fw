#include "pch.h"

static uint64_t swap_uint64_t(uint64_t x) {
    x = (x & 0x00000000FFFFFFFF) << 32 | (x & 0xFFFFFFFF00000000) >> 32;
    x = (x & 0x0000FFFF0000FFFF) << 16 | (x & 0xFFFF0000FFFF0000) >> 16;
    x = (x & 0x00FF00FF00FF00FF) << 8  | (x & 0xFF00FF00FF00FF00) >> 8;
    return x;
}

static int32_t GetSignalBits(uint64_t data, bool isLittleEndian, uint8_t bitpos, uint8_t length)
{
	uint64_t mask = (1ULL << length) - 1;

	uint8_t shift;

	if (isLittleEndian)
	{
		// Little endian is easy, just shift by bitpos
		shift = bitpos;
	}
	else
	{
		data = swap_uint64_t(data);

		// I don't love this, but it works!?
		const int8_t shiftCorrection[8] = { -7, -5, -3, -1, 1, 3, 5, 7 };
		shift = 64 - bitpos - length + shiftCorrection[bitpos % 8];
	}

	return static_cast<uint32_t>((data >> shift) & mask);
}

static float ToFloat(uint32_t bits, uint8_t length, bool isSigned)
{
	// if signed, sign extend as necessary
	if (isSigned)
	{
		auto signBit = bits & (1UL << (length - 1));
		uint32_t mask = signBit ? ~((signBit) - 1) : 0;

		bits |= mask;

		// compliant way to reinterpret the bits of an unsigned -> signed
		int32_t signedBits;
		static_assert(sizeof(bits) == sizeof(signedBits));
		memcpy(&signedBits, &bits, sizeof(bits));

		return static_cast<float>(signedBits);
	}
	else
	{
		return static_cast<float>(bits);
	}
}

static uint32_t MakeSignalBits(float value, uint8_t length)
{
	uint32_t mask = (1ULL << length) - 1;
	uint32_t u32Val = static_cast<uint32_t>(value);

	return u32Val & mask;
}

static uint64_t PlaceSignalBits(uint64_t input, bool isLittleEndian, uint8_t bitpos, uint8_t length)
{
	if (isLittleEndian)
	{
		// Little endian is easy, just shift by bitpos
		return input << bitpos;
	}
	else
	{
		// I don't love this, but it works!?
		const int8_t shiftCorrection[8] = { -7, -5, -3, -1, 1, 3, 5, 7 };
		uint8_t shift = 64 - bitpos - length + shiftCorrection[bitpos % 8];

		return swap_uint64_t(input << shift);
	}
}

static constexpr uint32_t CalcProtectionValue(uint32_t packet, uint32_t aliveRollingCount, uint8_t length)
{
	uint32_t mask = (1ULL << length) - 1;

	uint32_t val = ~(packet + aliveRollingCount) + 1;

	return val & mask;
}

// example from GMW8772 pg 7
static_assert(CalcProtectionValue(0x2fec, 2, 14) == 0x1012);

static float Decode(const CANRxFrame& frame, uint8_t bitpos, uint8_t length, bool isSigned)
{
	auto bits = GetSignalBits(frame.data64[0], false, bitpos, length);
	return ToFloat(bits, length, isSigned);
}

static bool Decode(const CANRxFrame& frame, uint8_t bitpos)
{
	auto bits = GetSignalBits(frame.data64[0], false, bitpos, 1);
	return bits != 0;
}

void TractionController::onTractionControlCanRx(const CANRxFrame& frame, efitick_t nowNt) {
	switch (frame.SID) {
		case 0x1f0:	// Mk60 wheel speeds
			wssFront = 
				(Decode(frame, 0, 12, false) + Decode(frame, 16, 12, false)) * (0.5 * 0.03125);
			wssRear = 
				(Decode(frame, 32, 12, false) + Decode(frame, 48, 12, false)) * (0.5 * 0.03125);
			m_wssTimeout.reset(nowNt);
			break;
		case 0x1c3:	// PpeiEngineTorqueStatus2
			engineTorqueActualValid = !Decode(frame, 4);
			engineTorqueActual = Decode(frame, 3, 12, true) * 0.5 - 848;
			driverReqTorqueValid = !Decode(frame, 20);
			driverReqTorque = Decode(frame, 19, 12, true) * 0.5 - 848;
			m_torqueStatus2Timeout.reset(nowNt);
			break;
		case 0x2c3:	// PpeiEngineTorqueStatus3
			engineTorqueMin = Decode(frame, 19, 12, true) * 0.5 - 848;
			engineTorqueMinValid = !Decode(frame, 20);
			engineTorqueMax = Decode(frame, 3, 12, true) * 0.5 - 848;
			engineTorqueMaxValid = !Decode(frame, 4);
			m_torqueStatus3Timeout.reset(nowNt);
			break;
		case 0x1f5:	// PpeiTransmissionGeneralStatus2
			currentGear = frame.data8[0] & 0x0F;
			currentGearValid = !Decode(frame, 4);
			break;
	}
}

void TractionController::sendTorqueRequest(float requestedTorque) {
	m_torqueRequestRollCount = (m_torqueRequestRollCount + 1) & 0x03;

	CANTxFrame frame;
	frame.SID = 0x1c7;
	frame.DLC = 8;
	frame.IDE = CAN_IDE_STD;
	frame.RTR = CAN_RTR_DATA;
	frame.data64[0] = 0;

	uint8_t torqueInterventionType = 0;

	{
		if (requestedTorque > (m_lastRequestedTorque + 5)) {
			torqueInterventionType = 1 << 4;
		} else if (requestedTorque < (m_lastRequestedTorque - 5)) {
			torqueInterventionType = 2 << 4;
		} else {
			// bits are left at 0 for "no change"
		}
	}

	sendPpeiGeneralStatus(torqueInterventionType != 0);

	// Request torque value
	uint32_t torqueRequestValue = MakeSignalBits((requestedTorque + 848) * 2, 12);
	uint32_t torqueRequestPacket = torqueInterventionType << 12 | torqueRequestValue;

	uint32_t torqueRequestProtectionValue = CalcProtectionValue(torqueRequestPacket, m_torqueRequestRollCount, 14);

	// place bits in frame
	frame.data64[0] |= PlaceSignalBits(torqueRequestPacket, true, 5, 14);
	frame.data64[0] |= PlaceSignalBits(torqueRequestProtectionValue, true, 37, 14);
	frame.data64[0] |= PlaceSignalBits(m_torqueRequestRollCount, true, 23, 2);

	// max tq apply rate - Nm/s
	frame.data64[0] |= PlaceSignalBits((800 / 16), true, 53, 6);

	canTransmit(&CAND1, CAN_ANY_MAILBOX, &frame, TIME_MS2I(5));

	m_lastRequestedTorque = requestedTorque;
}

void TractionController::sendPpeiGeneralStatus(bool tractionControlActive) {
	CANTxFrame frame;
	frame.SID = 0x1c7;
	frame.DLC = 8;
	frame.IDE = CAN_IDE_STD;
	frame.RTR = CAN_RTR_DATA;
	frame.data64[0] = 0;

	frame.data8[0] =
		1 << 7 |	// Brake Pedal Driver Applied Pressure Detected Validity = invalid
		1 << 4;		// Vehicle Stability Enhancement Lateral Acceleration Validity = invalid

	frame.data8[3] = 
		1 << 3 |	// Traction Control System Enabled = true
		1 << 2 |	// Traction Control System Driver Intent = enabled
		(tractionControlActive ? (1 << 4) : 0);

	frame.data8[4] =
		7 << 5 |	// Vehicle Dynamics Control System Status = Driver Disabled
		1 << 4; 	// Vehicle Dynamics Yaw Rate Validity = Invalid

	frame.data8[6] =
		1 << 2;		// Vehicle Dynamics Over Under Steer Validity = Invalid

	canTransmit(&CAND1, CAN_ANY_MAILBOX, &frame, TIME_MS2I(5));
}

static float totalRatioForGear(uint8_t gear)
{
	if (gear > 6) {
		return 0;
	}

	float ratio = 0;
	switch (gear) {
		case 1: ratio = 4.40; break;
		case 2: ratio = 2.59; break;
		case 3: ratio = 1.80; break;
		case 4: ratio = 1.34; break;
		case 5: ratio = 1.00; break;
		case 6: ratio = 0.75; break;
	}

	return 4.1 * ratio;
}

static const float minDriverTorqueForControl = 50;
static const float minWssForControl = 20;

static const float slipEnableControl = 7;
static const float slipDisableControl = 3;
static const float slipTarget = 6;

static const float kp = 0;
static const float ki = 0;
static const float kd = 0;

static Hysteresis slipEnableHysteresis;

void TractionController::onFastCallback() {
	slipRate = wssRear - wssFront;

	bool inputsValid = checkInputsValid();

	if (inputsValid) {
		tcActive =
			(wssFront > minWssForControl) &&
			(driverReqTorque > minDriverTorqueForControl) &&
			slipEnableHysteresis.test(slipRate, slipEnableControl, slipDisableControl);

		if (tcActive) {
			// Convert to wheel torque domain so gains are constant in any gear
			float totalGearRatio = totalRatioForGear(currentGear);
			float driverReqAxleTorque = driverReqTorque * totalGearRatio;

			// Do traction control!
			float tcAxleTorque = getTcAxleTorque(driverReqAxleTorque, slipTarget - slipRate);

			// Convert back to engine torque
			float tcEngineTorque = tcAxleTorque / totalGearRatio;

			// clamp to positive torque and no more than driver requested
			torqueRequest = clampF(0, tcEngineTorque, driverReqTorque);
		} else {
			// TC conditions not met, just pass on driver req torque
			m_slipError = 0;
			torqueRequest = driverReqTorque;
		}

		m_wasTcActive = tcActive;

		sendTorqueRequest(torqueRequest);
	} else {
		// do nothing?
	}
}

float TractionController::getTcAxleTorque(float driverReqAxleTorque, float slipError) {
	m_slipError = slipError;

	if (!m_wasTcActive) {
		// TC just started regulation, prime the integrator with driver
		// requested torque to avoid a step as TC engages
		m_integrator = driverReqAxleTorque;
		m_lastSlipError = slipError;
	}

	m_integrator += 0.005 * ki * slipError;
	m_integrator = clampF(0, m_integrator, driverReqAxleTorque);
	// TODO: clamp integrator
	float p = kp * slipError;
	float d = kd * (slipError - m_lastSlipError);

	m_lastSlipError = slipError;

	return p + m_integrator + d;
}

bool TractionController::checkInputsValid() const
{
	// Wheel speeds - nominal ~7ms period
	if (m_wssTimeout.hasElapsedMs(50)) {
		return false;
	}

	// PpeiEngineTorqueStatus2 - nominal 25ms period
	if (m_torqueStatus2Timeout.hasElapsedMs(100)) {
		return false;
	}

	if (!engineTorqueActualValid) {
		return false;
	}

	// PpeiEngineTorqueStatus3 - nominal 50ms period
	if (m_torqueStatus3Timeout.hasElapsedMs(200)) {
		return false;
	}

	if (!engineTorqueMinValid) {
		return false;
	}

	if (!engineTorqueMaxValid) {
		return false;
	}

	// PpeiTransmissionStatus2 - nominal 25ms period
	if (m_transmissionStatus2Timeout.hasElapsedMs(100)) {
		return false;
	}

	if (!currentGearValid) {
		return false;
	}

	// Everything passed!
	return true;
}
