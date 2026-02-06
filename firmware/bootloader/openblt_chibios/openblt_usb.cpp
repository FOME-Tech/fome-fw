#include "pch.h"
#include "usbcfg.h"
#include "usbconsole.h"

extern "C" {
	#include "boot.h"
	#include "rs232.h"
}

void Rs232Init() {
	// Actual USB init is deferred to doDeferredUsbInit
}

void doDeferredUsbInit() {
	static bool didInit = false;

	if (didInit) {
		return;
	}

	didInit = true;

	// Set up USB serial
	usb_serial_start();
}

#define RS232_CTO_RX_PACKET_TIMEOUT_MS (100u)

void Rs232TransmitPacket(blt_int8u *data, blt_int8u len)
{
  doDeferredUsbInit();

  /* first transmit the length of the packet */
  chnWriteTimeout(&SDU1, &len, 1, TIME_INFINITE);

  chnWriteTimeout(&SDU1, data, len, TIME_INFINITE);
}

blt_bool Rs232ReceivePacket(blt_int8u *data, blt_int8u *len)
{
  doDeferredUsbInit();

  if (!is_usb_serial_ready()) {
    return BLT_FALSE;
  }

  static blt_int8u xcpCtoReqPacket[BOOT_COM_RS232_RX_MAX_DATA+1];  /* one extra for length */
  static blt_int8u xcpCtoRxLength;
  static blt_bool  xcpCtoRxInProgress = BLT_FALSE;
  static blt_int32u xcpCtoRxStartTime = 0;

  /* start of cto packet received? */
  if (xcpCtoRxInProgress == BLT_FALSE)
  {
    /* try to read the length prefix byte */
    if (chnReadTimeout(&SDU1, &xcpCtoReqPacket[0], 1, TIME_IMMEDIATE) == 1)
    {
      if ( (xcpCtoReqPacket[0] > 0) &&
           (xcpCtoReqPacket[0] <= BOOT_COM_RS232_RX_MAX_DATA) )
      {
        xcpCtoRxStartTime = TimerGet();
        xcpCtoRxLength = 0;
        xcpCtoRxInProgress = BLT_TRUE;
      }
    }
  }

  if (xcpCtoRxInProgress == BLT_TRUE)
  {
    /* try to read all remaining packet bytes at once */
    blt_int8u remaining = xcpCtoReqPacket[0] - xcpCtoRxLength;
    auto bytesRead = chnReadTimeout(&SDU1, &xcpCtoReqPacket[xcpCtoRxLength + 1], remaining, TIME_IMMEDIATE);

    if (bytesRead > 0)
    {
      xcpCtoRxLength += bytesRead;

      /* check to see if the entire packet was received */
      if (xcpCtoRxLength == xcpCtoReqPacket[0])
      {
        /* copy the packet data */
        CpuMemCopy((blt_int32u)data, (blt_int32u)&xcpCtoReqPacket[1], xcpCtoRxLength);
        xcpCtoRxInProgress = BLT_FALSE;
        *len = xcpCtoRxLength;
        return BLT_TRUE;
      }
    }
    else
    {
      /* check packet reception timeout */
      if (TimerGet() > (xcpCtoRxStartTime + RS232_CTO_RX_PACKET_TIMEOUT_MS))
      {
        /* cancel cto packet reception due to timeout. note that that automatically
         * discards the already received packet bytes, allowing the host to retry.
         */
        xcpCtoRxInProgress = BLT_FALSE;
      }
    }
  }

  return BLT_FALSE;
}
