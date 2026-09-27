#include "pch.h"
#include "flash_main.h"
#include "eficonsole.h"
#include "flash_int.h"
#include "crc_accelerator.h"
#include "configuration_storage.h"
#include "electronic_throttle.h"
#include "tunerstudio.h"
#include "runtime_state.h"
#include "stored_value_sensor.h"

#include <functional>
#include <vector>

// Exercise the production entry points with only flash I/O and actuator hooks
// replaced. firmwareError returns in production, unlike the general host mock.
namespace flash_lifecycle_test {
static persistent_config_container_s persistentState;
static bool concurrent;
static unsigned errors;
static std::vector<char> events;
static std::function<void()> duringWrite;
static ConfigurationWriteResult nextResult;
static unsigned reads;
static std::function<void()> duringRead;

bool allowFlashWhileRunning() {
	return concurrent;
}
flashaddr_t getFlashAddrFirstCopy() {
	return 0x100000;
}
flashaddr_t getFlashAddrSecondCopy() {
	return 0x200000;
}
void beginBlockingFlash() {
	events.push_back('B');
}
void endBlockingFlash() {
	events.push_back('E');
}
void firmwareError(const char*, ...) {
	++errors;
}
void writeToFlashNow();
auto readConfigurationCopies = [](flashaddr_t, flashaddr_t, persistent_config_container_s&) {
	++reads;
	if (duringRead) {
		duringRead();
	}
	return ConfigurationFlashState::Ok;
};
auto writeConfigurationCopies = [](flashaddr_t, flashaddr_t, const persistent_config_container_s& data) {
	events.push_back('W');
	EXPECT_EQ(data.version, FLASH_DATA_VERSION);
	EXPECT_EQ(data.size, sizeof(data));
	EXPECT_EQ(data.value, singleCrc(&data.persistentConfiguration, sizeof(data.persistentConfiguration)));
	if (duringWrite) {
		duringWrite();
	}
	return nextResult;
};

#undef EFI_INTERNAL_FLASH
#define EFI_INTERNAL_FLASH 1
#undef EFI_STORAGE_INT_FLASH
#define EFI_STORAGE_INT_FLASH 1
#undef EFI_FLASH_WRITE_THREAD
#define EFI_FLASH_WRITE_THREAD 0
#include "../../../firmware/controllers/flash_main.cpp"
} // namespace flash_lifecycle_test

namespace {
class FlashWriteLifecycle : public testing::Test {
protected:
	void SetUp() override {
		using namespace flash_lifecycle_test;
		concurrent = false;
		errors = 0;
		reads = 0;
		events.clear();
		duringWrite = {};
		duringRead = {};
		nextResult = {ConfigurationWritePhase::Complete, 0, FLASH_RETURN_SUCCESS, 2};
		configurationWriteState = {};
		burnWithoutFlash = false;
	}
	void TearDown() override {
		Sensor::inhibitTimeouts(false);
	}
};

TEST_F(FlashWriteLifecycle, DirectWritesBracketStorageAndDoNotRestoreExpiredSensors) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StoredValueSensor sensor(SensorType::WastegatePosition, MS2NT(10));
	sensor.setValidValue(20, getTimeNowNt());
	eth.moveTimeForwardMs(100);
	ASSERT_FALSE(sensor.get().Valid);
	flash_lifecycle_test::duringWrite = [&] {
		EXPECT_TRUE(sensor.get().Valid);
		EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
	};
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(flash_lifecycle_test::events, (std::vector<char>{'B', 'W', 'E'}));
	EXPECT_FALSE(sensor.get().Valid);
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, FailedPendingWriteRemainsVisibleWithoutContinuousRetries) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	flash_lifecycle_test::nextResult = {ConfigurationWritePhase::Verify, 0x100000, FLASH_RETURN_BAD_FLASH, 0};
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
	EXPECT_EQ(flash_lifecycle_test::errors, 1u);
	EXPECT_EQ(flash_lifecycle_test::events, (std::vector<char>{'B', 'W', 'E'}));
	EXPECT_EQ(flash_lifecycle_test::lastWriteResult.phase, ConfigurationWritePhase::Verify);
	for (int i = 0; i < 5; ++i) {
		flash_lifecycle_test::writeToFlashIfPending();
	}
	EXPECT_EQ(flash_lifecycle_test::events.size(), 3u);
	flash_lifecycle_test::nextResult = {ConfigurationWritePhase::Complete, 0, FLASH_RETURN_SUCCESS, 2};
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
	EXPECT_EQ(flash_lifecycle_test::events.size(), 6u);
}

