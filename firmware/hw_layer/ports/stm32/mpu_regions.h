/**
 * @file mpu_regions.h
 * @brief Every MPU region number used on every STM32 we run on, in one place.
 *
 * Where regions overlap, the higher number wins.
 *
 *   0, 1  free
 *   2     The tune copies in flash: strongly ordered, XN, no access except inside a ScopeFlashAccess
 *         (flash_int_common.cpp). All chips.
 *   3     Fast ADC (knock) sample buffer, non-cacheable. H7.
 *   4     System memory (ST bootloader, UID): strongly ordered, XN, read-only. H7.
 *   5     DMA buffers, non-cacheable (dma_buffers.cpp). F7, H7.
 *   6, 7  ChibiOS, set per chip:
 *         F4: 6 stack guard pages (PORT_USE_GUARD_MPU_REGION, cfg/chconf.h), 7 free
 *         F7: 6 stack guard pages, 7 SRAM2 no-cache (fixed in ChibiOS's hal_lld.c)
 *         H7: 6 SRAM3 no-cache (STM32_NOCACHE_MPU_REGION, cfg/mcuconf.h), 7 stack guard pages
 *         The stack guard has to outrank the no-cache region, since NO_CACHE objects include a
 *         thread (WiFi) whose stack's guard page would otherwise be overridden. F7 can't, because
 *         ChibiOS fixes its no-cache region at 7.
 */

#pragma once

#define MPU_REGION_FLASH_CONFIG MPU_REGION_2
#define MPU_REGION_FAST_ADC MPU_REGION_3
#define MPU_REGION_SYSTEM_MEMORY MPU_REGION_4
#define MPU_REGION_DMA_BUFFERS MPU_REGION_5

// ChibiOS's regions must stay clear of ours
#if defined(PORT_USE_GUARD_MPU_REGION) && PORT_USE_GUARD_MPU_REGION < 6
#error "The stack guard MPU region collides with one of ours, see mpu_regions.h"
#endif
#if defined(STM32_NOCACHE_MPU_REGION) && STM32_NOCACHE_MPU_REGION < 6
#error "The no-cache MPU region collides with one of ours, see mpu_regions.h"
#endif
#if defined(STM32_NOCACHE_MPU_REGION) && defined(PORT_USE_GUARD_MPU_REGION) &&                                         \
		STM32_NOCACHE_MPU_REGION >= PORT_USE_GUARD_MPU_REGION
#error "The stack guard MPU region must outrank the no-cache region, see mpu_regions.h"
#endif
