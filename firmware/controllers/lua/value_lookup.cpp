// The generated getOutputValueByName() looks up output channels by offset into the same
// fragmented output-channel space that the main TunerStudio log uses, so no per-channel
// compile-time address is required. These helpers resolve an offset to a live value at runtime.
//
// This file textually includes the generated switch below, so the helpers are visible to it.

#if !EFI_UNIT_TEST
#include "pch.h"
#include "value_lookup.h"
#include "live_data.h"

static size_t sizeForLogType(LogField::Type type) {
	switch (type) {
		case LogField::Type::U08:
		case LogField::Type::S08:
			return 1;
		case LogField::Type::U16:
		case LogField::Type::S16:
			return 2;
		default:
			// U32, S32, F32
			return 4;
	}
}

// Read a scalar output channel at the given offset and return it as a real (scaled) value.
float getOutputChannelValue(uint16_t offset, LogField::Type type, float multiplier) {
	uint8_t* ptr = nullptr;
	getRangePtr(&ptr, getLiveDataFragments(), offset, sizeForLogType(type));

	if (!ptr) {
		return 0;
	}

	float raw;
	switch (type) {
		case LogField::Type::U08:
			raw = *reinterpret_cast<const uint8_t*>(ptr);
			break;
		case LogField::Type::S08:
			raw = *reinterpret_cast<const int8_t*>(ptr);
			break;
		case LogField::Type::U16:
			raw = *reinterpret_cast<const uint16_t*>(ptr);
			break;
		case LogField::Type::S16:
			raw = *reinterpret_cast<const int16_t*>(ptr);
			break;
		case LogField::Type::U32:
			raw = *reinterpret_cast<const uint32_t*>(ptr);
			break;
		case LogField::Type::S32:
			raw = *reinterpret_cast<const int32_t*>(ptr);
			break;
		case LogField::Type::F32:
			raw = *reinterpret_cast<const float*>(ptr);
			break;
		default:
			raw = 0;
			break;
	}

	return raw * multiplier;
}

// Read a single bit from a U32 bit-group output channel at the given offset.
float getOutputChannelBit(uint16_t offset, uint8_t bitIndex) {
	uint8_t* ptr = nullptr;
	getRangePtr(&ptr, getLiveDataFragments(), offset, sizeof(uint32_t));

	if (!ptr) {
		return 0;
	}

	uint32_t value = *reinterpret_cast<const uint32_t*>(ptr);
	return (value >> bitIndex) & 0x1;
}
#endif // !EFI_UNIT_TEST

#include "value_lookup_generated.cpp"
