/*
 * @file value_lookup.h
 *
 * @date Dec 13, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#pragma once

#include "log_field.h"

struct plain_get_integer_s {
	const char* token;
	int* value;
};

struct plain_get_short_s {
	const char* token;
	uint16_t* value;
};

struct plain_get_u8_s {
	const char* token;
	uint8_t* value;
};

struct plain_get_float_s {
	const char* token;
	float* value;
};

expected<float> getConfigValueByName(const char* name);
void setConfigValueByName(const char* name, float value);
expected<float> getOutputValueByName(const char* name);

// Used by the generated getOutputValueByName() to read an output channel by its offset into the
// output channel space (see getOutputValueByName in output_lookup_generated.cpp).
float getOutputChannelValue(uint16_t offset, LogField::Type type, float multiplier);
float getOutputChannelBit(uint16_t offset, uint8_t bitIndex);

void* hackEngineConfigurationPointer(void* ptr);
