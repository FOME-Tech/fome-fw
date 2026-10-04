#include "pch.h"
#include "flash_int.h"

#include <array>
#include <vector>

// Exercise the common production driver with the CMSIS cache boundary mocked.
// The vendored core_cm7.h helper starts at addr and steps once per 32 bytes of
// dsize, so an unaligned start must include its offset in the supplied length.
namespace flash_cache_test {
using flashdata_t = uint32_t;
struct Invalidation {
	flashaddr_t address;
	size_t size;
};
static std::vector<Invalidation> invalidations;
void SCB_InvalidateDCache_by_Addr(uint32_t* address, int32_t size) {
	invalidations.push_back({reinterpret_cast<flashaddr_t>(address), size_t(size)});
}
flashaddr_t intFlashSectorBegin(flashsector_t sector) {
	return sector * 1024;
}
size_t flashSectorSize(flashsector_t) {
	return 1024;
}
int intFlashSectorErase(flashsector_t) {
	return FLASH_RETURN_SUCCESS;
}

#undef EFI_INTERNAL_FLASH
#define EFI_INTERNAL_FLASH 1
#undef CORTEX_MODEL
#define CORTEX_MODEL 7
#include "../../../firmware/hw_layer/ports/stm32/flash_int_common.cpp"
#undef CORTEX_MODEL
} // namespace flash_cache_test

namespace {
class FlashCache : public testing::Test {
protected:
	void SetUp() override {
		flash_cache_test::invalidations.clear();
		memory.fill(char(0xff));
	}
	flashaddr_t address(size_t offset = 0) {
		return reinterpret_cast<flashaddr_t>(memory.data() + offset);
	}
	void expectInvalidation(size_t offset, size_t size) {
		ASSERT_EQ(flash_cache_test::invalidations.size(), 1u);
		auto invalidation = flash_cache_test::invalidations.front();
		EXPECT_EQ(invalidation.address, address(offset / 32 * 32));
		EXPECT_EQ(invalidation.size, size + offset % 32);
		// This is the address range actually visited by the vendored CMSIS loop.
		auto end = invalidation.address + (invalidation.size + 31) / 32 * 32;
		EXPECT_GE(end, address(offset) + size);
	}
	alignas(32) std::array<char, 256> memory;
};

TEST_F(FlashCache, PayloadChunksInvalidateTheirTrailingCacheLine) {
	std::array<char, 128> destination;
	EXPECT_EQ(flash_cache_test::intFlashRead(address(8), destination.data(), destination.size()), FLASH_RETURN_SUCCESS);
	expectInvalidation(8, 128);
	EXPECT_EQ(memcmp(destination.data(), memory.data() + 8, destination.size()), 0);
}

TEST_F(FlashCache, HeaderTrailerAndSmallReadsCoverAllIntersectingLines) {
	for (auto range : {std::pair<size_t, size_t>{0, 8}, {28, 4}, {31, 2}, {32, 1}, {60, 8}, {136, 4}}) {
		SCOPED_TRACE(range.first);
		flash_cache_test::invalidations.clear();
		std::array<char, 8> destination;
		EXPECT_EQ(
				flash_cache_test::intFlashRead(address(range.first), destination.data(), range.second),
				FLASH_RETURN_SUCCESS);
		expectInvalidation(range.first, range.second);
	}
}

TEST_F(FlashCache, CompareAndErasedChecksUseTheSameRange) {
	EXPECT_TRUE(flash_cache_test::intFlashCompare(address(8), memory.data() + 8, 128));
	expectInvalidation(8, 128);
	flash_cache_test::invalidations.clear();
	EXPECT_TRUE(flash_cache_test::intFlashIsErased(address(8), 128));
	expectInvalidation(8, 128);
}

TEST_F(FlashCache, ZeroLengthOperationsSkipCacheMaintenance) {
	char destination;
	EXPECT_EQ(flash_cache_test::intFlashRead(address(8), &destination, 0), FLASH_RETURN_SUCCESS);
	EXPECT_TRUE(flash_cache_test::intFlashCompare(address(8), &destination, 0));
	EXPECT_TRUE(flash_cache_test::intFlashIsErased(address(8), 0));
	EXPECT_TRUE(flash_cache_test::invalidations.empty());
}
} // namespace
