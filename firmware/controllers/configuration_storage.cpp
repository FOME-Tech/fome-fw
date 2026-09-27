#include "pch.h"

#if EFI_INTERNAL_FLASH || EFI_UNIT_TEST

#include "configuration_storage.h"
#include "crc_accelerator.h"

static ConfigurationFlashState
validateConfiguration(int version, int size, uint32_t storedCrc, uint32_t calculatedCrc) {
	if (storedCrc != calculatedCrc) {
		return storedCrc == UINT32_MAX ? ConfigurationFlashState::BlankChip : ConfigurationFlashState::CrcFailed;
	}
	if (version != FLASH_DATA_VERSION || size != sizeof(persistent_config_container_s)) {
		return ConfigurationFlashState::IncompatibleVersion;
	}
	return ConfigurationFlashState::Ok;
}

static ConfigurationFlashState readCopy(flashaddr_t address, persistent_config_container_s& destination) {
	if (!address) {
		return ConfigurationFlashState::BlankChip;
	}
	if (intFlashRead(address, reinterpret_cast<char*>(&destination), sizeof(destination)) != FLASH_RETURN_SUCCESS) {
		return ConfigurationFlashState::ReadFailed;
	}
	return validateConfiguration(
			destination.version,
			destination.size,
			destination.value,
			singleCrc(&destination.persistentConfiguration, sizeof(destination.persistentConfiguration)));
}

ConfigurationFlashState
readConfigurationCopies(flashaddr_t first, flashaddr_t second, persistent_config_container_s& destination) {
	auto state = readCopy(first, destination);
	return state == ConfigurationFlashState::Ok || !second ? state : readCopy(second, destination);
}

// Check a stored copy without allocating another full configuration in RAM.
// Every read goes through the driver so M7 cache invalidation also applies.
static ConfigurationFlashState checkCopy(flashaddr_t address) {
	struct Header {
		int version;
		int size;
	} header;
	static_assert(sizeof(header) == offsetof(persistent_config_container_s, persistentConfiguration));

	if (!address || intFlashRead(address, reinterpret_cast<char*>(&header), sizeof(header)) != FLASH_RETURN_SUCCESS) {
		return ConfigurationFlashState::ReadFailed;
	}
	if (header.version != FLASH_DATA_VERSION || header.size != sizeof(persistent_config_container_s)) {
		return header.version == -1 && header.size == -1 ? ConfigurationFlashState::BlankChip
														 : ConfigurationFlashState::IncompatibleVersion;
	}

	Crc crc(sizeof(persistent_config_s));
	alignas(4) char buffer[128];
	for (size_t offset = 0; offset < sizeof(persistent_config_s); offset += sizeof(buffer)) {
		size_t count = std::min(sizeof(buffer), sizeof(persistent_config_s) - offset);
		if (intFlashRead(address + sizeof(header) + offset, buffer, count) != FLASH_RETURN_SUCCESS) {
			return ConfigurationFlashState::ReadFailed;
		}
		crc.addData(buffer, count);
	}

	uint32_t storedCrc;
	if (intFlashRead(
				address + offsetof(persistent_config_container_s, value),
				reinterpret_cast<char*>(&storedCrc),
				sizeof(storedCrc)) != FLASH_RETURN_SUCCESS) {
		return ConfigurationFlashState::ReadFailed;
	}
	return validateConfiguration(header.version, header.size, storedCrc, crc.getCrc());
}

ConfigurationWriteResult
writeConfigurationCopies(flashaddr_t first, flashaddr_t second, const persistent_config_container_s& data) {
	if (!first || (second && intFlashSectorAt(first) <= intFlashSectorAt(second + sizeof(data) - 1) &&
				   intFlashSectorAt(second) <= intFlashSectorAt(first + sizeof(data) - 1))) {
		return {ConfigurationWritePhase::Layout, first, FLASH_RETURN_NO_PERMISSION, 0};
	}

	// If only the primary is valid, repair the backup before touching it. If the
	// backup is valid (or neither is), start with the primary as on boot.
	if (second) {
		auto backup = checkCopy(second);
		if (backup != ConfigurationFlashState::Ok) {
			auto primary = checkCopy(first);
			if (primary == ConfigurationFlashState::Ok) {
				std::swap(first, second);
			} else if (
					primary == ConfigurationFlashState::ReadFailed || backup == ConfigurationFlashState::ReadFailed) {
				// An unreadable copy might be the only good one. Do not erase it
				// unless another copy has actually been validated.
				return {ConfigurationWritePhase::Verify, first, FLASH_RETURN_BAD_FLASH, 0};
			}
		}
	}

	ConfigurationWriteResult result{ConfigurationWritePhase::Complete, 0, FLASH_RETURN_SUCCESS, 0};
	for (auto address : {first, second}) {
		if (!address) {
			continue;
		}
		auto error = intFlashErase(address, sizeof(data));
		if (error == FLASH_RETURN_SUCCESS && !intFlashIsErased(address, sizeof(data))) {
			error = FLASH_RETURN_BAD_FLASH;
		}
		if (error != FLASH_RETURN_SUCCESS) {
			return {ConfigurationWritePhase::Erase, address, error, result.verifiedCopies};
		}
		error = intFlashWrite(address, reinterpret_cast<const char*>(&data), sizeof(data));
		if (error != FLASH_RETURN_SUCCESS) {
			return {ConfigurationWritePhase::Program, address, error, result.verifiedCopies};
		}
		// A byte comparison alone is insufficient when tuning can change the
		// source during a write. The stored image must also have a valid CRC.
		if (!intFlashCompare(address, reinterpret_cast<const char*>(&data), sizeof(data)) ||
			checkCopy(address) != ConfigurationFlashState::Ok) {
			return {ConfigurationWritePhase::Verify, address, FLASH_RETURN_BAD_FLASH, result.verifiedCopies};
		}
		result.verifiedCopies++;
	}
	return result;
}

#endif
