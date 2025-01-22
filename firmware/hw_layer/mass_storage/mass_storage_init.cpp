#include "pch.h"

#include "mass_storage_init.h"
#include "mass_storage_device.h"
#include "null_device.h"
#include "block_cache.h"

#if HAL_USE_USB_MSD

#if EFI_EMBED_INI_MSD
	#ifdef EFI_USE_COMPRESSED_INI_MSD
		#include "compressed_block_device.h"
		#include "ramdisk_image_compressed.h"
	#else
		#include "ramdisk.h"
		#include "ramdisk_image.h"
	#endif

	// If the ramdisk image told us not to use it, don't use it.
	#ifdef RAMDISK_INVALID
		#undef EFI_EMBED_INI_MSD
		#define EFI_EMBED_INI_MSD FALSE
	#endif
#endif

#if EFI_EMBED_INI_MSD
	#ifdef EFI_USE_COMPRESSED_INI_MSD
		static CompressedBlockDevice cbd;
	#else
		static RamDisk ramdisk;
	#endif
#endif

#if STM32_USB_USE_OTG1
  USBDriver *usb_driver = &USBD1;
#elif STM32_USB_USE_OTG2
  USBDriver *usb_driver = &USBD2;
#else
  #error MSD needs OTG1 or OTG2 to be enabled
#endif

// One block buffer per LUN
static NO_CACHE uint8_t blkbufIni[MMCSD_BLOCK_SIZE];
static struct {
	uint8_t sdmmc[MMCSD_BLOCK_SIZE];
	uint8_t prefetch[MMCSD_BLOCK_SIZE];
} sdBuffers SDMMC_MEMORY;

static CCM_OPTIONAL MassStorageController msd(usb_driver);

static const scsi_inquiry_response_t iniDriveInquiry = {
    0x00,           /* direct access block device     */
    0x80,           /* removable                      */
    0x04,           /* SPC-2                          */
    0x02,           /* response data format           */
    0x20,           /* response has 0x20 + 4 bytes    */
    0x00,
    0x00,
    0x00,
    "FOME",
    "INI Drive",
    {'v',CH_KERNEL_MAJOR+'0','.',CH_KERNEL_MINOR+'0'}
};

static const scsi_inquiry_response_t sdCardInquiry = {
    0x00,           /* direct access block device     */
    0x80,           /* removable                      */
    0x04,           /* SPC-2                          */
    0x02,           /* response data format           */
    0x20,           /* response has 0x20 + 4 bytes    */
    0x00,
    0x00,
    0x00,
    "FOME",
    "SD Card",
    {'v',CH_KERNEL_MAJOR+'0','.',CH_KERNEL_MINOR+'0'}
};

static BlockCache sdReadPrefetch(sdBuffers.prefetch);

void attachMsdSdCard(BaseBlockDevice* blkdev) {
	// Start the prefetcher
	sdReadPrefetch.start(blkdev);
	msd.attachLun(1, reinterpret_cast<BaseBlockDevice*>(&sdReadPrefetch), sdBuffers.sdmmc, &sdCardInquiry, nullptr);

#if EFI_TUNER_STUDIO
	// SD MSD attached, enable indicator in TS
	engine->outputChannels.sd_msd = true;
#endif
}

static BaseBlockDevice* getRamdiskDevice() {
#if EFI_EMBED_INI_MSD
#ifdef EFI_USE_COMPRESSED_INI_MSD
	uzlib_init();
	compressedBlockDeviceObjectInit(&cbd);
	compressedBlockDeviceStart(&cbd, ramdisk_image_gz, sizeof(ramdisk_image_gz));

	return (BaseBlockDevice*)&cbd;
#else // not EFI_USE_COMPRESSED_INI_MSD
	ramdiskObjectInit(&ramdisk);

	constexpr size_t ramdiskSize = sizeof(ramdisk_image);
	constexpr size_t blockSize = 512;
	constexpr size_t blockCount = ramdiskSize / blockSize;

	// Ramdisk should be a round number of blocks
	static_assert(ramdiskSize % blockSize == 0);

	ramdiskStart(&ramdisk, const_cast<uint8_t*>(ramdisk_image), blockSize, blockCount, /*readonly =*/ true);

	return (BaseBlockDevice*)&ramdisk;
#endif // EFI_USE_COMPRESSED_INI_MSD
#else // not EFI_EMBED_INI_MSD
	// No embedded ini file, just mount the null device instead
	return (BaseBlockDevice*)&ND1;
#endif
}

void initUsbMsd() {
	// STM32H7 SDMMC1 needs the filesystem object to be in AXI
	// SRAM, but excluded from the cache
	#ifdef STM32H7XX
	{
		void* base = &sdBuffers;
		static_assert(sizeof(sdBuffers) == 1024);
		uint32_t size = MPU_RASR_SIZE_1K;

		mpuConfigureRegion(MPU_REGION_4,
						base,
						MPU_RASR_ATTR_AP_RW_RW |
						MPU_RASR_ATTR_NON_CACHEABLE |
						MPU_RASR_ATTR_S |
						size |
						MPU_RASR_ENABLE);
		mpuEnable(MPU_CTRL_PRIVDEFENA);

		/* Invalidating data cache to make sure that the MPU settings are taken
		immediately.*/
		SCB_CleanInvalidateDCache();
	}
	#endif

	// Attach the ini ramdisk
	msd.attachLun(0, getRamdiskDevice(), blkbufIni, &iniDriveInquiry, nullptr);

	// attach a null device in place of the SD card for now - the SD thread may replace it later
	msd.attachLun(1, (BaseBlockDevice*)&ND1, sdBuffers.sdmmc, &sdCardInquiry, nullptr);

	// start the mass storage thread
	msd.start();
}

#endif // HAL_USE_USB_MSD
