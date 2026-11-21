#pragma once
#include <stddef.h>
#include <stdint.h>

namespace g0 {
struct BootTransport {
	// The hardware adapter spaces bytes by at least 15 us (ST AN4286).
	virtual uint8_t byte(uint8_t value) = 0;
	virtual void sleepMs(uint32_t delay) = 0;
	// Deselect, wait for option-byte reload, then reselect without pulsing NRST.
	virtual void reconnect() = 0;
};

class Bootloader {
public:
	explicit Bootloader(BootTransport& transport)
		: m_io(transport) {}
	static constexpr uint32_t FlashBase = 0x08000000;
	static constexpr size_t FlashSize = 128 * 1024;
	static bool validImage(const uint8_t* image, size_t size);
	bool connect();
	bool flash(const uint8_t* image, size_t size);
	bool go();

private:
	static constexpr uint32_t OptionsBase = 0x1FFF7800;
	static constexpr size_t OptionsSize = 128;
	bool ack(uint32_t timeoutMs = 1000);
	void send(const uint8_t* data, size_t size);
	bool command(uint8_t command, uint32_t timeoutMs = 1000);
	bool address(uint32_t address);
	bool startRead(uint32_t address, size_t size);
	bool read(uint32_t address, uint8_t* data, size_t size);
	bool write(uint32_t address, const uint8_t* data, size_t size);
	bool verify(uint32_t address, const uint8_t* data, size_t size);
	bool enableBootPin();
	BootTransport& m_io;
	bool m_connected = false;
};
} // namespace g0
