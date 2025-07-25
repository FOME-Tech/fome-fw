#pragma once

#include <rusefi/expected.h>
#include "injector_model_generated.h"
#include "engine_module.h"

struct IInjectorModel : public EngineModule, public injector_model_s {
	virtual void prepare() = 0;
	virtual floatms_t getInjectionDuration(float fuelMassGram) const = 0;
	virtual float getFuelMassForDuration(floatms_t duration) const = 0;
	virtual floatms_t getDeadtime() const = 0;
};

class InjectorModelBase : public IInjectorModel {
public:
	void prepare() override;
	floatms_t getInjectionDuration(float fuelMassGram) const override;
	float getFuelMassForDuration(floatms_t duration) const override;

	virtual float getInjectorFlowRatio() = 0;
	virtual expected<float> getFuelDifferentialPressure() const = 0;

	virtual float getBaseFlowRate() const = 0;
	virtual float getMinimumPulse() const = 0;

	virtual InjectorNonlinearMode getNonlinearMode() const = 0;
	float getBaseDurationImpl(float baseDuration) const;
	virtual float correctInjectionPolynomial(float baseDuration) const;

	// Ford small pulse model
	virtual float getSmallPulseFlowRate() const = 0;
	virtual float getSmallPulseBreakPoint() const = 0;

private:
	// Mass flow rate for large-pulse flow, g/s
	float m_massFlowRate = 0;

	// Break point below which the "small pulse" slope is used, grams
	float m_smallPulseBreakPoint = 0;

	// Flow rate for small pulses, g/s
	float m_smallPulseFlowRate = 0;

	// Correction adder for small pulses to correct for small/large pulse kink, ms
	float m_smallPulseOffset = 0;
};

class InjectorModelWithConfig : public InjectorModelBase {
public:
	InjectorModelWithConfig(const injector_s* const cfg);

	floatms_t getDeadtime() const override;
	float getBaseFlowRate() const override;
	float getInjectorFlowRatio() override;
	expected<float> getFuelDifferentialPressure() const override;
	float getMinimumPulse() const override;

	using interface_t = IInjectorModel; // Mock interface

private:
	const injector_s* const m_cfg;
};

struct InjectorModelPrimary : public InjectorModelWithConfig {
	InjectorModelPrimary();

	InjectorNonlinearMode getNonlinearMode() const override;

	// Ford small pulse model
	float getSmallPulseFlowRate() const override;
	float getSmallPulseBreakPoint() const override;
};

struct InjectorModelSecondary : public InjectorModelWithConfig {
	InjectorModelSecondary();

	InjectorNonlinearMode getNonlinearMode() const override;

	// Ford small pulse model
	float getSmallPulseFlowRate() const override;
	float getSmallPulseBreakPoint() const override;
};
