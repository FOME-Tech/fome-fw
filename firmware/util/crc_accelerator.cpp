#include "pch.h"

#include "crc_accelerator.h"

#include <rusefi/crc.h>

static std::atomic_flag crcMutex;

static bool tryAcquireCrc() {
	// equivalent to
	// return crcMutex.tryLock();
	return !crcMutex.test_and_set(std::memory_order_acquire);
}

static void releaseCrc() {
	// equivalent to
	// crcMutex.unlock();
	crcMutex.clear(std::memory_order_release);
}

#if STM32_CRC_USE_CRC1
static const CRCConfig crcCfg = {
	.poly_size			= 32,
	.poly				= 0x04C11DB7,
	.initial_val		= 0xFFFFFFFF,
	.final_val			= 0xFFFFFFFF,
	.reflect_data		= true,
	.reflect_remainder	= true,
	.end_cb				= nullptr,
};

static bool didInit = false;

#endif // STM32_CRC_USE_CRC1

Crc::Crc()
	: m_acquiredExclusive(tryAcquireCrc())
{
	#if STM32_CRC_USE_CRC1
		if (m_acquiredExclusive) {
			if (didInit) {
				crcReset(&CRCD1);
			} else {
				didInit = true;

				crcStart(&CRCD1, &crcCfg);
			}
		}
	#endif // STM32_CRC_USE_CRC1
}

Crc::~Crc() {
	if (m_acquiredExclusive) {
		releaseCrc();
	}
}

void Crc::addData(const void* buf, size_t size) {
	#if STM32_CRC_USE_CRC1
		if (m_acquiredExclusive) {
			m_crc = crcCalc(&CRCD1, size, buf);
			return;
		}
	#endif // STM32_CRC_USE_CRC1

	// fall through to software CRC if hardware not available
	m_crc = crc32inc(buf, m_crc, size);
}

uint32_t Crc::getCrc() const {
	return m_crc;
}
