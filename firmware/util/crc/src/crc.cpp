/**
 * @file    crc.c
 * @date Sep 20, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "crc.h"

/**
 * Online CRC calculator:
 * http://www.zorc.breitbandkatze.de/crc.html
 */
uint32_t crc32(const void *buf, uint32_t size) {
	return crc32inc(buf, 0, size);
}

uint32_t crc32inc(const void *buf, uint32_t crc, uint32_t size) {

	CRCConfig cfg = crc32_cfg;
	cfg.initial_val = crc ^ 0xFFFFFFFF;

	crcStart(&CRCD1, &cfg);
	uint32_t crcCalculated = crcCalc(&CRCD1, size, buf);
	crcStop(&CRCD1);
	return crcCalculated;
}

/**
 * http://www.sunshine2k.de/coding/javascript/crc/crc_js.html
 * https://stackoverflow.com/questions/38639423/understanding-results-of-crc8-sae-j1850-normal-vs-zero
 * j1850 SAE crc8
 */
uint8_t crc8(const uint8_t *data, uint8_t len) {

	if (data == 0) {
		return 0;
	}

	crcStart(&CRCD1, &crc8_cfg);
	uint8_t crc = crcCalc(&CRCD1, len, data);
	crcStop(&CRCD1);
	return crc;
}
