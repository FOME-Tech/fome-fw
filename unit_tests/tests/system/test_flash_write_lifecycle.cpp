#include "pch.h"
#include "flash_main.h"
#include "eficonsole.h"
#include "flash_int.h"
#include "crc_accelerator.h"
#include "configuration_write.h"
#include "tunerstudio.h"
#include "runtime_state.h"
#include "stored_value_sensor.h"
#include "electronic_throttle_impl.h"
#include "dc_motor.h"

#include <functional>
#include <vector>

// Compile the production entry points with only flash I/O replaced.
// The error hook returns, matching production firmwareError behavior.
namespace flash_lifecycle_test {
struct SensorTimeoutState : Sensor {
	static bool inhibited() {
		return s_inhibitSensorTimeouts;
	}
};
static persistent_config_container_s persistentState;
static bool concurrent;
static unsigned errors;
static unsigned reads;
static bool exerciseActuator;
static unsigned guardDepth;
static unsigned guardStarts;
static unsigned guardEnds;
static std::vector<std::string> events;
static std::function<void()> duringEnd;
static std::function<void()> duringErase;
bool isWriting();
static flashaddr_t firstAddress;
static flashaddr_t secondAddress;
static flashaddr_t failingAddress;
static ConfigurationWritePhase failingPhase;
static std::vector<flashaddr_t> erases;
static std::vector<flashaddr_t> programs;
static std::function<void()> duringWrite;
static std::function<void()> duringRead;
static std::function<void()> duringError;

bool allowFlashWhileRunning() {
	return concurrent;
}
flashaddr_t getFlashAddrFirstCopy() {
	return firstAddress;
}
flashaddr_t getFlashAddrSecondCopy() {
	return secondAddress;
}
void beginBlockingFlash() {
	EXPECT_TRUE(isWriting());
	EXPECT_TRUE(flash_lifecycle_test::SensorTimeoutState::inhibited());
	EXPECT_EQ(guardDepth, 0u);
	++guardDepth;
	++guardStarts;
	events.push_back("begin");
	if (exerciseActuator) {
		::beginBlockingFlash();
	}
}
void endBlockingFlash() {
	EXPECT_TRUE(isWriting());
	EXPECT_TRUE(flash_lifecycle_test::SensorTimeoutState::inhibited());
	EXPECT_EQ(guardDepth, 1u);
	++guardEnds;
	events.push_back("end");
	if (exerciseActuator) {
		::endBlockingFlash();
	}
	if (duringEnd) {
		duringEnd();
	}
	--guardDepth;
}
void firmwareError(const char*, ...) {
	++errors;
	events.push_back("error");
	hasFirmwareErrorFlag = true;
	if (duringError) {
		duringError();
	}
}
int intFlashErase(flashaddr_t address, size_t size) {
	erases.push_back(address);
	events.push_back("erase");
	EXPECT_EQ(guardDepth, concurrent ? 0u : 1u);
	EXPECT_EQ(flash_lifecycle_test::SensorTimeoutState::inhibited(), !concurrent);
	if (duringErase) {
		duringErase();
	}
	EXPECT_EQ(size, sizeof(persistentState));
	if (address == failingAddress && failingPhase == ConfigurationWritePhase::Erase) {
		return FLASH_RETURN_OPERROR;
	}
	return FLASH_RETURN_SUCCESS;
}
int intFlashWrite(flashaddr_t address, const char* buffer, size_t size) {
	programs.push_back(address);
	events.push_back("program");
	EXPECT_EQ(guardDepth, concurrent ? 0u : 1u);
	EXPECT_EQ(size, sizeof(persistentState));
	EXPECT_EQ(buffer, reinterpret_cast<const char*>(&persistentState));
	EXPECT_EQ(persistentState.version, FLASH_DATA_VERSION);
	EXPECT_EQ(persistentState.size, sizeof(persistentState));
	EXPECT_EQ(persistentState.value, singleCrc(&persistentState.persistentConfiguration, sizeof(persistent_config_s)));
	if (duringWrite) {
		duringWrite();
	}
	if (address == failingAddress && failingPhase == ConfigurationWritePhase::Program) {
		return FLASH_RETURN_PSEQERROR;
	}
	return FLASH_RETURN_SUCCESS;
}
int intFlashRead(flashaddr_t, char* destination, size_t size) {
	++reads;
	EXPECT_EQ(destination, reinterpret_cast<char*>(&persistentState));
	EXPECT_EQ(size, sizeof(persistentState));
	if (duringRead) {
		duringRead();
	}
	// Read a valid copy so the production loader follows its normal path.
	persistentState.size = sizeof(persistentState);
	persistentState.version = FLASH_DATA_VERSION;
	persistentState.value = singleCrc(&persistentState.persistentConfiguration, sizeof(persistent_config_s));
	return FLASH_RETURN_SUCCESS;
}
void writeToFlashNow();

#undef EFI_INTERNAL_FLASH
#define EFI_INTERNAL_FLASH 1
#undef EFI_STORAGE_INT_FLASH
#define EFI_STORAGE_INT_FLASH 1
#undef EFI_FLASH_WRITE_THREAD
#define EFI_FLASH_WRITE_THREAD 0
#include "../../../firmware/controllers/flash_main.cpp"
bool isWriting() {
	return configurationWriteState.writing();
}
} // namespace flash_lifecycle_test

