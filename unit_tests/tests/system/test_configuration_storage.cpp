#include "pch.h"

#include "configuration_storage.h"
#include "crc_accelerator.h"

#include <array>
#include <functional>
#include <vector>

namespace {
constexpr flashaddr_t firstAddress = 0x100000;
constexpr flashaddr_t secondAddress = 0x200000;
constexpr size_t copySize = sizeof(persistent_config_container_s);

enum class Fault {
	None,
	Erase,
	Program,
	Corrupt,
	Read,
	Compare
};

struct FakeFlash {
	std::array<std::array<char, copySize>, 2> copies;
	std::vector<flashaddr_t> erases;
	std::vector<flashaddr_t> programs;
	std::vector<flashaddr_t> compares;
	Fault fault = Fault::None;
	flashaddr_t faultyAddress = firstAddress;
	std::function<void()> afterChange;
	std::function<void()> duringProgram;

	char* at(flashaddr_t address) {
		auto base = address >= secondAddress ? secondAddress : firstAddress;
		return copies[base == firstAddress ? 0 : 1].data() + address - base;
	}

	bool fails(flashaddr_t address, Fault phase) const {
		return fault == phase && address >= faultyAddress && address < faultyAddress + copySize;
	}

	void changed() {
		if (afterChange) {
			afterChange();
		}
	}
};

FakeFlash* flash;

void makeConfiguration(persistent_config_container_s& data, int fill) {
	memset(&data, 0, sizeof(data));
	data.version = FLASH_DATA_VERSION;
	data.size = sizeof(data);
	memset(&data.persistentConfiguration, fill, sizeof(data.persistentConfiguration));
	data.value = singleCrc(&data.persistentConfiguration, sizeof(data.persistentConfiguration));
}

class ConfigurationStorage : public testing::Test {
protected:
	void SetUp() override {
		flash = &memory;
		makeConfiguration(oldData, 0x11);
		makeConfiguration(newData, 0x22);
		memcpy(memory.at(firstAddress), &oldData, copySize);
		memcpy(memory.at(secondAddress), &oldData, copySize);
	}

	void TearDown() override {
		flash = nullptr;
	}

	void expectRecoverable() {
		persistent_config_container_s recovered;
		// A restart clears an injected transient driver error, but keeps the bytes.
		auto fault = memory.fault;
		memory.fault = Fault::None;
		auto state = readConfigurationCopies(firstAddress, secondAddress, recovered);
		memory.fault = fault;
		ASSERT_EQ(state, ConfigurationFlashState::Ok);
		EXPECT_TRUE(memcmp(&recovered, &oldData, copySize) == 0 || memcmp(&recovered, &newData, copySize) == 0);
	}

	FakeFlash memory;
	persistent_config_container_s oldData;
	persistent_config_container_s newData;
};

class ConfigurationStorageFault : public ConfigurationStorage, public testing::WithParamInterface<Fault> {};
} // namespace

int intFlashErase(flashaddr_t address, size_t size) {
	flash->erases.push_back(address);
	// A failed erase may already have destroyed part of the sector.
	bool fail = flash->fails(address, Fault::Erase);
	memset(flash->at(address), 0xff, fail ? size / 2 : size);
	flash->changed();
	return fail ? FLASH_RETURN_OPERROR : FLASH_RETURN_SUCCESS;
}

int intFlashWrite(flashaddr_t address, const char* buffer, size_t size) {
	flash->programs.push_back(address);
	if (flash->duringProgram) {
		flash->duringProgram();
	}
	for (size_t offset = 0; offset < size; offset += 256) {
		size_t count = std::min(size - offset, size_t(256));
		memcpy(flash->at(address + offset), buffer + offset, count);
		flash->changed();
		if (flash->fails(address, Fault::Program)) {
			return FLASH_RETURN_PSEQERROR;
		}
	}
	if (flash->fails(address, Fault::Corrupt)) {
		flash->at(address)[100] ^= 0x40;
		flash->changed();
	}
	return FLASH_RETURN_SUCCESS;
}

int intFlashRead(flashaddr_t source, char* destination, size_t size) {
	if (flash->fails(source, Fault::Read)) {
		return FLASH_RETURN_BAD_FLASH;
	}
	memcpy(destination, flash->at(source), size);
	return FLASH_RETURN_SUCCESS;
}

