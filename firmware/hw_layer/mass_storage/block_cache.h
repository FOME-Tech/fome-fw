#pragma once

#include "ch.hpp"
#include "hal.h"

#if HAL_USE_USB_MSD

struct BlockCache {
	const BaseBlockDeviceVMT* vmt;
	_base_block_device_data

	BaseBlockDevice* m_backing;
	chibios_rt::Mutex m_deviceMutex;

	BlockCache(uint8_t* cachedBlockData);

	void start(BaseBlockDevice* cachedBlockData);

	bool read(uint32_t startblk, uint8_t* buffer);

private:
	bool fetchBlock(uint32_t blockId);

	struct Handle {
		// Read request parameters
		uint32_t startblk;
		uint8_t* buffer;

		// Returned result
		bool result;
	};

	static constexpr int handleCount = 1;

	chibios_rt::Mailbox<Handle*, handleCount> m_requests;
	chibios_rt::Mailbox<Handle*, handleCount> m_completed;
	chibios_rt::Mailbox<Handle*, handleCount> m_free;

	Handle m_handles[handleCount];

	int32_t m_cachedBlockId = -1;
	uint8_t* const m_cachedBlockData;

	// Worker thread that performs actual read operations in the background
	void readThread();
	THD_WORKING_AREA(waRead, USB_MSD_THREAD_WA_SIZE);
};

#endif // HAL_USE_USB_MSD
