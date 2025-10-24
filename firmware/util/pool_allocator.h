#pragma once

#include <cstddef>

template <typename T, size_t TSize>
class PoolAllocator {
public:
	PoolAllocator() {
		// return all elements to the pool
		for (size_t i = 0; i < efi::size(m_pool); i++) {
			tryReturn(&m_pool[i]);
		}

		m_used = 0;
	}

	T* get() {
		auto retVal = m_freelist;

		if (retVal) {
			m_freelist = retVal->nextScheduling_s;
			retVal->nextScheduling_s = nullptr;

			m_used++;
		}

		return retVal;
	}

	void tryReturn(T* element) {
		// Only return this scheduling to the free list if it's from the our pool
		if (element >= &m_pool[0] && element <= &m_pool[efi::size(m_pool) - 1]) {
			element->nextScheduling_s = m_freelist;
			m_freelist = element;

			m_used--;
		}
	}

	size_t used() const {
		return m_used;
	}

private:
	T* m_freelist = nullptr;
	T m_pool[TSize];
	size_t m_used = 0;
};
