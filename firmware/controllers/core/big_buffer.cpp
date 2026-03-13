#include "pch.h"

#include "big_buffer.h"

static BigBufferUser s_currentUser;

#if EFI_PROD_CODE
#include "dma_buffers.h"

static uint8_t* getBigBufferPtr() {
	return dma_buffers::bigBuffer();
}
#else
// this buffer requires 4 byte alignment
// Some users place C++ objects in this memory, and that has certain alignment
// requirements.
static __attribute__((aligned(4))) uint8_t s_bigBuffer[BIG_BUFFER_SIZE];

static uint8_t* getBigBufferPtr() {
	return s_bigBuffer;
}
#endif

#if EFI_UNIT_TEST
BigBufferUser getBigBufferCurrentUser() {
	return s_currentUser;
}
#endif // EFI_UNIT_TEST

static void releaseBuffer(void* bufferPtr, BigBufferUser user) {
	if (bufferPtr != getBigBufferPtr() || user != s_currentUser) {
		// todo: panic!
	}

	s_currentUser = BigBufferUser::None;
}

BigBufferHandle::BigBufferHandle(void* buffer, BigBufferUser user)
	: m_bufferPtr(buffer)
	, m_user(user) {}

BigBufferHandle::BigBufferHandle(BigBufferHandle&& other) {
	// swap contents of the two objects, the destructor will clean up the old object
	std::swap(m_bufferPtr, other.m_bufferPtr);
	std::swap(m_user, other.m_user);
}

BigBufferHandle& BigBufferHandle::operator=(BigBufferHandle&& other) {
	// swap contents of the two objects, the destructor will clean up the old object
	std::swap(m_bufferPtr, other.m_bufferPtr);
	std::swap(m_user, other.m_user);

	return *this;
}

BigBufferHandle::~BigBufferHandle() {
	if (m_bufferPtr) {
		releaseBuffer(m_bufferPtr, m_user);
	}
}

BigBufferHandle getBigBuffer(BigBufferUser user) {
	if (s_currentUser != BigBufferUser::None) {
		// fatal
		return {};
	}

	s_currentUser = user;

	return BigBufferHandle(getBigBufferPtr(), user);
}