bool intFlashCompare(flashaddr_t address, const char* buffer, size_t size) {
	flash->compares.push_back(address);
	return !flash->fails(address, Fault::Compare) && memcmp(flash->at(address), buffer, size) == 0;
}

bool intFlashIsErased(flashaddr_t address, size_t size) {
	return std::all_of(
			flash->at(address), flash->at(address) + size, [](char value) { return uint8_t(value) == 0xff; });
}

flashsector_t intFlashSectorAt(flashaddr_t address) {
	return address / firstAddress;
}

TEST_F(ConfigurationStorage, writesAndVerifiesBothCopies) {
	auto result = writeConfigurationCopies(firstAddress, secondAddress, newData);
	EXPECT_TRUE(result.success());
	EXPECT_EQ(result.verifiedCopies, 2u);
	EXPECT_EQ(memory.compares, (std::vector<flashaddr_t>{firstAddress, secondAddress}));
	EXPECT_EQ(memcmp(memory.at(firstAddress), &newData, copySize), 0);
	EXPECT_EQ(memcmp(memory.at(secondAddress), &newData, copySize), 0);
}

TEST_P(ConfigurationStorageFault, firstCopyFailurePreservesBackup) {
	memory.fault = GetParam();
	auto result = writeConfigurationCopies(firstAddress, secondAddress, newData);
	EXPECT_FALSE(result.success());
	EXPECT_EQ(result.address, firstAddress);
	EXPECT_EQ(result.verifiedCopies, 0u);
	EXPECT_EQ(memory.erases, (std::vector<flashaddr_t>{firstAddress}));
	EXPECT_EQ(memcmp(memory.at(secondAddress), &oldData, copySize), 0);
	expectRecoverable();
}

TEST_P(ConfigurationStorageFault, protectsOnlyValidPrimary) {
	memset(memory.at(secondAddress), 0xff, copySize);
	memory.faultyAddress = secondAddress;
	memory.fault = GetParam();
	auto result = writeConfigurationCopies(firstAddress, secondAddress, newData);
	EXPECT_FALSE(result.success());
	EXPECT_EQ(memory.erases, (std::vector<flashaddr_t>{secondAddress}));
	EXPECT_EQ(memcmp(memory.at(firstAddress), &oldData, copySize), 0);
	expectRecoverable();
}

TEST_P(ConfigurationStorageFault, backupFailurePreservesOldOrVerifiedNewCopy) {
	memory.faultyAddress = secondAddress;
	memory.fault = GetParam();
	// An unreadable backup is repaired first while the validated primary is kept.
	bool backupFirst = GetParam() == Fault::Read;
	auto result = writeConfigurationCopies(firstAddress, secondAddress, newData);
	EXPECT_FALSE(result.success());
	EXPECT_EQ(result.verifiedCopies, backupFirst ? 0u : 1u);
	EXPECT_EQ(memcmp(memory.at(firstAddress), backupFirst ? &oldData : &newData, copySize), 0);
	expectRecoverable();
}

INSTANTIATE_TEST_SUITE_P(
		DriverErrors,
		ConfigurationStorageFault,
		testing::Values(Fault::Erase, Fault::Program, Fault::Corrupt, Fault::Read, Fault::Compare));

TEST_F(ConfigurationStorage, repairsBackupBeforeErasingOnlyValidPrimary) {
	memset(memory.at(secondAddress), 0xff, copySize);
	memory.afterChange = [this] { expectRecoverable(); };
	auto result = writeConfigurationCopies(firstAddress, secondAddress, newData);
	EXPECT_TRUE(result.success());
	EXPECT_EQ(memory.erases, (std::vector<flashaddr_t>{secondAddress, firstAddress}));
}

TEST_F(ConfigurationStorage, everyEraseAndProgrammingBoundaryPreservesAValidCopy) {
	memory.afterChange = [this] { expectRecoverable(); };
	EXPECT_TRUE(writeConfigurationCopies(firstAddress, secondAddress, newData).success());
}

TEST_F(ConfigurationStorage, startsWithInvalidPrimaryWhenBackupIsOnlyValidCopy) {
	memset(memory.at(firstAddress), 0xff, copySize);
	memory.afterChange = [this] { expectRecoverable(); };
	EXPECT_TRUE(writeConfigurationCopies(firstAddress, secondAddress, newData).success());
	EXPECT_EQ(memory.erases.front(), firstAddress);
}

