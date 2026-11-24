#include "pch.h"

static constexpr float geometricMean(float x, float y) {
	return sqrtf(x * y);
}

// Widest tolerance for the measured ratio to count as locked to a gear. Narrowed for close-ratio
// gearboxes so that neighboring gears' bands never touch.
static constexpr float kMaxLockTolerance = 0.04f;

// With the clutch engaged, a gear must stay locked this long before we switch to it.
static constexpr float kEngagedLockTimeSec = 0.15f;
// With the clutch in the middle (possibly slipping), a gear must stay locked this long before we
// believe the clutch is actually holding. Also covers a sensor stuck partway.
static constexpr float kMiddleLockTimeSec = 1.0f;
// With the clutch engaged but the ratio not near any gear for this long, we're in neutral.
static constexpr float kNeutralTimeSec = 1.0f;

// If the clutch reads disengaged, but the ratio stays locked to a gear for this long while engine
// speed changes by at least this fraction, the engine is clearly still driving the wheels.
static constexpr float kCoupledTimeSec = 0.5f;
static constexpr float kCoupledRpmSpan = 0.1f;

// Mismatch check: the ratio counts as steady while it stays within this fraction of where it
// started, for at least this long, above this speed.
static constexpr float kSteadyRatioBand = 0.015f;
static constexpr float kSteadyTimeSec = 2.0f;
static constexpr float kSteadyMinKph = 30.0f;
// Flag a mismatch once steady driving has spent this long not locked to any gear, and more time
// unlocked than locked.
static constexpr float kMismatchUnlockedSec = 10.0f;

// A single switch can't tell all three clutch positions apart. Assume it trips at the bite point,
// so the clutch-aware logic still runs at full speed. If the switch actually trips somewhere else,
// the clutch loses trust and we fall back to the ratio alone.
static ClutchState forGearDetection(ClutchState state) {
	switch (state) {
		case ClutchState::EngagedOrMiddle:
			return ClutchState::Engaged;
		case ClutchState::MiddleOrDisengaged:
			return ClutchState::Disengaged;
		default:
			return state;
	}
}

GearDetector::GearDetector()
	: Sensor(SensorType::DetectedGear) {}

GearDetector::~GearDetector() {
	unregister();
}

void GearDetector::onConfigurationChange(engine_configuration_s const* /*previousConfig*/) {
	// Any of tire size, final drive, gear ratios or VSS may have changed: start the mismatch check over
	m_steadyRatio = 0;
	m_steadyLockedSec = 0;
	m_steadyUnlockedSec = 0;
	ratioMismatch = false;

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

	m_lockTolerance = kMaxLockTolerance;

	for (int i = 0; i < gearCount - 1; i++) {
		// Threshold i is the threshold between gears i and i+1
		float gearI = engineConfiguration->gearRatio[i];
		float gearIplusOne = engineConfiguration->gearRatio[i + 1];

		if (gearI <= gearIplusOne) {
			firmwareError("Invalid gear ordering near gear #%d", i + 1);
			return;
		}

		m_gearThresholds[i] = geometricMean(gearI, gearIplusOne);

		// Use at most a quarter of the gap between adjacent gears
		float gap = gearI / gearIplusOne - 1;
		m_lockTolerance = std::min(m_lockTolerance, gap / 4);
	}

	Register();
}

void GearDetector::onSlowCallback() {
	float ratio = computeGearboxRatio();
	m_gearboxRatio = ratio;

	size_t raw = determineGearFromRatio(ratio);
	size_t locked = determineLockedGear(ratio);
	ClutchState clutch = forGearDetection(getClutchState());
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	if (locked != lockedGear || clutch != m_lastClutch) {
		// Start a new episode
		m_episodeTimer.reset();
		m_episodeRpmMin = rpm;
		m_episodeRpmMax = rpm;
	} else {
		m_episodeRpmMin = std::min(m_episodeRpmMin, rpm);
		m_episodeRpmMax = std::max(m_episodeRpmMax, rpm);
	}

	rawGear = raw;
	lockedGear = locked;
	m_lastClutch = clutch;

	updateClutchTrust(clutch, locked);
	updateRatioMismatch(ratio, raw, locked, clutch);

	if (clutchTrusted) {
		m_currentGear = determineGearWithClutch(clutch, locked);
	} else {
		// Without a trustworthy clutch, fall back to the ratio alone
		m_currentGear = raw;
	}

	speedToRpmRatio = ratio;
	detectedGear = m_currentGear;
}

bool GearDetector::isLockCoupled() const {
	if (!m_episodeTimer.hasElapsedSec(kCoupledTimeSec)) {
		return false;
	}

	// A coincidental match while the clutch is released (ie, coasting at idle) has steady engine
	// speed. A real lock means engine speed tracks road speed as it changes.
	return (m_episodeRpmMax - m_episodeRpmMin) > kCoupledRpmSpan * m_episodeRpmMax;
}

