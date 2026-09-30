#include "pch.h"
#include "flash_int.h"
#include "flash_h7_test_hardware.h"

#if defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#endif

// Include the actual driver under its own namespace, avoiding the fake flash
// symbols used by configuration-storage tests. Suppress only logging/Timer code.
#undef EFI_INTERNAL_FLASH
#define EFI_INTERNAL_FLASH 1
#define EFI_BOOTLOADER 1
namespace h7_driver_test {
#include "../../../firmware/hw_layer/ports/stm32/flash_int_h7.cpp"
}
#undef EFI_BOOTLOADER
#undef __DSB
#undef __ISB

namespace {
using namespace h7_driver_test;

class FlashH7 : public testing::Test {
protected:
	void SetUp() override {
		reset();
	}
	flashaddr_t address(unsigned bank = 1, size_t offset = 0) {
		return FLASH_BASE + bank * bankSize + offset;
	}
	void expectClean(unsigned bank = 1) {
		auto control = bank ? registers.CR2 : registers.CR1;
		EXPECT_TRUE(control & FLASH_CR_LOCK);
		EXPECT_FALSE(control & FLASH_CR_PG);
	}
};

TEST_F(FlashH7, ClearsErrorsInTheSelectedBank) {
	registers.SR1 = registers.SR2 = FLASH_SR_OPERR | FLASH_SR_INCERR | FLASH_SR_DBECCERR;
	intFlashClearErrors(1);
	EXPECT_EQ(registers.CCR2.writes, 1u);
	EXPECT_EQ(registers.CCR1.writes, 0u);
	EXPECT_EQ(registers.SR2, 0u);
	EXPECT_NE(registers.SR1, 0u);
	intFlashClearErrors(0);
	EXPECT_EQ(registers.CCR1.writes, 1u);
	EXPECT_EQ(registers.SR1, 0u);
}

TEST_F(FlashH7, ZeroSizeDoesNotUnlockOrDereferenceAnything) {
	EXPECT_EQ(h7_driver_test::intFlashWrite(1, nullptr, 0), FLASH_RETURN_SUCCESS);
	EXPECT_EQ(registers.KEYR1.writes + registers.KEYR2.writes, 0u);
	EXPECT_EQ(waits, 0u);
	EXPECT_EQ(completedWords, 0u);
}

TEST_F(FlashH7, RejectsUnalignedDestinationAndOutOfBankRangesBeforeUnlock) {
	std::array<char, 64> source{};
	EXPECT_EQ(h7_driver_test::intFlashWrite(address() + 1, source.data(), 32), FLASH_RETURN_ALIGNERROR);
	EXPECT_EQ(h7_driver_test::intFlashWrite(address(), nullptr, 32), FLASH_RETURN_NO_PERMISSION);
	EXPECT_EQ(h7_driver_test::intFlashWrite(address(0, bankSize - 32), source.data(), 33), FLASH_RETURN_NO_PERMISSION);
	EXPECT_EQ(h7_driver_test::intFlashWrite(address(1, bankSize - 32), source.data(), 33), FLASH_RETURN_NO_PERMISSION);
	EXPECT_EQ(h7_driver_test::intFlashWrite(address(), source.data(), SIZE_MAX), FLASH_RETURN_NO_PERMISSION);
	EXPECT_EQ(registers.KEYR1.writes + registers.KEYR2.writes, 0u);
	EXPECT_EQ(completedWords, 0u);
}

TEST_F(FlashH7, PadsPartialWordsAndAcceptsUnalignedSources) {
	for (size_t size : {size_t(1), size_t(31), size_t(32), size_t(33), size_t(63), size_t(64)}) {
		for (unsigned bank : {0u, 1u}) {
			reset();
			std::array<uint8_t, 100> source;
			source.fill(0x23);
			for (size_t i = 0; i < size; ++i) {
				source[i + 1] = uint8_t(i);
			}
			ASSERT_EQ(
					h7_driver_test::intFlashWrite(address(bank), reinterpret_cast<char*>(source.data() + 1), size),
					FLASH_RETURN_SUCCESS);
			auto destination = reinterpret_cast<const uint8_t*>(address(bank));
			EXPECT_EQ(memcmp(destination, source.data() + 1, size), 0);
			size_t paddedSize = (size + 31) / 32 * 32;
			for (size_t i = size; i < paddedSize + 32; ++i) {
				EXPECT_EQ(destination[i], 0xff);
			}
			EXPECT_EQ(completedWords, paddedSize / 32);
			expectClean(bank);
		}
	}
}

TEST_F(FlashH7, PartialSourceEndsAtUnreadablePage) {
#if defined(__unix__)
	long pageSize = sysconf(_SC_PAGESIZE);
	ASSERT_GT(pageSize, 0);
	void* mapping = mmap(nullptr, 2 * pageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(mapping, MAP_FAILED);
	ASSERT_EQ(mprotect(static_cast<char*>(mapping) + pageSize, pageSize, PROT_NONE), 0);
	char* source = static_cast<char*>(mapping) + pageSize - 31;
	memset(source, 0x36, 31);
	EXPECT_EQ(h7_driver_test::intFlashWrite(address(), source, 31), FLASH_RETURN_SUCCESS);
	EXPECT_EQ(memcmp(reinterpret_cast<void*>(address()), source, 31), 0);
	EXPECT_EQ(*reinterpret_cast<uint8_t*>(address() + 31), 0xff);
	EXPECT_EQ(munmap(mapping, 2 * pageSize), 0);
#else
	GTEST_SKIP() << "Guard-page validation requires mmap/mprotect";
#endif
}

class FlashH7Errors : public FlashH7, public testing::WithParamInterface<uint32_t> {};

TEST_P(FlashH7Errors, StopsAfterFailedFlashWordAndAlwaysClearsProgramAndLocks) {
	std::array<char, 96> source;
	source.fill(0x56);
	wordErrors = {0, GetParam()};
	EXPECT_NE(h7_driver_test::intFlashWrite(address(), source.data(), source.size()), FLASH_RETURN_SUCCESS);
	EXPECT_EQ(completedWords, 2u);
	EXPECT_EQ(memcmp(reinterpret_cast<void*>(address()), source.data(), 64), 0);
	EXPECT_TRUE(h7_driver_test::intFlashIsErased(address(1, 64), 32));
	expectClean();
}

INSTANTIATE_TEST_SUITE_P(
		AllStatusErrors,
		FlashH7Errors,
		testing::Values(
				FLASH_SR_OPERR,
				FLASH_SR_WRPERR,
				FLASH_SR_PGSERR,
				FLASH_SR_STRBERR,
				FLASH_SR_INCERR,
				FLASH_SR_RDPERR,
				FLASH_SR_RDSERR,
				FLASH_SR_SNECCERR,
				FLASH_SR_DBECCERR,
				FLASH_SR_CRCRDERR));

TEST_F(FlashH7, ClearsStaleStatusBeforeProgramming) {
	std::array<char, 32> source{};
	registers.SR2 = FLASH_SR_OPERR | FLASH_SR_PGSERR;
	EXPECT_EQ(h7_driver_test::intFlashWrite(address(), source.data(), source.size()), FLASH_RETURN_SUCCESS);
	EXPECT_EQ(registers.CCR2.writes, 1u);
	EXPECT_EQ(registers.SR2, FLASH_SR_EOP);
	expectClean();
}

TEST_F(FlashH7, UnlockFailureDoesNotProgramOrTouchOtherBank) {
	std::array<char, 32> source{};
	rejectUnlock = true;
	EXPECT_EQ(h7_driver_test::intFlashWrite(address(), source.data(), source.size()), FLASH_RETURN_NO_PERMISSION);
	EXPECT_EQ(registers.KEYR1.writes, 0u);
	EXPECT_EQ(registers.KEYR2.writes, 2u);
	EXPECT_EQ(completedWords, 0u);
	expectClean();
}

TEST_F(FlashH7, EraseChecksH7SpecificErrorsAndLocks) {
	eraseError = FLASH_SR_INCERR;
	EXPECT_EQ(h7_driver_test::intFlashSectorErase(8), FLASH_RETURN_PSEQERROR);
	EXPECT_EQ(registers.CCR2.writes, 1u);
	EXPECT_FALSE(registers.CR2 & FLASH_CR_SER);
	EXPECT_EQ(cacheDisables, 1u);
	EXPECT_EQ(cacheEnables, 1u);
	expectClean();
}
} // namespace
