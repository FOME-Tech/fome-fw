#include "pch.h"
#include "usbcfg.h"
#include "usbconsole.h"

extern "C" {
	#include "boot.h"
	#include "rs232.h"
}

void Rs232Init() {
	// Set up USB serial
	usb_serial_start();
}

#define RS232_CTO_RX_PACKET_TIMEOUT_MS (100u)

static blt_bool Rs232ReceiveByte(blt_int8u *data);
static void     Rs232TransmitByte(blt_int8u data);

/************************************************************************************//**
** \brief     Transmits a packet formatted for the communication interface.
** \param     data Pointer to byte array with data that it to be transmitted.
** \param     len  Number of bytes that are to be transmitted.
** \return    none.
**
****************************************************************************************/
void Rs232TransmitPacket(blt_int8u *data, blt_int8u len)
{
  blt_int16u data_index;

  /* verify validity of the len-paramenter */
  // ASSERT_RT(len <= BOOT_COM_RS232_TX_MAX_DATA);

  /* first transmit the length of the packet */
  Rs232TransmitByte(len);

  /* transmit all the packet bytes one-by-one */
  for (data_index = 0; data_index < len; data_index++)
  {
    /* keep the watchdog happy */
    CopService();
    /* write byte */
    Rs232TransmitByte(data[data_index]);
  }
} /*** end of Rs232TransmitPacket ***/

/************************************************************************************//**
** \brief     Receives a communication interface packet if one is present.
** \param     data Pointer to byte array where the data is to be stored.
** \param     len Pointer where the length of the packet is to be stored.
** \return    BLT_TRUE if a packet was received, BLT_FALSE otherwise.
**
****************************************************************************************/
blt_bool Rs232ReceivePacket(blt_int8u *data, blt_int8u *len)
{
  static blt_int8u xcpCtoReqPacket[BOOT_COM_RS232_RX_MAX_DATA+1];  /* one extra for length */
  static blt_int8u xcpCtoRxLength;
  static blt_bool  xcpCtoRxInProgress = BLT_FALSE;
  static blt_int32u xcpCtoRxStartTime = 0;

  /* start of cto packet received? */
  if (xcpCtoRxInProgress == BLT_FALSE)
  {
    /* store the message length when received */
    if (Rs232ReceiveByte(&xcpCtoReqPacket[0]) == BLT_TRUE)
    {
      if ( (xcpCtoReqPacket[0] > 0) &&
           (xcpCtoReqPacket[0] <= BOOT_COM_RS232_RX_MAX_DATA) )
      {
        /* store the start time */
        xcpCtoRxStartTime = TimerGet();
        /* reset packet data count */
        xcpCtoRxLength = 0;
        /* indicate that a cto packet is being received */
        xcpCtoRxInProgress = BLT_TRUE;
      }
    }
  }
  else
  {
    /* store the next packet byte */
    if (Rs232ReceiveByte(&xcpCtoReqPacket[xcpCtoRxLength+1]) == BLT_TRUE)
    {
      /* increment the packet data count */
      xcpCtoRxLength++;

      /* check to see if the entire packet was received */
      if (xcpCtoRxLength == xcpCtoReqPacket[0])
      {
        /* copy the packet data */
        CpuMemCopy((blt_int32u)data, (blt_int32u)&xcpCtoReqPacket[1], xcpCtoRxLength);
        /* done with cto packet reception */
        xcpCtoRxInProgress = BLT_FALSE;
        /* set the packet length */
        *len = xcpCtoRxLength;
        /* packet reception complete */
        return BLT_TRUE;
      }
    }
    else
    {
      /* check packet reception timeout */
      if (TimerGet() > (xcpCtoRxStartTime + RS232_CTO_RX_PACKET_TIMEOUT_MS))
      {
        /* cancel cto packet reception due to timeout. note that that automaticaly
         * discards the already received packet bytes, allowing the host to retry.
         */
        xcpCtoRxInProgress = BLT_FALSE;
      }
    }
  }
  /* packet reception not yet complete */
  return BLT_FALSE;
} /*** end of Rs232ReceivePacket ***/

static blt_bool Rs232ReceiveByte(blt_int8u *data)
{
	if (!is_usb_serial_ready()) {
		return BLT_FALSE;
	}

	auto bytesRead = chnReadTimeout(&SDU1, data, 1, TIME_IMMEDIATE);

	return bytesRead == 0 ? BLT_FALSE : BLT_TRUE;
}

static void Rs232TransmitByte(blt_int8u data)
{
	chnWriteTimeout(&SDU1, &data, 1, TIME_INFINITE);
}
