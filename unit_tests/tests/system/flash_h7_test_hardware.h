#pragma once

#include <array>
#include <vector>

// Register bits from the vendored CMSIS STM32H7xx/stm32h743xx.h. Only the
// hardware boundary is mocked; test_flash_h7.cpp includes the production driver.
#define FLASH_CR_LOCK (1u << 0)
#define FLASH_CR_PG (1u << 1)
#define FLASH_CR_SER (1u << 2)
#define FLASH_CR_PSIZE (3u << 4)
#define FLASH_CR_PSIZE_MASK FLASH_CR_PSIZE
#define FLASH_CR_PSIZE_VALUE (2u << 4)
#define FLASH_CR_START (1u << 7)
#define FLASH_CR_SNB_Pos 8
#define FLASH_CR_SNB (7u << FLASH_CR_SNB_Pos)
#define FLASH_SR_QW (1u << 2)
#define FLASH_SR_EOP (1u << 16)
#define FLASH_SR_WRPERR (1u << 17)
#define FLASH_SR_PGSERR (1u << 18)
#define FLASH_SR_STRBERR (1u << 19)
#define FLASH_SR_INCERR (1u << 21)
#define FLASH_SR_OPERR (1u << 22)
#define FLASH_SR_RDPERR (1u << 23)
#define FLASH_SR_RDSERR (1u << 24)
#define FLASH_SR_SNECCERR (1u << 25)
#define FLASH_SR_DBECCERR (1u << 26)
#define FLASH_SR_CRCEND (1u << 27)
#define FLASH_SR_CRCRDERR (1u << 28)
#define FLASH_CCR_CLR_EOP FLASH_SR_EOP
#define FLASH_CCR_CLR_WRPERR FLASH_SR_WRPERR
#define FLASH_CCR_CLR_PGSERR FLASH_SR_PGSERR
#define FLASH_CCR_CLR_STRBERR FLASH_SR_STRBERR
#define FLASH_CCR_CLR_INCERR FLASH_SR_INCERR
#define FLASH_CCR_CLR_OPERR FLASH_SR_OPERR
#define FLASH_CCR_CLR_RDPERR FLASH_SR_RDPERR
#define FLASH_CCR_CLR_RDSERR FLASH_SR_RDSERR
#define FLASH_CCR_CLR_SNECCERR FLASH_SR_SNECCERR
#define FLASH_CCR_CLR_DBECCERR FLASH_SR_DBECCERR
#define FLASH_CCR_CLR_CRCEND FLASH_SR_CRCEND
#define FLASH_CCR_CLR_CRCRDERR FLASH_SR_CRCRDERR
#define HAL_FAILED true

namespace h7_driver_test {
using flashdata_t = uint32_t;
constexpr size_t bankSize = 1024 * 1024;
alignas(32) inline std::array<uint8_t, 2 * bankSize> memory;
inline bool rejectUnlock = false;

struct ClearRegister {
	uint32_t& status;
	unsigned writes = 0;
	uint32_t lastWrite = 0;
	ClearRegister& operator=(uint32_t value) {
		++writes;
		lastWrite = value;
		status &= ~value;
		return *this;
	}
};

struct KeyRegister {
	uint32_t& control;
	uint32_t previous = 0;
	unsigned writes = 0;
	KeyRegister& operator=(uint32_t value) {
		++writes;
		if (!rejectUnlock && previous == 0x45670123 && value == 0xCDEF89AB) {
			control &= ~FLASH_CR_LOCK;
		}
		previous = value;
		return *this;
	}
};

struct Registers {
	uint32_t CR1 = FLASH_CR_LOCK;
	uint32_t CR2 = FLASH_CR_LOCK;
	uint32_t SR1 = 0;
	uint32_t SR2 = 0;
	ClearRegister CCR1{SR1}, CCR2{SR2};
	KeyRegister KEYR1{CR1}, KEYR2{CR2};
};

inline Registers registers;
inline std::vector<uint32_t> wordErrors;
inline unsigned completedWords = 0;
inline unsigned waits = 0;
inline unsigned barriers = 0;
inline unsigned cacheDisables = 0;
inline unsigned cacheEnables = 0;
inline uint32_t eraseError = 0;

inline void reset() {
	memory.fill(0xff);
	registers.CR1 = registers.CR2 = FLASH_CR_LOCK;
	registers.SR1 = registers.SR2 = 0;
	registers.CCR1.writes = registers.CCR2.writes = 0;
	registers.KEYR1.writes = registers.KEYR2.writes = 0;
	registers.CCR1.lastWrite = registers.CCR2.lastWrite = 0;
	registers.KEYR1.previous = registers.KEYR2.previous = 0;
	rejectUnlock = false;
	wordErrors.clear();
	completedWords = waits = barriers = cacheDisables = cacheEnables = 0;
	eraseError = 0;
}

inline size_t flashSectorSize(flashsector_t) {
	return 128 * 1024;
}
inline flashsector_t intFlashSectorAt(flashaddr_t address) {
	return (address - reinterpret_cast<flashaddr_t>(memory.data())) / flashSectorSize(0);
}
inline bool intFlashIsErased(flashaddr_t address, size_t size) {
	auto bytes = reinterpret_cast<const uint8_t*>(address);
	return std::all_of(bytes, bytes + size, [](uint8_t value) { return value == 0xff; });
}

inline void chThdSleepMilliseconds(int) {
	++waits;
	for (unsigned bank = 0; bank < 2; ++bank) {
		auto& control = bank ? registers.CR2 : registers.CR1;
		auto& status = bank ? registers.SR2 : registers.SR1;
		status &= ~FLASH_SR_QW;
		if (control & FLASH_CR_PG) {
			if (completedWords < wordErrors.size()) {
				status |= wordErrors[completedWords];
			}
			++completedWords;
			status |= FLASH_SR_EOP;
		}
		if ((control & (FLASH_CR_SER | FLASH_CR_START)) == (FLASH_CR_SER | FLASH_CR_START)) {
			unsigned sector = (control & FLASH_CR_SNB) >> FLASH_CR_SNB_Pos;
			memset(memory.data() + bank * bankSize + sector * flashSectorSize(0), 0xff, flashSectorSize(0));
			control &= ~FLASH_CR_START;
			status |= eraseError;
		}
	}
}

inline void barrier() {
	++barriers;
}
inline void SCB_DisableICache() {
	++cacheDisables;
}
inline void SCB_InvalidateICache() {}
inline void SCB_EnableICache() {
	++cacheEnables;
}
} // namespace h7_driver_test

#define FLASH (&h7_driver_test::registers)
#define FLASH_BASE (reinterpret_cast<flashaddr_t>(h7_driver_test::memory.data()))
#define FLASH_BANK2_BASE (FLASH_BASE + h7_driver_test::bankSize)
#define FLASH_END (FLASH_BASE + h7_driver_test::memory.size() - 1)
#define __DSB() h7_driver_test::barrier()
#define __ISB() h7_driver_test::barrier()
