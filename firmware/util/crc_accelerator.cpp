#include "pch.h"

#include "crc_accelerator.h"

#include <rusefi/crc.h>

#define USE_HW_CRC_THRESHOLD 150

static std::atomic_flag crcMutex;

static bool tryAcquireCrc(size_t estimatedWorkSize) {
	// For small jobs, don't use HW at all
	if (estimatedWorkSize < USE_HW_CRC_THRESHOLD) {
		return false;
	}

	// equivalent to
	// return crcMutex.tryLock();
	return !crcMutex.test_and_set(std::memory_order_acquire);
}

static void releaseCrc() {
	// equivalent to
	// crcMutex.unlock();
	crcMutex.clear(std::memory_order_release);
}

#if HAL_USE_CRC
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

#endif // HAL_USE_CRC

Crc::Crc(size_t estimatedWorkSize)
	: m_acquiredExclusive(tryAcquireCrc(estimatedWorkSize))
{
	#if HAL_USE_CRC
		if (m_acquiredExclusive) {
			if (didInit) {
				crcResetI(&CRCD1);
			} else {
				didInit = true;

				crcStart(&CRCD1, &crcCfg);
			}
		}
	#endif // HAL_USE_CRC
}

Crc::~Crc() {
	if (m_acquiredExclusive) {
		releaseCrc();
	}
}

void Crc::addData(const void* buf, size_t size) {
	#if HAL_USE_CRC
		if (m_acquiredExclusive) {
			m_crc = crcCalcI(&CRCD1, size, buf);
			return;
		}
	#endif // HAL_USE_CRC

	// fall through to software CRC if hardware not available
	m_crc = crc32inc(buf, m_crc, size);
}

uint32_t Crc::getCrc() const {
	return m_crc;
}
