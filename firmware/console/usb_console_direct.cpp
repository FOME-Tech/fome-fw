#include "pch.h"

#if EFI_USB_SERIAL

#include "usbconsole.h"
#include "usbcfg.h"
#include "thread_controller.h"
#include "tunerstudio.h"

static_assert(SERIAL_USB_BUFFERS_SIZE >= BLOCKING_FACTOR + 10);

#define CDC_PACKET_SIZE 64

// RX staging + queue. The pump thread receives into s_rxStaging, then copies
// into s_rxQueue which is drained by UsbDirectChannel::readTimeout.
static input_queue_t s_rxQueue;
static uint8_t s_rxQueueBuf[SERIAL_USB_BUFFERS_SIZE];
static uint8_t s_rxStaging[CDC_PACKET_SIZE];

static volatile bool s_configured = false;

class UsbDirectChannel final : public TsChannelBase {
public:
	UsbDirectChannel()
		: TsChannelBase("USB") {}

	bool isReady() const override {
		return s_configured && EFI_USB_DRIVER->state == USB_ACTIVE;
	}

	void write(const uint8_t* buffer, size_t size, bool isEndOfPacket) override {
		// Coalesce small writes. writeCrcPacketLocked calls write() three times
		// per response (header, data, CRC); for typical short replies all three
		// fit in m_txBuffer and become a single usbTransmit.
		if (size > 0) {
			size_t space = sizeof(m_txBuffer) - m_txFill;
			if (size <= space) {
				memcpy(m_txBuffer + m_txFill, buffer, size);
				m_txFill += size;
			} else {
				flushBuffer();
				if (size >= sizeof(m_txBuffer)) {
					// Bulk payload — pass through directly to avoid an extra copy.
					transmit(buffer, size);
				} else {
					memcpy(m_txBuffer, buffer, size);
					m_txFill = size;
				}
			}
		}

		if (isEndOfPacket) {
			flushBuffer();
			// USB bulk-IN requires a short (or zero-length) packet to signal end-of-transfer.
			// If the cumulative bytes landed exactly on a 64-byte boundary, emit a ZLP so the
			// host sees the packet boundary immediately instead of waiting for a short packet.
			if (m_bytesSinceEop > 0 && (m_bytesSinceEop % CDC_PACKET_SIZE) == 0) {
				usbTransmit(EFI_USB_DRIVER, EFI_USB_CDC_DATA_REQUEST_EP, nullptr, 0);
			}
			m_bytesSinceEop = 0;
		}
	}

	size_t readTimeout(uint8_t* buffer, size_t size, int timeout) override {
		return iqReadTimeout(&s_rxQueue, buffer, size, timeout);
	}

private:
	void transmit(const uint8_t* buffer, size_t size) {
		msg_t r = usbTransmit(EFI_USB_DRIVER, EFI_USB_CDC_DATA_REQUEST_EP, buffer, size);
		if (r == MSG_RESET) {
			m_bytesSinceEop = 0;
		} else {
			m_bytesSinceEop += size;
		}
	}

	void flushBuffer() {
		if (m_txFill == 0) {
			return;
		}
		size_t n = m_txFill;
		m_txFill = 0;
		transmit(m_txBuffer, n);
	}

	static constexpr size_t TX_BUFFER_SIZE = 128;
	uint8_t m_txBuffer[TX_BUFFER_SIZE];
	size_t m_txFill = 0;
	size_t m_bytesSinceEop = 0;
};

static CCM_OPTIONAL UsbDirectChannel usbChannel;

class UsbRxPumpThread : public ThreadController<512> {
public:
	UsbRxPumpThread()
		: ThreadController("USB RX", PRIO_CONSOLE) {}

protected:
	void ThreadTask() override {
		while (!chThdShouldTerminateX()) {
			msg_t r = usbReceive(EFI_USB_DRIVER, EFI_USB_CDC_DATA_AVAILABLE_EP, s_rxStaging, sizeof(s_rxStaging));
			if (r < 0) {
				// MSG_RESET: endpoint aborted / USB not active. Drop any pending
				// bytes and wait for reconfigure.
				chSysLock();
				iqResetI(&s_rxQueue);
				chSysUnlock();
				chThdSleepMilliseconds(50);
				continue;
			}

			// r is the received byte count
			chSysLock();
			for (msg_t i = 0; i < r; i++) {
				if (iqPutI(&s_rxQueue, s_rxStaging[i]) != Q_OK) {
					// Queue full: TS reader is too slow. Drop the rest; the
					// protocol will time out and re-sync via in_sync = false.
					break;
				}
			}
			chSysUnlock();
		}
	}
};

static CCM_OPTIONAL UsbRxPumpThread rxPump;

struct UsbThread : public TunerstudioThread {
	UsbThread()
		: TunerstudioThread("USB Console") {}

	TsChannelBase* setupChannel() override {
		usb_serial_start();
		return &usbChannel;
	}
};

static CCM_OPTIONAL UsbThread usbConsole;

// Called from usbcfg.cpp on USB_EVENT_CONFIGURED (replaces sduConfigureHookI).
void usbDirectConfiguredHookI(USBDriver*) {
	s_configured = true;
}

// Called from usbcfg.cpp on USB_EVENT_RESET / USB_EVENT_UNCONFIGURED / USB_EVENT_SUSPEND
// (replaces sduSuspendHookI).
void usbDirectSuspendHookI(USBDriver*) {
	s_configured = false;
	iqResetI(&s_rxQueue);
}

// Called from usbcfg.cpp on USB_EVENT_WAKEUP (replaces sduWakeupHookI).
void usbDirectWakeupHookI(USBDriver*) {
	// Nothing to do — s_configured stays until USB_EVENT_CONFIGURED fires.
}

void startUsbConsole() {
	// Initialize the RX queue before any thread can touch it. The pump thread
	// may hit iqResetI on its first usbReceive (driver isn't started yet), so
	// the queue must be valid before rxPump runs.
	iqObjectInit(&s_rxQueue, s_rxQueueBuf, sizeof(s_rxQueueBuf), nullptr, nullptr);

	rxPump.startThread();
	usbConsole.startThread();
}

#endif // EFI_USB_SERIAL
