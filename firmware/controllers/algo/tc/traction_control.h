#pragma once

#include "traction_control_generated.h"

class TractionController : public EngineModule, public traction_control_s {
public:
	void onFastCallback() override;

	void onTractionControlCanRx(const CANRxFrame& frame, efitick_t nowNt);

private:
	bool checkInputsValid() const;
	float getTcAxleTorque(float driverReqAxleTorque, float slipError);
	void sendTorqueRequest(float requestedTorque);
	void sendPpeiGeneralStatus(bool tractionControlActive);

	Timer m_torqueStatus2Timeout;
	Timer m_torqueStatus3Timeout;
	Timer m_transmissionStatus2Timeout;
	Timer m_wssTimeout;

	uint8_t m_torqueRequestRollCount;
	float m_lastRequestedTorque;

	float m_integrator;
	float m_lastSlipError;
	bool m_wasTcActive = false;
};
