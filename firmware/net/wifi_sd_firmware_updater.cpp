#include "pch.h"

#if EFI_WIFI

#include "ff.h"
#include "mmc_card.h"
#include "spi_flash/include/spi_flash.h"
#include "driver/include/m2m_wifi.h"
#include "programmer/programmer.h"

static chibios_rt::BinarySemaphore sdUpdateDoneSemaphore(/* taken =*/true);

void signalWifiSdUpdateComplete() {
	sdUpdateDoneSemaphore.signal();
}

void waitForWifiSdUpdateComplete() {
	sdUpdateDoneSemaphore.wait();
}

void tryUpdateWifiFirmwareFromSd() {
	FILINFO finfo;
	if (f_stat("atwinc1500.bin", &finfo) != FR_OK) {
		efiPrintf("WiFi: atwinc1500.bin not found, skipping update.");
		return; // no file, nothing to do
	}

	Timer t;
	t.reset();

	efiPrintf("WiFi: found atwinc1500.bin (%lu bytes), updating...", (uint32_t)finfo.fsize);

	if (m2m_wifi_download_mode() != M2M_SUCCESS) {
		efiPrintf("WiFi: failed to enter download mode");
		return;
	}

	FIL* fd = sd_mem::getLogFileFd(); // reuse DMA-safe FIL
	if (f_open(fd, "atwinc1500.bin", FA_READ) != FR_OK) {
		efiPrintf("WiFi: failed to open file");
		m2m_wifi_deinit(nullptr);
		return;
	}

	uint32_t fileSize = finfo.fsize;
	uint32_t eraseSize = ((fileSize + FLASH_SECTOR_SZ - 1) / FLASH_SECTOR_SZ) * FLASH_SECTOR_SZ;

	efiPrintf("WiFi: erasing %lu bytes", eraseSize);
	if (spi_flash_erase(0, eraseSize) != M2M_SUCCESS) {
		efiPrintf("WiFi: erase failed");
		f_close(fd);
		m2m_wifi_deinit(nullptr);
		return;
	}

	auto& wifiUpdateBuffer = sd_mem::getWifiUpdateBuffer();

	uint32_t offset = 0;
	int lastPct = -1;
	while (offset < fileSize) {
		UINT bytesRead;
		if (f_read(fd, wifiUpdateBuffer.data(), wifiUpdateBuffer.size(), &bytesRead) != FR_OK || bytesRead == 0) {
			efiPrintf("WiFi: read error at offset %lu", offset);
			f_close(fd);
			m2m_wifi_deinit(nullptr);
			return;
		}

		if (spi_flash_write(wifiUpdateBuffer.data(), offset, bytesRead) != M2M_SUCCESS) {
			efiPrintf("WiFi: write failed at offset %lu", offset);
			f_close(fd);
			m2m_wifi_deinit(nullptr);
			return;
		}

		offset += bytesRead;
		int pct = offset * 100 / fileSize;
		if (pct / 5 != lastPct / 5) {
			efiPrintf("WiFi: writing %d%% (%lu / %lu)", pct, offset, fileSize);
			lastPct = pct;
		}
	}

	f_close(fd);

	m2m_wifi_deinit(nullptr);

	// Delete .done if it exists from a previous update, then rename
	f_unlink("atwinc1500.done");
	f_rename("atwinc1500.bin", "atwinc1500.done");
	efiPrintf("WiFi: firmware update complete in %.1f sec!", t.getElapsedSeconds());
}

void tryDumpWifiFirmwareToSd() {
	if (f_stat("atwinc1500.read", nullptr) != FR_OK) {
		return; // no trigger file, nothing to do
	}

	Timer t;
	t.reset();

	efiPrintf("WiFi: found atwinc1500.read, dumping firmware...");

	if (m2m_wifi_download_mode() != M2M_SUCCESS) {
		efiPrintf("WiFi: failed to enter download mode");
		m2m_wifi_deinit(nullptr);
		return;
	}

	uint32_t flashSize = programmer_get_flash_size();
	efiPrintf("WiFi: flash size %lu bytes", flashSize);

	FIL* fd = sd_mem::getLogFileFd();
	f_unlink("atwinc1500_dump.bin");
	if (f_open(fd, "atwinc1500_dump.bin", FA_CREATE_NEW | FA_WRITE) != FR_OK) {
		efiPrintf("WiFi: failed to create dump file");
		m2m_wifi_deinit(nullptr);
		return;
	}

	auto& wifiUpdateBuffer = sd_mem::getWifiUpdateBuffer();

	uint32_t offset = 0;
	int lastPct = -1;
	while (offset < flashSize) {
		uint32_t chunkSize = flashSize - offset;
		if (chunkSize > wifiUpdateBuffer.size()) {
			chunkSize = wifiUpdateBuffer.size();
		}

		if (spi_flash_read(wifiUpdateBuffer.data(), offset, chunkSize) != M2M_SUCCESS) {
			efiPrintf("WiFi: read failed at offset %lu", offset);
			f_close(fd);
			m2m_wifi_deinit(nullptr);
			return;
		}

		UINT bytesWritten;
		if (f_write(fd, wifiUpdateBuffer.data(), chunkSize, &bytesWritten) != FR_OK || bytesWritten != chunkSize) {
			efiPrintf("WiFi: file write error at offset %lu", offset);
			f_close(fd);
			m2m_wifi_deinit(nullptr);
			return;
		}

		offset += chunkSize;
		int pct = offset * 100 / flashSize;
		if (pct / 5 != lastPct / 5) {
			efiPrintf("WiFi: dumping %d%% (%lu / %lu)", pct, offset, flashSize);
			lastPct = pct;
		}
	}

	f_close(fd);
	m2m_wifi_deinit(nullptr);

	f_unlink("atwinc1500.read");

	efiPrintf("WiFi: firmware dump complete in %.1f sec!", t.getElapsedSeconds());
}

#endif // EFI_WIFI
