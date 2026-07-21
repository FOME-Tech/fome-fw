#pragma once

#include "can_sensor.h"
#include "efi_timer.h"

#include "wideband_state_generated.h"

class AemXSeriesWideband final : public CanSensorBase, public wideband_state_s {
public:
	AemXSeriesWideband(uint8_t logicalIndex, SensorType type);

	bool acceptFrame(CanBusIndex busIndex, const CANRxFrame& frame) const override;

	// Refresh timeSinceLastFrame from the alive timer - call periodically, not on frame receipt
	void updateTimeSinceLastFrame();

protected:
	// Dispatches to one of the three decoders below
	void decodeFrame(const CANRxFrame& frame, efitick_t nowNt) override;

	// Decode an actual AEM controller, or a rusEFI controller sending AEM format
	void decodeAemXSeries(const CANRxFrame& frame, efitick_t nowNt);

	// Decode rusEFI custom format
	void decodeRusefiStandard(const CANRxFrame& frame, efitick_t nowNt);
	void decodeRusefiDiag(const CANRxFrame& frame);

private:
	const uint8_t m_logicalIndex;

	// Reset any time a CAN frame is received from this controller, valid lambda or not,
	// so TS can show "is this wideband controller alive and talking" independent of lambda validity.
	Timer m_lastFrameTimer;
};