namespace {
class FlashWriteLifecycle : public testing::Test {
protected:
	bool previousFirmwareError;
	void SetUp() override {
		using namespace flash_lifecycle_test;
		previousFirmwareError = hasFirmwareErrorFlag;
		hasFirmwareErrorFlag = false;
		Sensor::inhibitTimeouts(false);
		concurrent = false;
		exerciseActuator = false;
		guardDepth = guardStarts = guardEnds = 0;
		events.clear();
		errors = reads = 0;
		firstAddress = 0x100000;
		secondAddress = 0x200000;
		failingAddress = 0;
		failingPhase = ConfigurationWritePhase::Complete;
		erases.clear();
		programs.clear();
		duringWrite = duringRead = duringError = duringEnd = duringErase = {};
		configurationWriteState = {};
		lastWriteResult = {ConfigurationWritePhase::Complete, 0, FLASH_RETURN_SUCCESS, 0};
		burnWithoutFlash = false;
	}
	void TearDown() override {
		EXPECT_EQ(flash_lifecycle_test::guardDepth, 0u);
		hasFirmwareErrorFlag = previousFirmwareError;
		Sensor::inhibitTimeouts(false);
	}
};

TEST_F(FlashWriteLifecycle, DirectWritePublishesBothDriverCompletions) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(erases, (std::vector<flashaddr_t>{firstAddress, secondAddress}));
	EXPECT_EQ(programs, erases);
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
	EXPECT_EQ(lastWriteResult.completedCopies, 2u);
	EXPECT_TRUE(lastWriteResult.success());
	EXPECT_EQ(events, (std::vector<std::string>{"begin", "erase", "program", "erase", "program", "end"}));
	EXPECT_FALSE(flash_lifecycle_test::SensorTimeoutState::inhibited());
}

TEST_F(FlashWriteLifecycle, PendingWriteBracketsAllIoAndRestoresTimeoutsImmediately) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_EQ(guardStarts, 1u);
	EXPECT_EQ(guardEnds, 1u);
	EXPECT_FALSE(flash_lifecycle_test::SensorTimeoutState::inhibited());
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_EQ(guardStarts, 1u);
}

TEST_F(FlashWriteLifecycle, ConcurrentFlashPolicyDoesNotSuspendActuators) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	concurrent = true;
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(guardStarts, 0u);
	EXPECT_EQ(guardEnds, 0u);
	EXPECT_FALSE(flash_lifecycle_test::SensorTimeoutState::inhibited());
	EXPECT_EQ(erases.size(), 2u);
}

