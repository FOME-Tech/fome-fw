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

	static bool init_futex = false;
	static fast_mutex_t crc_futex;
	if (!init_futex) {
		fast_mutex_init(&crc_futex);
		init_futex = true;
	}
	fast_mutex_lock(&crc_futex);
	CRC->INIT = crc ^ 0xFFFFFFFF;
	uint32_t crcCalculated = crcCalc(&CRCD1, size, buf);
	fast_mutex_unlock(&crc_futex);
	return crcCalculated;
}

/**
 * http://www.sunshine2k.de/coding/javascript/crc/crc_js.html
 * https://stackoverflow.com/questions/38639423/understanding-results-of-crc8-sae-j1850-normal-vs-zero
 * j1850 SAE crc8
 */
uint8_t crc8(const uint8_t *data, uint8_t len) {
	uint8_t crc = 0;

	if (data == 0)
		return 0;
	crc ^= 0xff;
	while (len--) {
		crc ^= *data++;
		for (unsigned k = 0; k < 8; k++)
			crc = crc & 0x80 ? (crc << 1) ^ 0x1d : crc << 1;
	}
	crc &= 0xff;
	crc ^= 0xff;
	return crc;
}