#include "pch.h"

#include "crc_accelerator.h"

#include <rusefi/crc.h>

static std::atomic_flag crcMutex;

bool tryAcquireCrc() {
	// equivalent to
	// return crcMutex.tryLock();
	return !crcMutex.test_and_set(std::memory_order_acquire);
}

void releaseCrc() {
	// equivalent to
	// crcMutex.unlock();
	crcMutex.clear(std::memory_order_release);
}

Crc::Crc()
	: m_acquiredExclusive(tryAcquireCrc())
{
}

Crc::~Crc() {
	if (m_acquiredExclusive) {
		releaseCrc();
	}
}

void Crc::addData(const void* buf, size_t size) {
	if (m_acquiredExclusive) {
		// TODO: use hardware CRC when we acquire exclusive use
		m_crc = crc32inc(buf, m_crc, size);
	} else {
		m_crc = crc32inc(buf, m_crc, size);
	}
}

uint32_t Crc::getCrc() const {
	return m_crc;
}