TEST_F(FlashWriteLifecycle, ErrorAndGuardEndReentrantWritesWaitForOriginalGuard) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	failingAddress = firstAddress;
	failingPhase = ConfigurationWritePhase::Erase;
	duringError = [] {
		EXPECT_EQ(guardDepth, 1u);
		EXPECT_TRUE(configurationWriteState.writing());
		EXPECT_EQ(lastWriteResult.phase, ConfigurationWritePhase::Erase);
		flash_lifecycle_test::writeToFlashNow();
		EXPECT_EQ(erases.size(), 1u);
	};
	duringEnd = [] {
		EXPECT_TRUE(hasFirmwareError());
		flash_lifecycle_test::writeToFlashNow();
		flash_lifecycle_test::writeToFlashIfPending();
		EXPECT_EQ(erases.size(), 1u);
	};
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(guardStarts, 1u);
	EXPECT_EQ(guardEnds, 1u);
	EXPECT_FALSE(configurationWriteState.writing());
	EXPECT_TRUE(shouldWriteConfiguration());
	EXPECT_FALSE(flash_lifecycle_test::SensorTimeoutState::inhibited());
	duringError = duringEnd = {};
	failingAddress = 0;
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_EQ(guardStarts, 2u);
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, SuccessfulGuardEndRetainsOwnershipUntilTeardown) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	duringEnd = [] {
		EXPECT_TRUE(configurationWriteState.writing());
		flash_lifecycle_test::writeToFlashNow();
		EXPECT_EQ(erases.size(), 2u);
	};
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(guardStarts, 1u);
	EXPECT_TRUE(shouldWriteConfiguration());
	duringEnd = {};
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_EQ(guardStarts, 2u);
	EXPECT_EQ(erases.size(), 4u);
}

TEST_F(FlashWriteLifecycle, FailedFlashCannotRecoverActuatorBeforeFatalInhibit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	struct Pwm : IPwm {
		float duty = 0;
		void setSimplePwmDutyCycle(float value) override {
			duty = value;
		}
		void setDutyImmediate(float value) override {
			duty = value;
		}
	} enable, dir1, dir2;
	OutputPin disablePin;
	TwoPinDcMotor motor(disablePin);
	motor.configure(enable, dir1, dir2, false);
	motor.setType(TwoPinDcMotor::ControlType::PwmEnablePin);
	EtbController controller;
	pid_s pid = {};
	pid.pFactor = 1;
	pid.minValue = -90;
	pid.maxValue = 90;
	ASSERT_TRUE(controller.init(DC_Wastegate, &motor, &pid, nullptr, false));
	exerciseActuator = true;
	engine->etbControllers[0] = &controller;
	engine->etbControllers[1] = nullptr;
	controller.setWastegatePosition(50);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	controller.update();
	ASSERT_GT(enable.duty, 0);
	auto attemptRecovery = [&] {
		eth.moveTimeForwardMs(1);
		Sensor::setMockValue(SensorType::WastegatePosition, 10);
		controller.update();
		controller.setOutput(50);
		motor.set(0.5f);
		motor.enable();
		EXPECT_TRUE(disablePin.getLogicValue());
		EXPECT_EQ(enable.duty, 0);
	};
	duringErase = [&] {
		EXPECT_FALSE(hasFirmwareError());
		attemptRecovery();
	};
	duringError = [&] {
		EXPECT_TRUE(hasFirmwareError());
		attemptRecovery();
	};
	duringEnd = attemptRecovery;
	failingAddress = firstAddress;
	failingPhase = ConfigurationWritePhase::Erase;
	flash_lifecycle_test::writeToFlashNow();
	attemptRecovery();
	controller.deinit();
	ASSERT_TRUE(controller.init(DC_Wastegate, &motor, &pid, nullptr, false));
	attemptRecovery();
	EXPECT_EQ(motor.get(), 0);
}

TEST_F(FlashWriteLifecycle, FirstDriverFailureStopsBeforeBackupAndPublishesBeforeError) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	for (auto phase : {ConfigurationWritePhase::Erase, ConfigurationWritePhase::Program}) {
		erases.clear();
		programs.clear();
		failingAddress = firstAddress;
		failingPhase = phase;
		duringError = [&] {
			EXPECT_TRUE(configurationWriteState.writing());
			EXPECT_EQ(guardDepth, 1u);
			EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
			EXPECT_FALSE(flash_lifecycle_test::shouldWriteConfiguration());
			EXPECT_EQ(lastWriteResult.phase, phase);
			EXPECT_EQ(lastWriteResult.address, firstAddress);
			EXPECT_EQ(lastWriteResult.completedCopies, 0u);
		};
		flash_lifecycle_test::writeToFlashNow();
		EXPECT_EQ(erases, (std::vector<flashaddr_t>{firstAddress}));
		EXPECT_EQ(programs.size(), phase == ConfigurationWritePhase::Erase ? 0u : 1u);
	}
	EXPECT_EQ(errors, 2u);
}

