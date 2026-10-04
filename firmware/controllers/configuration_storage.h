#pragma once

#include "configuration_write.h"
#include "persistent_configuration.h"

enum class ConfigurationFlashState {
	Ok,
	CrcFailed,
	IncompatibleVersion,
	BlankChip,
	ReadFailed,
};

ConfigurationFlashState
readConfigurationCopies(flashaddr_t first, flashaddr_t second, persistent_config_container_s& destination);

ConfigurationWriteResult
writeConfigurationCopies(flashaddr_t first, flashaddr_t second, const persistent_config_container_s& data);
