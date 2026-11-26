/**
 * @file frequency_sensor.h
 */
#include "functional_sensor.h"
#include "efi_timer.h"
#include "biquad.h"

class FrequencySensor : public FunctionalSensor {
public:
	FrequencySensor(SensorType type, efidur_t timeoutPeriod)
		: FunctionalSensor(type, timeoutPeriod) {}

	// Starts on the pin, or carries on if it's already running on it (updating the filter)
	void initIfValid(brain_pin_e pin, SensorConverter& converter, float filterParameter);
	// Releases the pin and unregisters, unless it's running on keepPin
	void deInit(brain_pin_e keepPin = Gpio::Unassigned);

	// sad workaround: we are not good at BiQuad configuring
	bool useBiQuad = true;

	void showInfo(const char* sensorName) const override;

	void onEdge(efitick_t nowNt);

	int eventCounter = 0;

private:
	Timer m_edgeTimer;
	brain_pin_e m_pin = Gpio::Unassigned;

	Biquad m_filter;
	float m_filterParameter = 0;
};
