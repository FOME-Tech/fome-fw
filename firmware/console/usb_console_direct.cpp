#include "pch.h"

#if EFI_USB_SERIAL && EFI_USB_SERIAL_DIRECT

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
		if (size > 0) {
			msg_t r = usbTransmit(EFI_USB_DRIVER, EFI_USB_CDC_DATA_REQUEST_EP, buffer, size);
			if (r == MSG_RESET) {
				m_bytesSinceEop = 0;
				return;
			}
			m_bytesSinceEop += size;
		}
		if (isEndOfPacket) {
			// USB bulk-IN requires a short (or zero-length) packet to signal end-of-transfer.
			// If the last transfer landed exactly on a 64-byte boundary, emit a ZLP so the
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
			msg_t r = usbReceive(EFI_USB_DRIVER, EFI_USB_CDC_DATA_AVAILABLE_EP,
			                     s_rxStaging, sizeof(s_rxStaging));
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

// Called from usbconsole.cpp (port layer) at startup, replacing sduObjectInit/sduStart.
extern "C" void usbDirectObjectInit() {
	iqObjectInit(&s_rxQueue, s_rxQueueBuf, sizeof(s_rxQueueBuf), nullptr, nullptr);
}

// Called from usbcfg.cpp on USB_EVENT_CONFIGURED (replaces sduConfigureHookI).
extern "C" void usbDirectConfiguredHookI(USBDriver*) {
	s_configured = true;
}

// Called from usbcfg.cpp on USB_EVENT_RESET / USB_EVENT_UNCONFIGURED / USB_EVENT_SUSPEND
// (replaces sduSuspendHookI).
extern "C" void usbDirectSuspendHookI(USBDriver*) {
	s_configured = false;
	iqResetI(&s_rxQueue);
}

// Called from usbcfg.cpp on USB_EVENT_WAKEUP (replaces sduWakeupHookI).
extern "C" void usbDirectWakeupHookI(USBDriver*) {
	// Nothing to do — s_configured stays until USB_EVENT_CONFIGURED fires.
}

void startUsbConsole() {
	rxPump.startThread();
	usbConsole.startThread();
}

#endif // EFI_USB_SERIAL && EFI_USB_SERIAL_DIRECT
