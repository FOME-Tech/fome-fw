#pragma once
#include <stddef.h>
#include <stdint.h>

namespace g0 {
struct BootTransport {
	// The hardware adapter spaces bytes by at least 15 us (ST AN4286).
	virtual uint8_t byte(uint8_t value) = 0;
	virtual void sleepMs(uint32_t delay) = 0;
};

class Bootloader {
public:
	explicit Bootloader(BootTransport& transport)
		: m_io(transport) {}
	static constexpr uint32_t FlashBase = 0x08000000;
	static constexpr size_t FlashSize = 128 * 1024;
	static bool validImage(const uint8_t* image, size_t size);
	bool flash(const uint8_t* image, size_t size);

private:
	bool ack(uint32_t timeoutMs = 1000);
	void send(const uint8_t* data, size_t size);
	bool command(uint8_t command);
	bool address(uint32_t address);
	bool write(uint32_t address, const uint8_t* data);
	bool verify(uint32_t address, const uint8_t* data);
	BootTransport& m_io;
};
} // namespace g0