TEST_F(FlashWriteLifecycle, ConcurrentFlashDoesNotSuspendActuatorsOrSensorTimeouts) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StoredValueSensor sensor(SensorType::WastegatePosition, MS2NT(10));
	sensor.setValidValue(20, getTimeNowNt());
	eth.moveTimeForwardMs(100);
	flash_lifecycle_test::concurrent = true;
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_TRUE(flash_lifecycle_test::events.empty());
	flash_lifecycle_test::duringWrite = [&] { EXPECT_FALSE(sensor.get().Valid); };
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(flash_lifecycle_test::events, (std::vector<char>{'W'}));
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, BurnWithoutFlashDoesNotTouchStorageOrActuators) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	flash_lifecycle_test::burnWithoutFlash = true;
	flash_lifecycle_test::setNeedToWriteConfiguration();
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_TRUE(flash_lifecycle_test::events.empty());
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, ReentrantDirectWriteIsDeferredUntilCurrentWriteCompletes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	flash_lifecycle_test::duringWrite = [] { flash_lifecycle_test::writeToFlashNow(); };
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(flash_lifecycle_test::events, (std::vector<char>{'B', 'W', 'E'}));
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
	flash_lifecycle_test::duringWrite = {};
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
	EXPECT_EQ(flash_lifecycle_test::events.size(), 6u);
}

TEST_F(FlashWriteLifecycle, ReadConfigCannotOverwriteRamDuringProgramming) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	flash_lifecycle_test::duringWrite = [] { flash_lifecycle_test::readFromFlash(); };
	flash_lifecycle_test::writeToFlashNow();
	EXPECT_EQ(flash_lifecycle_test::reads, 0u);
}

TEST_F(FlashWriteLifecycle, ConsumedRequestCannotTriggerAnotherAutomaticWrite) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	flash_lifecycle_test::setNeedToWriteConfiguration();
	ASSERT_TRUE(flash_lifecycle_test::shouldWriteConfiguration());
	// Model a console write consuming the request just before the writer runs.
	flash_lifecycle_test::nextResult = {ConfigurationWritePhase::Verify, 0x100000, FLASH_RETURN_BAD_FLASH, 0};
	flash_lifecycle_test::writeToFlashNow();
	flash_lifecycle_test::writeConfiguration(true);
	EXPECT_EQ(flash_lifecycle_test::events.size(), 3u);
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
}

TEST_F(FlashWriteLifecycle, WriteRequestedDuringReadWaitsUntilRamHasBeenLoaded) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	flash_lifecycle_test::duringRead = [] {
		flash_lifecycle_test::writeToFlashNow();
		flash_lifecycle_test::writeToFlashIfPending();
		EXPECT_TRUE(flash_lifecycle_test::events.empty());
	};
	flash_lifecycle_test::readFromFlash();
	EXPECT_TRUE(flash_lifecycle_test::getNeedToWriteConfiguration());
	flash_lifecycle_test::writeToFlashIfPending();
	EXPECT_EQ(flash_lifecycle_test::events, (std::vector<char>{'B', 'W', 'E'}));
	EXPECT_FALSE(flash_lifecycle_test::getNeedToWriteConfiguration());
}
} // namespace
