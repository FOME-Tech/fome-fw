/*
 * @file value_lookup.h
 *
 * @date Dec 13, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#pragma once

struct plain_get_integer_s {
	const char *token;
	int *value;
};

struct plain_get_short_s {
	const char *token;
	uint16_t *value;
};

struct plain_get_u8_s {
	const char *token;
	uint8_t *value;
};

struct plain_get_float_s {
	const char *token;
	float *value;
};

expected<float> getConfigValueByName(const char *name);
void setConfigValueByName(const char *name, float value);
expected<float> getOutputValueByName(const char *name);

void * hackEngineConfigurationPointer(void *ptr);