void GearDetector::updateClutchTrust(ClutchState clutch, size_t locked) {
	switch (clutch) {
		case ClutchState::Indeterminate:
			clutchTrusted = false;
			m_sawDecoupled = false;
			break;
		case ClutchState::Disengaged:
			if (!locked) {
				// Consistent with the sensor: engine decoupled from the wheels
				m_sawDecoupled = true;
			} else if (isLockCoupled()) {
				// The sensor says disengaged, but the engine is clearly driving the wheels.
				clutchTrusted = false;
			}
			break;
		case ClutchState::Engaged:
			if (locked && m_episodeTimer.hasElapsedSec(kEngagedLockTimeSec)) {
				if (m_sawDecoupled) {
					// Full cycle observed: disengaged & decoupled, then engaged & locked in a gear
					clutchTrusted = true;
				}

				m_sawDecoupled = false;
			}
			break;
		case ClutchState::Middle:
			// Could be slipping or not, tells us nothing.
			break;
		case ClutchState::EngagedOrMiddle:
		case ClutchState::MiddleOrDisengaged:
			// Never seen here, forGearDetection() maps these away
			break;
	}
}

void GearDetector::updateRatioMismatch(float ratio, size_t raw, size_t locked, ClutchState clutch) {
	bool clutchMayBeOpen = clutch == ClutchState::Middle || clutch == ClutchState::Disengaged;
	bool eligible = raw != 0 && !clutchMayBeOpen && Sensor::getOrZero(SensorType::VehicleSpeed) >= kSteadyMinKph;

	if (!eligible) {
		m_steadyRatio = 0;
	} else if (m_steadyRatio == 0 || std::abs(ratio / m_steadyRatio - 1) > kSteadyRatioBand) {
		// Ratio moved: start a new steady span from here
		m_steadyRatio = ratio;
		m_steadyTimer.reset();
	} else if (m_steadyTimer.hasElapsedSec(kSteadyTimeSec)) {
		constexpr float tickSec = SLOW_CALLBACK_PERIOD_MS / 1000.0f;

		if (locked) {
			m_steadyLockedSec += tickSec;
		} else {
			m_steadyUnlockedSec += tickSec;
		}
	}

	ratioMismatch = m_steadyUnlockedSec >= kMismatchUnlockedSec && m_steadyUnlockedSec > m_steadyLockedSec;

	if (ratioMismatch) {
		// Repeated calls are rate limited, and keep the warning active while the condition holds
		warning(ObdCode::CUSTOM_GEAR_RATIO_MISMATCH,
				"Gear detection questionable: ratio %.3f matches no gear. Check tire size, final drive, VSS, "
				"gear ratios",
				ratio);
	}
}

size_t GearDetector::determineGearWithClutch(ClutchState clutch, size_t locked) const {
	if (m_gearboxRatio == 0) {
		// Stopped or too slow to tell
		return 0;
	}

	switch (clutch) {
		case ClutchState::Engaged:
			if (locked) {
				return m_episodeTimer.hasElapsedSec(kEngagedLockTimeSec) ? locked : m_currentGear;
			}

			// Clutch engaged but not near any gear: neutral
			return m_episodeTimer.hasElapsedSec(kNeutralTimeSec) ? 0 : m_currentGear;
		case ClutchState::Middle:
			// Possibly slipping, so only believe a lock that has held for a long time. This also
			// covers a sensor stuck partway: we still pick up shifts we're sure about.
			if (locked && m_episodeTimer.hasElapsedSec(kMiddleLockTimeSec)) {
				return locked;
			}

			return m_currentGear;
		case ClutchState::Disengaged:
		case ClutchState::Indeterminate:
			// Mid-shift: hold the gear we're shifting out of
			return m_currentGear;
		case ClutchState::EngagedOrMiddle:
		case ClutchState::MiddleOrDisengaged:
			// Never seen here, forGearDetection() maps these away
			break;
	}

	return m_currentGear;
}

size_t GearDetector::determineGearFromRatio(float ratio) const {
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
		if (ratio < m_gearThresholds[currentGear - 2]) {
			break;
		}

		currentGear--;
	}

	return currentGear;
}

size_t GearDetector::determineLockedGear(float ratio) const {
	auto gearCount = engineConfiguration->totalGearsCount;

	for (size_t i = 0; i < gearCount; i++) {
		float gearRatio = engineConfiguration->gearRatio[i];

		if (std::abs(ratio / gearRatio - 1) < m_lockTolerance) {
			return i + 1;
		}
	}

	return 0;
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
	efiPrintf("    Detected gear: %d", m_currentGear);
	efiPrintf("    Raw gear: %d locked gear: %d", rawGear, lockedGear);
	efiPrintf("    Clutch state: %d trusted: %d", static_cast<int>(m_lastClutch), clutchTrusted);
}
