#pragma once

// Memory protection/register access is outside the host lifecycle fixture.
constexpr int MPU_RASR_SIZE_8K = 0;
constexpr int MPU_REGION_3 = 0;
constexpr int MPU_RASR_ATTR_AP_RW_RW = 0;
constexpr int MPU_RASR_ATTR_NON_CACHEABLE = 0;
constexpr int MPU_RASR_ATTR_S = 0;
constexpr int MPU_RASR_ENABLE = 0;
constexpr int MPU_CTRL_PRIVDEFENA = 0;
inline void mpuConfigureRegion(int, void*, uint32_t) {}
inline void mpuEnable(int) {}
inline void SCB_CleanInvalidateDCache() {}
constexpr uint32_t SYSCFG_PMCR_PA0SO = 1;
constexpr uint32_t SYSCFG_PMCR_PA1SO = 2;
constexpr uint32_t SYSCFG_PMCR_PC2SO = 4;
constexpr uint32_t SYSCFG_PMCR_PC3SO = 8;
struct MockSyscfg {
	uint32_t PMCR = 0;
};
inline MockSyscfg syscfg;
inline auto* SYSCFG = &syscfg;