TEST_F(ConfigurationStorage, rejectsPayloadChangedAfterItsCrcWasCalculated) {
	memory.duringProgram = [this] { reinterpret_cast<char*>(&newData.persistentConfiguration)[10] ^= 0x20; };
	auto result = writeConfigurationCopies(firstAddress, secondAddress, newData);
	EXPECT_FALSE(result.success());
	EXPECT_EQ(result.phase, ConfigurationWritePhase::Verify);
	EXPECT_EQ(memory.erases, (std::vector<flashaddr_t>{firstAddress}));
	EXPECT_EQ(memcmp(memory.at(secondAddress), &oldData, copySize), 0);
}

TEST_F(ConfigurationStorage, singleCopyIsVerified) {
	auto result = writeConfigurationCopies(firstAddress, 0, newData);
	EXPECT_TRUE(result.success());
	EXPECT_EQ(result.verifiedCopies, 1u);
	EXPECT_EQ(memory.compares, (std::vector<flashaddr_t>{firstAddress}));
}

TEST_F(ConfigurationStorage, invalidLayoutDoesNotEraseAnything) {
	EXPECT_FALSE(writeConfigurationCopies(0, 0, newData).success());
	EXPECT_FALSE(writeConfigurationCopies(0, secondAddress, newData).success());
	EXPECT_FALSE(writeConfigurationCopies(firstAddress, firstAddress, newData).success());
	EXPECT_TRUE(memory.erases.empty());
}

TEST_F(ConfigurationStorage, bootFallsBackAfterReadError) {
	memory.fault = Fault::Read;
	persistent_config_container_s recovered;
	EXPECT_EQ(readConfigurationCopies(firstAddress, secondAddress, recovered), ConfigurationFlashState::Ok);
	EXPECT_EQ(memcmp(&recovered, &oldData, copySize), 0);
}

TEST_F(ConfigurationStorage, bootRejectsCorruptionAndVersionMismatch) {
	memory.at(firstAddress)[100] ^= 1;
	persistent_config_container_s recovered;
	EXPECT_EQ(readConfigurationCopies(firstAddress, 0, recovered), ConfigurationFlashState::CrcFailed);
	EXPECT_EQ(readConfigurationCopies(firstAddress, secondAddress, recovered), ConfigurationFlashState::Ok);
	oldData.version++;
	memcpy(memory.at(firstAddress), &oldData, copySize);
	EXPECT_EQ(readConfigurationCopies(firstAddress, 0, recovered), ConfigurationFlashState::IncompatibleVersion);
}

TEST(ConfigurationWriteState, failedAttemptRemainsPendingWithoutRepeatedAutomaticWrites) {
	ConfigurationWriteState state;
	state.request();
	EXPECT_TRUE(state.shouldWrite());
	ASSERT_TRUE(state.begin());
	EXPECT_FALSE(state.begin());
	state.complete(false);
	EXPECT_TRUE(state.pending());
	EXPECT_FALSE(state.shouldWrite());
	state.request();
	EXPECT_TRUE(state.shouldWrite());
	ASSERT_TRUE(state.begin());
	state.complete(true);
	EXPECT_FALSE(state.pending());
}

TEST(ConfigurationWriteState, preservesRequestArrivingDuringWrite) {
	ConfigurationWriteState state;
	ASSERT_TRUE(state.begin());
	state.request();
	state.complete(true);
	EXPECT_TRUE(state.pending());
	EXPECT_TRUE(state.shouldWrite());
	ASSERT_TRUE(state.begin());
	state.complete(true);
	EXPECT_FALSE(state.pending());
	EXPECT_FALSE(state.shouldWrite());
}

TEST_F(ConfigurationStorage, doesNotEraseUnreadableCopyWithoutAnotherVerifiedCopy) {
	memset(memory.at(secondAddress), 0xff, copySize);
	memory.fault = Fault::Read;
	auto result = writeConfigurationCopies(firstAddress, secondAddress, newData);
	EXPECT_FALSE(result.success());
	EXPECT_TRUE(memory.erases.empty());
	expectRecoverable();
}
