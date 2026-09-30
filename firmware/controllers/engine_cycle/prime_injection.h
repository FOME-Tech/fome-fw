/*
 * @file prime_injection.h

 */

#pragma once

#include "engine_module.h"
#include "rusefi_types.h"
#include "scheduler.h"

class PrimeController : public EngineModule {
public:
	void onIgnitionStateChanged(bool ignitionOn) override;
	void onSlowCallback() override;

	floatms_t getPrimeDuration() const;

	void onPrimeStart();
	void onPrimeEnd();

	bool isPriming() const {
		return m_isPriming;
	}

private:
	bool m_isPriming = false;
	uint16_t m_primeOutputsMask = 0;
	void onPrimeOpen();

	static void onPrimeStartAdapter(PrimeController* instance);
	static void onPrimeOpenAdapter(PrimeController* instance);
	static void onPrimeEndAdapter(PrimeController* instance);

	uint32_t getKeyCycleCounter() const;
	void setKeyCycleCounter(uint32_t count);
};