TEST_F(FlashWriteLifecycle, SecondDriverFailureReportsOneCompletedCopy) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	failingAddress = secondAddress;
	failingPhase = ConfigurationWritePhase::Program;
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(lastWriteResult.completedCopies, 1u);
	EXPECT_EQ(lastWriteResult.address, secondAddress);
	EXPECT_EQ(lastWriteResult.error, FLASH_RETURN_PSEQERROR);
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, FailedPendingWriteWaitsForFreshRequest) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	failingAddress = firstAddress;
	failingPhase = ConfigurationWritePhase::Erase;
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
	for (int i = 0; i < 5; ++i) {
		flash_lifecycle_test::writeToFlashIfPending();
	}
	EXPECT_EQ(erases.size(), 1u);
	failingAddress = 0;
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
	EXPECT_EQ(erases.size(), 3u);
}

TEST_F(FlashWriteLifecycle, ReentrantDirectWritePreservesRequestWithoutOverlappingIo) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	duringWrite = [] { flash_lifecycle_test::writeToFlashNow(); };
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(erases.size(), 2u);
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
	duringWrite = {};
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
	EXPECT_EQ(erases.size(), 4u);
}

TEST_F(FlashWriteLifecycle, NewRequestDuringFailedWriteIsEligibleForNextAttempt) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	failingAddress = firstAddress;
	failingPhase = ConfigurationWritePhase::Program;
	duringWrite = [] { flash_lifecycle_test::setNeedToWriteConfiguration(); };
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_TRUE(flash_lifecycle_test::shouldWriteConfiguration());
	duringWrite = {};
	failingAddress = 0;
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, ReadCannotOverwriteRamDuringProgramming) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	duringWrite = [] { flash_lifecycle_test::readFromFlash(); };
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(reads, 0u);
}

TEST_F(FlashWriteLifecycle, WriteDuringReadWaitsForLoadToFinish) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	duringRead = [] {
		flash_lifecycle_test::writeToFlashNow();
		flash_lifecycle_test::writeToFlashIfPending();
		flash_lifecycle_test::readFromFlash();
		EXPECT_TRUE(erases.empty());
	};
	flash_lifecycle_test::readFromFlash();
	EXPECT_EQ(reads, 1u);
	EXPECT_FALSE(configurationWriteState.reading());
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_EQ(erases.size(), 2u);
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, ConsumedRequestCannotTriggerStaleWriterWakeup) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	concurrent = true;
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_TRUE(erases.empty());
	failingAddress = firstAddress;
	failingPhase = ConfigurationWritePhase::Erase;
	flash_lifecycle_test::writeToFlashNow();
	// This is the atomic claim used by the H7 writer after a semaphore wakeup.
	flash_lifecycle_test::writeConfiguration(true);
	EXPECT_EQ(erases.size(), 1u);
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, BurnWithoutFlashConsumesRequestWithoutDriverIo) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	burnWithoutFlash = true;
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_TRUE(erases.empty());
	EXPECT_TRUE(programs.empty());
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
	EXPECT_EQ(lastWriteResult.completedCopies, 0u);
	EXPECT_EQ(guardStarts, 0u);
	EXPECT_FALSE(flash_lifecycle_test::SensorTimeoutState::inhibited());
}

TEST_F(FlashWriteLifecycle, TimeoutInhibitionLastsOnlyForBlockingWrite) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StoredValueSensor sensor(SensorType::WastegatePosition, MS2NT(10));
	sensor.setValidValue(20, getTimeNowNt());
	eth.moveTimeForwardMs(100);
	ASSERT_FALSE(sensor.get().Valid);
	using namespace flash_lifecycle_test;
	duringWrite = [&] { EXPECT_TRUE(sensor.get().Valid); };
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_FALSE(sensor.get().Valid);
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_FALSE(sensor.get().Valid);
}

TEST_F(FlashWriteLifecycle, SupportsSingleCopyAndRejectsMissingPrimary) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	using namespace flash_lifecycle_test;
	secondAddress = 0;
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(lastWriteResult.completedCopies, 1u);
	firstAddress = 0;
	erases.clear();
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_TRUE(erases.empty());
	EXPECT_EQ(lastWriteResult.phase, ConfigurationWritePhase::Layout);
	EXPECT_EQ(lastWriteResult.completedCopies, 0u);
}
} // namespace
