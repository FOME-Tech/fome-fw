#include "pch.h"

#include "block_cache.h"

#if HAL_USE_USB_MSD

static bool is_inserted(void* instance) {
	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	chibios_rt::MutexLocker lock(bc->m_deviceMutex);

	// ask the underlying device, otherwise false
	auto backing = bc->m_backing;
	return backing ? backing->vmt->is_inserted(backing) : false;
}

static bool is_protected(void* instance) {
	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	chibios_rt::MutexLocker lock(bc->m_deviceMutex);

	// ask the underlying device, otherwise true
	auto backing = bc->m_backing;
	return backing ? backing->vmt->is_protected(backing) : true;
}

static bool connect(void* instance) {
	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	chibios_rt::MutexLocker lock(bc->m_deviceMutex);

	if (auto backing = bc->m_backing) {
		return backing->vmt->connect(backing);
	}

	return HAL_FAILED;
}

static bool disconnect(void* instance) {
	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	chibios_rt::MutexLocker lock(bc->m_deviceMutex);

	if (auto backing = bc->m_backing) {
		return backing->vmt->disconnect(backing);
	}

	return HAL_FAILED;
}

static bool read(void* instance, uint32_t startblk, uint8_t* buffer, uint32_t n) {
	if (n != 1) {
		return HAL_FAILED;
	}

	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	return bc->read(startblk, buffer);
}

bool BlockCache::read(uint32_t startblk, uint8_t* buffer) {
	if (!m_backing) {
		return HAL_FAILED;
	}

	Handle* h;
	m_free.fetch(&h, TIME_INFINITE);

	// copy request information
	h->startblk = startblk;
	h->buffer = buffer;

	// make the request
	m_requests.post(h, TIME_INFINITE);
	// wait for it to complete
	m_completed.fetch(&h, TIME_INFINITE);

	// stash the result
	auto result = h->result;

	// return the handle to the free list
	m_free.post(h, TIME_INFINITE);

	return result;
}

static bool write(void* instance, uint32_t startblk, const uint8_t* buffer, uint32_t n) {
	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	if (!bc->m_backing) {
		return HAL_FAILED;
	}

	chibios_rt::MutexLocker lock(bc->m_deviceMutex);
	return bc->m_backing->vmt->write(bc->m_backing, startblk, buffer, n);
}

bool BlockCache::fetchBlock(uint32_t blockId) {
	chibios_rt::MutexLocker lock(m_deviceMutex);

	auto result = m_backing->vmt->read(m_backing, blockId, m_cachedBlockData, 1);

	if (result == HAL_SUCCESS) {
		// read succeeded, mark cache as valid
		m_cachedBlockId = blockId;
	} else {
		// read failed, invalidate cache
		m_cachedBlockId = -1;
	}

	return result;
}

void BlockCache::readThread() {
	while (true) {
		Handle* h;
		
		// Wait for a request to come in
		m_requests.fetch(&h, TIME_INFINITE);

		auto startblk = h->startblk;

		// Did we prefetch the wrong block?
		if (startblk != m_cachedBlockId) {
			// Cache miss, fetch the correct block
			h->result = fetchBlock(startblk);
		} else {
			// Cache hit, the correct block is already loaded!
			h->result = HAL_SUCCESS;
		}

		// Copy from the cache to the output buffer
		memcpy(h->buffer, m_cachedBlockData, 512);

		// return the completed request
		m_completed.post(h, TIME_INFINITE);

		// Now that we have returned the requested block, prefetch the next block
		startblk++;
		fetchBlock(startblk);
	}
}

static bool sync(void* instance) {
	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	if (!bc->m_backing) {
		return HAL_FAILED;
	}

	chibios_rt::MutexLocker lock(bc->m_deviceMutex);
	return bc->m_backing->vmt->sync(bc->m_backing);
}

static bool get_info(void* instance, BlockDeviceInfo* bdip) {
	BlockCache* bc = reinterpret_cast<BlockCache*>(instance);

	if (!bc->m_backing) {
		return HAL_FAILED;
	}

	chibios_rt::MutexLocker lock(bc->m_deviceMutex);
	return bc->m_backing->vmt->get_info(bc->m_backing, bdip);
}

static const BaseBlockDeviceVMT blockCacheVmt = {
	(size_t)0, // instanceOffset
	is_inserted,
	is_protected,
	connect,
	disconnect,
	read,
	write,
	sync,
	get_info,
};

BlockCache::BlockCache(uint8_t* cachedBlockData) : m_cachedBlockData(cachedBlockData) {
	vmt = &blockCacheVmt;

	// push all in to the free buffer
	for (int i = 0; i < efi::size(m_handles); i++) {
		m_free.post(&m_handles[i], TIME_INFINITE);
	}
}

void BlockCache::start(BaseBlockDevice* backing) {
	this->m_backing = backing;

	// prefetch block 0
	fetchBlock(0);

	chThdCreateStatic(waRead, sizeof(waRead), MSD_CACHE_PRIO, [](void* instance) { reinterpret_cast<BlockCache*>(instance)->readThread(); }, this);
}

#endif // HAL_USE_USB_MSD
