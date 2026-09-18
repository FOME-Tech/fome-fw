#include "pch.h"

// Hysteresis between gears: the measured ratio has to move this far past the boundary between two
// gears before we'll leave the gear we're currently holding. Without this, a ratio hovering near a
// boundary (miscalibrated gear ratio, wrong tire size, noisy VSS) chatters between two gears.
static constexpr float gearHysteresis = 0.03f;

// A gear we're sitting exactly on needs only the minimum dwell: when the driveline is locked up the
// measured ratio *is* that gear's ratio, so there's nothing left to wait for. The further the
// measurement sits from any real gear, the longer it has to persist before we believe it.
static constexpr float snapDeviation = 0.02f;
static constexpr float ambiguousDeviation = 0.08f;

// Even a perfect match has to hold still for a moment: a ratio sweeping across the gears mid-shift
// (an over-blipped 4-3 downshift passing through 2nd, say) crosses each gear's exact ratio on the
// way past, and we don't want to publish the gears it merely drove through.
static constexpr float minDwellSeconds = 0.1f;
static constexpr float maxDwellSeconds = 0.3f;

static constexpr float geometricMean(float x, float y) {
	return sqrtf(x * y);
}

// How long a candidate gear has to persist, given how well the ratio matches it
static float requiredDwellSeconds(float deviation) {
	return interpolateClamped(snapDeviation, minDwellSeconds, ambiguousDeviation, maxDwellSeconds, deviation);
}

GearDetector::GearDetector()
	: Sensor(SensorType::DetectedGear) {}

GearDetector::~GearDetector() {
	unregister();
}

void GearDetector::onConfigurationChange(engine_configuration_s const* /*previousConfig*/) {
	// Compute gear thresholds between gears

	uint8_t gearCount = engineConfiguration->totalGearsCount;

	if (gearCount == 0) {
		// No gears, nothing to do here.
		return;
	}

	if (gearCount > GEARS_COUNT) {
		firmwareError("too many gears");
		return;
	}

	// validate gears
	for (size_t i = 0; i < gearCount; i++) {
		if (engineConfiguration->gearRatio[i] <= 0) {
			firmwareError("Invalid gear ratio for #%d", i + 1);
			return;
		}
	}

	for (int i = 0; i < gearCount - 1; i++) {
		// Threshold i is the threshold between gears i and i+1
		float gearI = engineConfiguration->gearRatio[i];
		float gearIplusOne = engineConfiguration->gearRatio[i + 1];

		if (gearI <= gearIplusOne) {
			firmwareError("Invalid gear ordering near gear #%d", i + 1);
		}

		m_gearThresholds[i] = geometricMean(gearI, gearIplusOne);
	}

	// The gear we were holding was decided against the old ratios, start over
	m_currentGear = 0;
	m_candidateGear = 0;
	m_candidateTimer.reset();

	Register();
}

void GearDetector::onSlowCallback() {
	float ratio = computeGearboxRatio();
	m_gearboxRatio = ratio;

	size_t candidate = determineGearFromRatio(ratio, m_currentGear);

	if (candidate == m_currentGear) {
		// Still in the gear we already believe we're in, nothing to confirm.
		m_candidateGear = candidate;
		m_candidateTimer.reset();
	} else {
		if (candidate != m_candidateGear) {
			// Something other than what we were considering, start the clock over. This is what
			// rejects transient gears during a shift: the ratio has to stop somewhere, not just
			// pass through.
			m_candidateGear = candidate;
			m_candidateTimer.reset();
		}

		// Dwell is based on how well the ratio matches right now, so a shift that settles onto its
		// new gear is believed as soon as it's settled, not after some fixed timeout.
		float dwell = requiredDwellSeconds(gearRatioDeviation(ratio, candidate));

		if (m_candidateTimer.hasElapsedSec(dwell)) {
			m_currentGear = candidate;
		}
	}

	m_gearDeviation = gearRatioDeviation(ratio, m_currentGear);
}

size_t GearDetector::determineGearFromRatio(float ratio) const {
	return determineGearFromRatio(ratio, 0);
}

