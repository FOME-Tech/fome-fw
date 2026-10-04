#pragma once

#include "flash_int.h"

enum class ConfigurationWritePhase {
	Complete,
	Layout,
	Erase,
	Program,
	Verify,
};

struct ConfigurationWriteResult {
	ConfigurationWritePhase phase;
	flashaddr_t address;
	int error;
	// Copies whose erase, program, and readback verification completed successfully.
	unsigned completedCopies;

	bool success() const {
		return phase == ConfigurationWritePhase::Complete;
	}
};

// Callers serialize access. A failed attempt remains pending, but only a new
// request permits an automatic retry. Requests arriving during a write survive it.
class ConfigurationWriteState {
public:
	void request() {
		m_pending = true;
		m_requested = true;
	}

	bool pending() const {
		return m_pending;
	}
	bool writing() const {
		return m_writing;
	}
	bool reading() const {
		return m_reading;
	}
	bool shouldWrite() const {
		return m_requested && !m_writing && !m_reading;
	}

	bool begin(bool requestedOnly = false) {
		if (m_writing || m_reading || (requestedOnly && !m_requested)) {
			return false;
		}
		m_pending = true;
		m_requested = false;
		m_writing = true;
		return true;
	}

	void complete(bool success) {
		m_writing = false;
		m_pending = !success || m_requested;
	}

	bool beginRead() {
		if (m_writing || m_reading) {
			return false;
		}
		m_reading = true;
		return true;
	}

	void endRead() {
		m_reading = false;
	}

private:
	bool m_pending = false;
	bool m_requested = false;
	bool m_writing = false;
	bool m_reading = false;
};
