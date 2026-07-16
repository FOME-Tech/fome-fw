#pragma once

#include "efi_scaled_channel.h"
#include <cstdint>
#include <cstddef>

struct Writer;
class LogField {
public:
	enum class Type : uint8_t {
		U08 = 0,
		S08 = 1,
		U16 = 2,
		S16 = 3,
		U32 = 4,
		S32 = 5,
		S64 = 6,
		F32 = 7,
	};

	// Offset-based field: reads from a snapshot of the output channel space (see writeData).
	// This is how generated SD log fields are described - the offset, type and multiplier come
	// from the same output-channel layout the main TunerStudio log uses, so no compile-time
	// address (and thus no per-usage constexpr) is required.
	constexpr LogField(
			uint16_t offset,
			Type type,
			float multiplier,
			const char* name,
			const char* units,
			int8_t digits,
			const char* category = "none")
		: m_multiplier(multiplier)
		, m_addr(nullptr)
		, m_offset(offset)
		, m_type(type)
		, m_digits(digits)
		, m_size(sizeForType(type))
		, m_bitIndex(-1)
		, m_name(name)
		, m_units(units)
		, m_category(category) {}

	// Offset-based single-bit field: extracts one bit (bitIndex) from the output-channel snapshot and
	// emits it as a 0/1 U08. Boolean/flag channels are packed into bit groups; spending a whole byte
	// per bit is wasteful but keeps the writer trivial (no native MLQ bitfield header/names section).
	constexpr LogField(uint16_t offset, uint8_t bitIndex, const char* name, const char* category = "none")
		: m_multiplier(1)
		, m_addr(nullptr)
		, m_offset(offset)
		, m_type(Type::U08)
		, m_digits(0)
		, m_size(1)
		, m_bitIndex(static_cast<int8_t>(bitIndex))
		, m_name(name)
		, m_units("")
		, m_category(category) {}

	// Scaled channels, memcpys data directly and describes format in header.
	// Pointer-based: reads directly from a fixed address, used for fields that live outside the
	// output channel space (e.g. the log timestamp).
	template <typename TValue, int TMult, int TDiv>
	constexpr LogField(
			const scaled_channel<TValue, TMult, TDiv>& toRead,
			const char* name,
			const char* units,
			int8_t digits,
			const char* category = "none")
		: m_multiplier(float(TDiv) / TMult)
		, m_addr(toRead.getFirstByteAddr())
		, m_offset(0)
		, m_type(resolveType<TValue>())
		, m_digits(digits)
		, m_size(sizeForType(resolveType<TValue>()))
		, m_bitIndex(-1)
		, m_name(name)
		, m_units(units)
		, m_category(category) {}

	// Non-scaled channel, works for plain arithmetic types (int, float, uint8_t, etc)
	template <typename TValue, typename = typename std::enable_if<std::is_arithmetic_v<TValue>>::type>
	constexpr LogField(
			TValue& toRead, const char* name, const char* units, int8_t digits, const char* category = "none")
		: m_multiplier(1)
		, m_addr(&toRead)
		, m_offset(0)
		, m_type(resolveType<TValue>())
		, m_digits(digits)
		, m_size(sizeForType(resolveType<TValue>()))
		, m_bitIndex(-1)
		, m_name(name)
		, m_units(units)
		, m_category(category) {}

	constexpr size_t getSize() const {
		return m_size;
	}

	// Write the header data describing this field.
	void writeHeader(Writer& outBuffer) const;

	// Write the field's data to the buffer, reading either from a fixed address (pointer-based
	// fields) or from `channels` + offset (offset-based fields, snapshot of the output space).
	// Returns the number of bytes written.
	size_t writeData(char* buffer, const uint8_t* channels = nullptr) const;

private:
	template <typename T>
	static constexpr Type resolveType();

	static constexpr size_t sizeForType(Type t) {
		switch (t) {
			case Type::U08:
			case Type::S08:
				return 1;
			case Type::U16:
			case Type::S16:
				return 2;
			default:
				// float, uint32, int32
				return 4;
		}
	}

	const float m_multiplier;
	// Non-null for pointer-based fields; null for offset-based fields (which use m_offset).
	const void* const m_addr;
	// Offset into the output channel snapshot, used when m_addr is null.
	const uint16_t m_offset;
	const Type m_type;
	const int8_t m_digits;
	const uint8_t m_size;
	// Bit index (0..31) within the byte(s) at m_offset for single-bit fields; -1 for all other fields.
	const int8_t m_bitIndex;

	const char* const m_name;
	const char* const m_units;
	const char* const m_category;
};

template <>
constexpr LogField::Type LogField::resolveType<uint8_t>() {
	return Type::U08;
}

template <>
constexpr LogField::Type LogField::resolveType<int8_t>() {
	return Type::S08;
}

template <>
constexpr LogField::Type LogField::resolveType<uint16_t>() {
	return Type::U16;
}

template <>
constexpr LogField::Type LogField::resolveType<int16_t>() {
	return Type::S16;
}

template <>
constexpr LogField::Type LogField::resolveType<uint32_t>() {
	return Type::U32;
}

template <>
constexpr LogField::Type LogField::resolveType<int32_t>() {
	return Type::S32;
}

template <>
constexpr LogField::Type LogField::resolveType<float>() {
	return Type::F32;
}
