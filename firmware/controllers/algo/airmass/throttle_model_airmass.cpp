#include "pch.h"

#include "throttle_model_airmass.h"

AirmassResult ThrottleModelAirmass::getAirmass(int rpm, bool postState) {
	auto map = Sensor::get(SensorType::Map);
	auto tps = Sensor::get(SensorType::Tps1);
	
	if (!map || !tps) {
		return {};
	}

	auto throttleMaf = engine->module<ThrottleModel>()->estimateThrottleFlow(map.Value, tps.Value);

	if (throttleMaf) {
		return getAirmassImpl(throttleMaf.Value, rpm, postState);
	} else {
		return {};
	}
}