size_t GearDetector::determineGearFromRatio(float ratio, size_t stickyGear) const {
	auto gearCount = engineConfiguration->totalGearsCount;
	if (gearCount == 0) {
		// No gears, we only have neutral.
		return 0;
	}

	// 1.5x first gear is neutral or clutch slip or something
	if (ratio > engineConfiguration->gearRatio[0] * 1.5f) {
		return 0;
	}

	// 0.66x top gear is coasting with engine off or something
	if (ratio < engineConfiguration->gearRatio[gearCount - 1] * 0.66f) {
		return 0;
	}

	size_t currentGear = gearCount;

	while (currentGear > 1) {
		// Threshold between gears currentGear - 1 and currentGear
		float threshold = m_gearThresholds[currentGear - 2];

		if (currentGear == stickyGear) {
			// We're holding the taller of the two gears: the ratio has to climb past the boundary
			// before we'll drop out of it.
			threshold *= 1 + gearHysteresis;
		} else if (currentGear - 1 == stickyGear) {
			// We're holding the shorter of the two: the ratio has to fall past the boundary before
			// we'll shift up out of it.
			threshold *= 1 - gearHysteresis;
		}

		if (ratio < threshold) {
			break;
		}

		currentGear--;
	}

	return currentGear;
}

float GearDetector::gearRatioDeviation(float ratio, size_t gear) const {
	if (gear == 0 || gear > engineConfiguration->totalGearsCount) {
		// Not a gear we can measure against: neutral, or we don't know what we're in. Treat that as
		// maximally uncertain so it gets the longest dwell.
		return 1;
	}

	float expected = engineConfiguration->gearRatio[gear - 1];

	if (expected <= 0 || ratio <= 0) {
		return 1;
	}

	// Symmetric fractional error, so that being 5% high and 5% low count the same. This matches the
	// geometric mean used for the thresholds: both are distance in ratio, not in absolute terms.
	return (ratio > expected ? ratio / expected : expected / ratio) - 1;
}

float GearDetector::getDriveshaftRpm() const {
	auto vssKph = Sensor::getOrZero(SensorType::VehicleSpeed);

	if (vssKph < 5) {
		// Vehicle too slow to determine gearbox ratio, avoid div/0
		return 0;
	}

	// Convert to wheel RPM
	//                 km                        rev                        1 hr
	//               ------ *               ------------              *  __________
	//                 hr                        km                        60 min
	float wheelRpm = vssKph * engineConfiguration->driveWheelRevPerKm * (1 / 60.0f);

	// Convert to driveshaft RPM
	return wheelRpm * engineConfiguration->finalGearRatio;
}

float GearDetector::computeGearboxRatio() const {
	float driveshaftRpm = getDriveshaftRpm();

	if (driveshaftRpm == 0) {
		return 0;
	}

	float engineRpm = Sensor::getOrZero(SensorType::Rpm);

	return engineRpm / driveshaftRpm;
}

float GearDetector::getRpmInGear(size_t gear) const {
	if (gear <= 0 || gear > engineConfiguration->totalGearsCount) {
		return 0;
	}

	// Ideal engine RPM is driveshaft speed times gear
	return getDriveshaftRpm() * engineConfiguration->gearRatio[gear - 1];
}

expected<float> GearDetector::getTotalRatioInCurrentGear() const {
	auto currentGear = get();
	// If invalid or in neutral, there is no ratio
	if (currentGear.value_or(0) <= 0) {
		return unexpected;
	}

	size_t gear = currentGear.Value;

	return engineConfiguration->gearRatio[gear - 1] * engineConfiguration->finalGearRatio;
}

float GearDetector::getGearboxRatio() const {
	return m_gearboxRatio;
}

SensorResult GearDetector::get() const {
	return m_currentGear;
}

void GearDetector::showInfo(const char* sensorName) const {
	efiPrintf("Sensor \"%s\" is gear detector.", sensorName);
	efiPrintf("    Gearbox ratio: %.3f", m_gearboxRatio);
	efiPrintf("    Detected gear: %d (%.1f%% off ideal ratio)", m_currentGear, 100 * m_gearDeviation);
}
