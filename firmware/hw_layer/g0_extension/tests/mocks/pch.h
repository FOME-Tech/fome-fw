#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
enum Gpio {
	G0_OUT_1 = 219,
	G0_OUT_2,
	G0_OUT_3,
	G0_OUT_4
};
using brain_pin_e = Gpio;
using iomode_t = unsigned;
struct SPIDriver {};
struct SPIConfig {};
namespace ObdCode {
enum Code {
	CUSTOM_ERR_ASSERT,
	CUSTOM_ERR_PWM_STATE_ASSERT
};
} // namespace ObdCode
inline unsigned testWarnings = 0;
inline void warning(ObdCode::Code, const char*) {
	++testWarnings;
}
#define efiAssertVoid(code, condition, text) assert(condition)
constexpr unsigned PAL_STM32_MODE_MASK = 3, PAL_MODE_OUTPUT_PUSHPULL = 1;
namespace chibios_rt {
struct CriticalSectionLocker {
	inline static unsigned depth = 0;
	CriticalSectionLocker() {
		++depth;
	}
	~CriticalSectionLocker() {
		assert(depth);
		--depth;
	}
};
} // namespace chibios_rt
