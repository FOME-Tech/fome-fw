#include "sensor_converter_func.h"
#include "biquad.h"

class FlexConverter : public SensorConverter {
public:
	FlexConverter() {
		// Update rate is 50-150hz, so this actually filters at 0.5-1.5hz -3db depending on E%, which is ok
		m_filter.configureLowpass(100, 1);
	}

	// Preload the filter as if we'd been seeing this value for a long time, so that the reported
	// value doesn't slowly ramp up from zero over the first second after startup
	void preloadFilter(float flexPct) {
		m_filter.cookSteadyState(flexPct);
		m_hasUpdated = true;
	}

	SensorResult convert(float frequency) const override {
		// Sensor should only report 50-150hz, significantly outside that range indicates a problem
		// it changes to 200hz+ to indicate methanol "contamination"
		if (frequency < 45) {
			return UnexpectedCode::Low;
		}

		if (frequency > 155) {
			return UnexpectedCode::High;
		}

		float flexPct = clampF(0, frequency - 50, 100);

		// Nothing preloaded the filter, so start it out at whatever the sensor says right now
		if (!m_hasUpdated) {
			m_filter.cookSteadyState(flexPct);
			m_hasUpdated = true;
		}

		return m_filter.filter(flexPct);
	}

private:
	mutable Biquad m_filter;
	mutable bool m_hasUpdated = false;
};
