#include "pch.h"
#include "usbcfg.h"
#include "usbconsole.h"

extern "C" {
#include "boot.h"
#include "rs232.h"
}

// CDC bulk packet size — also our single-packet RX staging buffer.
#define CDC_PACKET_SIZE 64

static volatile bool s_configured = false;

// Set to true while a usbStartReceiveI is in flight; transitions to false once
// the HAL clears the receiving bit. Latched bytes live in s_rxBuffer between
// completion and the next startReceiveI.
static volatile bool s_rxBusy = false;
static uint8_t s_rxBuffer[CDC_PACKET_SIZE];
static size_t s_rxAvailable = 0;
static size_t s_rxConsumed = 0;

static void startReceiveI() {
	s_rxAvailable = 0;
	s_rxConsumed = 0;
	s_rxBusy = true;
	usbStartReceiveI(EFI_USB_DRIVER, EFI_USB_CDC_DATA_AVAILABLE_EP, s_rxBuffer, sizeof(s_rxBuffer));
}

// Called from usbcfg.cpp on USB_EVENT_CONFIGURED.
void usbDirectConfiguredHookI(USBDriver*) {
	s_configured = true;
	s_rxBusy = false;
	startReceiveI();
}

// Called from usbcfg.cpp on USB_EVENT_RESET / USB_EVENT_UNCONFIGURED / USB_EVENT_SUSPEND.
void usbDirectSuspendHookI(USBDriver*) {
	s_configured = false;
	s_rxBusy = false;
	s_rxAvailable = 0;
	s_rxConsumed = 0;
}

// Called from usbcfg.cpp on USB_EVENT_WAKEUP.
void usbDirectWakeupHookI(USBDriver*) {
}

void Rs232Init() {
	// Actual USB init is deferred to doDeferredUsbInit
}

static void doDeferredUsbInit() {
	static bool didInit = false;

	if (didInit) {
		return;
	}

	didInit = true;

	// Set up USB serial
	usb_serial_start();
}

void Rs232TransmitPacket(blt_int8u* data, blt_int8u len) {
	doDeferredUsbInit();

	if (!is_usb_serial_ready()) {
		return;
	}

	/* first transmit the length of the packet */
	usbTransmit(EFI_USB_DRIVER, EFI_USB_CDC_DATA_REQUEST_EP, &len, 1);
	usbTransmit(EFI_USB_DRIVER, EFI_USB_CDC_DATA_REQUEST_EP, data, len);

	// ChibiOS doesn't auto-terminate a transfer that lands on a packet boundary.
	// Emit a ZLP so a host doing a single buffered read sees end-of-transfer
	// immediately instead of stalling until the next response.
	if (len > 0 && (len % CDC_PACKET_SIZE) == 0) {
		usbTransmit(EFI_USB_DRIVER, EFI_USB_CDC_DATA_REQUEST_EP, nullptr, 0);
	}
}

// Non-blocking read. If the previous receive has completed, latches its size
// and copies up to maxLen bytes out. When the staging buffer is fully drained,
// kicks off the next receive. Returns the byte count actually copied (0 if
// nothing is available yet).
static size_t readNonBlocking(uint8_t* dest, size_t maxLen) {
	if (!s_configured) {
		return 0;
	}

	chSysLock();
	if (s_rxBusy && !usbGetReceiveStatusI(EFI_USB_DRIVER, EFI_USB_CDC_DATA_AVAILABLE_EP)) {
		s_rxAvailable = usbGetReceiveTransactionSizeX(EFI_USB_DRIVER, EFI_USB_CDC_DATA_AVAILABLE_EP);
		s_rxConsumed = 0;
		s_rxBusy = false;
	}
	chSysUnlock();

	size_t avail = s_rxAvailable - s_rxConsumed;
	size_t toCopy = avail < maxLen ? avail : maxLen;
	if (toCopy > 0) {
		memcpy(dest, &s_rxBuffer[s_rxConsumed], toCopy);
		s_rxConsumed += toCopy;
	}

	if (!s_rxBusy && s_rxConsumed >= s_rxAvailable) {
		chSysLock();
		startReceiveI();
		chSysUnlock();
	}

	return toCopy;
}

#define RS232_CTO_RX_PACKET_TIMEOUT_MS (100u)

blt_bool Rs232ReceivePacket(blt_int8u* data, blt_int8u* len) {
	doDeferredUsbInit();

	if (!is_usb_serial_ready()) {
		return BLT_FALSE;
	}

	static blt_int8u xcpCtoReqPacket[BOOT_COM_RS232_RX_MAX_DATA + 1]; /* one extra for length */
	static blt_int8u xcpCtoRxLength;
	static blt_bool xcpCtoRxInProgress = BLT_FALSE;
	static blt_int32u xcpCtoRxStartTime = 0;

	/* start of cto packet received? */
	if (xcpCtoRxInProgress == BLT_FALSE) {
		/* try to read the length prefix byte */
		if (readNonBlocking(&xcpCtoReqPacket[0], 1) == 1) {
			if ((xcpCtoReqPacket[0] > 0) && (xcpCtoReqPacket[0] <= BOOT_COM_RS232_RX_MAX_DATA)) {
				xcpCtoRxStartTime = TimerGet();
				xcpCtoRxLength = 0;
				xcpCtoRxInProgress = BLT_TRUE;
			}
		}
	}

	if (xcpCtoRxInProgress == BLT_TRUE) {
		/* try to read all remaining packet bytes at once */
		blt_int8u remaining = xcpCtoReqPacket[0] - xcpCtoRxLength;
		size_t bytesRead = readNonBlocking(&xcpCtoReqPacket[xcpCtoRxLength + 1], remaining);

		if (bytesRead > 0) {
			xcpCtoRxLength += bytesRead;

			/* check to see if the entire packet was received */
			if (xcpCtoRxLength == xcpCtoReqPacket[0]) {
				/* copy the packet data */
				CpuMemCopy((blt_int32u)data, (blt_int32u)&xcpCtoReqPacket[1], xcpCtoRxLength);
				xcpCtoRxInProgress = BLT_FALSE;
				*len = xcpCtoRxLength;
				return BLT_TRUE;
			}
		} else {
			/* check packet reception timeout */
			if (TimerGet() > (xcpCtoRxStartTime + RS232_CTO_RX_PACKET_TIMEOUT_MS)) {
				/* cancel cto packet reception due to timeout. note that that automatically
				 * discards the already received packet bytes, allowing the host to retry.
				 */
				xcpCtoRxInProgress = BLT_FALSE;
			}
		}
	}

	return BLT_FALSE;
}
