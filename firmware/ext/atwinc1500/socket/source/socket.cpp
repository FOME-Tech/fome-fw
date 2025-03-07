/**
 *
 * \file
 *
 * \brief BSD compatible socket interface.
 *
 * Copyright (c) 2016-2021 Microchip Technology Inc. and its subsidiaries.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Subject to your compliance with these terms, you may use Microchip
 * software and any derivatives exclusively with Microchip products.
 * It is your responsibility to comply with third party license terms applicable
 * to your use of third party software (including open source software) that
 * may accompany Microchip software.
 *
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
 * INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
 * AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
 * LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
 * LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
 * SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
 * POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
 * ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
 * RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 * THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * \asf_license_stop
 *
 */

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
INCLUDES
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

#include "bsp/include/nm_bsp.h"
#include "socket/include/socket.h"
#include "driver/source/m2m_hif.h"
#include "socket/source/socket_internal.h"
#include "driver/include/m2m_types.h"

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
MACROS
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/


#define TLS_RECORD_HEADER_LENGTH            (5)
#define ETHERNET_HEADER_OFFSET              (34)
#define ETHERNET_HEADER_LENGTH              (14)
#define TCP_IP_HEADER_LENGTH                (40)
#define UDP_IP_HEADER_LENGTH                (28)

#define IP_PACKET_OFFSET                    (ETHERNET_HEADER_LENGTH + ETHERNET_HEADER_OFFSET - M2M_HIF_HDR_OFFSET)

#define TCP_TX_PACKET_OFFSET                (IP_PACKET_OFFSET + TCP_IP_HEADER_LENGTH)
#define UDP_TX_PACKET_OFFSET                (IP_PACKET_OFFSET + UDP_IP_HEADER_LENGTH)
#define SSL_TX_PACKET_OFFSET                (TCP_TX_PACKET_OFFSET + TLS_RECORD_HEADER_LENGTH)

#define SOCKET_REQUEST(reqID, reqArgs, reqSize, reqPayload, reqPayloadSize, reqPayloadOffset)       \
    hif_send(M2M_REQ_GROUP_IP, reqID, reqArgs, reqSize, reqPayload, reqPayloadSize, reqPayloadOffset)


#define SSL_FLAGS_ACTIVE                    NBIT0
#define SSL_FLAGS_BYPASS_X509               NBIT1
#define SSL_FLAGS_2_RESERVD                 NBIT2
#define SSL_FLAGS_3_RESERVD                 NBIT3
#define SSL_FLAGS_CACHE_SESSION             NBIT4
#define SSL_FLAGS_NO_TX_COPY                NBIT5
#define SSL_FLAGS_CHECK_SNI                 NBIT6
#define SSL_FLAGS_DELAY                     NBIT7

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
PRIVATE DATA TYPES
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/


/*!
*  @brief
*/
typedef struct {
    SOCKET      sock;
    uint8       u8Dummy;
    uint16      u16SessionID;
} tstrCloseCmd;


/*!
*  @brief
*/
typedef struct {
    uint8               *pu8UserBuffer;
    uint16              u16UserBufferSize;
    uint16              u16SessionID;
    uint16              u16DataOffset;
    uint8               bIsUsed;
    uint8               u8SSLFlags;
    uint8               bIsRecvPending;
    uint8               u8AlpnStatus;
    uint8               u8ErrSource;
    uint8               u8ErrCode;
} tstrSocket;

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
GLOBALS
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

volatile sint8                  gsockerrno;
volatile tstrSocket             gastrSockets[MAX_SOCKET];
volatile uint8                  gu8OpCode;
volatile uint16                 gu16BufferSize;
volatile uint16                 gu16SessionID = 0;

volatile tpfAppSocketCb         gpfAppSocketCb;
volatile tpfAppResolveCb        gpfAppResolveCb;
volatile uint8                  gbSocketInit = 0;

static tpfPingCb                gfpPingCb = NULL;
static uint32                   gu32PingId = 0;

/*********************************************************************
Function
        Socket_ReadSocketData

Description
        Callback function used by the NMC1500 driver to deliver messages
        for socket layer.

Return
        None.

Author
        Ahmed Ezzat

Version
        1.0

Date
        17 July 2012
*********************************************************************/
NMI_API void Socket_ReadSocketData(SOCKET sock, tstrSocketRecvMsg *pstrRecv, uint8 u8SocketMsg,
                                   uint32 u32StartAddress, uint16 u16ReadCount)
{
    uint32  u32Address = u32StartAddress;
    uint16  u16Read;
    sint16  s16Diff;

    pstrRecv->u16RemainingSize = u16ReadCount;
    if((u16ReadCount > 0) && (gastrSockets[sock].pu8UserBuffer != NULL) && (gastrSockets[sock].u16UserBufferSize > 0) && (gastrSockets[sock].bIsUsed == 1))
    {
        u16Read = u16ReadCount;
        s16Diff = u16Read - gastrSockets[sock].u16UserBufferSize;
        if(s16Diff > 0)
        {
            /* We don't expect to be here. Firmware 19.6.4 and later only sends data to the driver according to the application's buffer size.
             * But it is worth keeping this check, eg in case the application calls recv again with a smaller buffer size, or in case of HIF hacking. */
            u16Read = gastrSockets[sock].u16UserBufferSize;
        }

        if(hif_receive(u32Address, gastrSockets[sock].pu8UserBuffer, u16Read, 1) == M2M_SUCCESS)
        {
            pstrRecv->pu8Buffer         = gastrSockets[sock].pu8UserBuffer;
            pstrRecv->s16BufferSize     = u16Read;
            pstrRecv->u16RemainingSize  -= u16Read;

            gastrSockets[sock].u16UserBufferSize = 0;
            gastrSockets[sock].pu8UserBuffer = NULL;

            if(gpfAppSocketCb)
                gpfAppSocketCb(sock, u8SocketMsg, pstrRecv);
        }
        else
        {
            M2M_ERR("Current <%d>\n", u16ReadCount);
        }
    }
}

/*********************************************************************
Function
        m2m_ip_cb

Description
        Callback function used by the NMC1000 driver to deliver messages
        for socket layer.

Return
        None.

Author
        Ahmed Ezzat

Version
        1.0

Date
        17 July 2012
*********************************************************************/
static void m2m_ip_cb(uint8 u8OpCode, uint16 u16BufferSize, uint32 u32Address)
{
    if((u8OpCode == SOCKET_CMD_BIND) || (u8OpCode == SOCKET_CMD_SSL_BIND))
    {
        tstrBindReply       strBindReply;
        tstrSocketBindMsg   strBind;

        if(hif_receive(u32Address, (uint8 *)&strBindReply, sizeof(tstrBindReply), 0) == M2M_SUCCESS)
        {
            strBind.status = strBindReply.s8Status;
            if(gpfAppSocketCb)
                gpfAppSocketCb(strBindReply.sock, SOCKET_MSG_BIND, &strBind);
        }
    }
    else if(u8OpCode == SOCKET_CMD_LISTEN)
    {
        tstrListenReply         strListenReply;
        tstrSocketListenMsg     strListen;
        if(hif_receive(u32Address, (uint8 *)&strListenReply, sizeof(tstrListenReply), 0) == M2M_SUCCESS)
        {
            strListen.status = strListenReply.s8Status;
            if(gpfAppSocketCb)
                gpfAppSocketCb(strListenReply.sock, SOCKET_MSG_LISTEN, &strListen);
        }
    }
    else if(u8OpCode == SOCKET_CMD_ACCEPT)
    {
        tstrAcceptReply         strAcceptReply;
        tstrSocketAcceptMsg     strAccept;
        if(hif_receive(u32Address, (uint8 *)&strAcceptReply, sizeof(tstrAcceptReply), 0) == M2M_SUCCESS)
        {
            if((strAcceptReply.sConnectedSock >= 0) && (strAcceptReply.sConnectedSock < MAX_SOCKET))
            {
                gastrSockets[strAcceptReply.sConnectedSock].u8SSLFlags      = gastrSockets[strAcceptReply.sListenSock].u8SSLFlags;
                gastrSockets[strAcceptReply.sConnectedSock].bIsUsed         = 1;
                gastrSockets[strAcceptReply.sConnectedSock].u16DataOffset   = strAcceptReply.u16AppDataOffset - M2M_HIF_HDR_OFFSET;

                /* The session ID is used to distinguish different socket connections
                    by comparing the assigned session ID to the one reported by the firmware*/
                ++gu16SessionID;
                if(gu16SessionID == 0)
                    ++gu16SessionID;

                gastrSockets[strAcceptReply.sConnectedSock].u16SessionID = gu16SessionID;
                M2M_DBG("Socket %d session ID = %d\r\n", strAcceptReply.sConnectedSock, gu16SessionID);
            }
            strAccept.sock = strAcceptReply.sConnectedSock;
            strAccept.strAddr.sin_family        = AF_INET;
            strAccept.strAddr.sin_port = strAcceptReply.strAddr.u16Port;
            strAccept.strAddr.sin_addr.s_addr = strAcceptReply.strAddr.u32IPAddr;
            if(gpfAppSocketCb)
                gpfAppSocketCb(strAcceptReply.sListenSock, SOCKET_MSG_ACCEPT, &strAccept);
        }
    }
    else if((u8OpCode == SOCKET_CMD_CONNECT) || (u8OpCode == SOCKET_CMD_SSL_CONNECT) || (u8OpCode == SOCKET_CMD_SSL_CONNECT_ALPN))
    {
        /* Note that for successful connections the fw always sends SOCKET_CMD_CONNECT, even for SSL connections. */
        tstrConnectAlpnReply    strConnectAlpnReply;
        tstrSocketConnectMsg    strConnMsg;
        uint16                  u16HifSz = sizeof(tstrConnectAlpnReply);
        if(u8OpCode != SOCKET_CMD_SSL_CONNECT_ALPN)
            u16HifSz = sizeof(tstrConnectReply);
        if(hif_receive(u32Address, (uint8*)&strConnectAlpnReply, u16HifSz, 0) == M2M_SUCCESS)
        {
            if((strConnectAlpnReply.strConnReply.sock >= 0) && (strConnectAlpnReply.strConnReply.sock < MAX_SOCKET))
            {
                uint8 u8Msg = SOCKET_MSG_CONNECT;

                strConnMsg.sock     = strConnectAlpnReply.strConnReply.sock;
                strConnMsg.s8Error  = strConnectAlpnReply.strConnReply.s8Error;

                /* If the SOCKET_CMD_SSL_CONNECT op code is received and the socket was already connected, then the
                    callback corresponds to an attempt to make the socket secure. */
                if(0 != gastrSockets[strConnMsg.sock].u16DataOffset)
                {
                    u8Msg = SOCKET_MSG_SECURE;
                }
                if(strConnectAlpnReply.strConnReply.s8Error == SOCK_ERR_NO_ERROR)
                {
                    gastrSockets[strConnMsg.sock].u16DataOffset = strConnectAlpnReply.strConnReply.u16AppDataOffset - M2M_HIF_HDR_OFFSET;
                    gastrSockets[strConnMsg.sock].u8AlpnStatus = strConnectAlpnReply.u8AppProtocolIdx;
                }
                else
                {
                    gastrSockets[strConnMsg.sock].u8ErrSource = strConnectAlpnReply.strConnReply.u8ErrSource;
                    gastrSockets[strConnMsg.sock].u8ErrCode = strConnectAlpnReply.strConnReply.u8ErrCode;
                }
                if(gpfAppSocketCb)
                    gpfAppSocketCb(strConnMsg.sock, u8Msg, &strConnMsg);
            }
        }
    }
    else if(u8OpCode == SOCKET_CMD_DNS_RESOLVE)
    {
        tstrDnsReply    strDnsReply;
        if(hif_receive(u32Address, (uint8 *)&strDnsReply, sizeof(tstrDnsReply), 0) == M2M_SUCCESS)
        {
            if(gpfAppResolveCb)
                gpfAppResolveCb((uint8 *)strDnsReply.acHostName, strDnsReply.u32HostIP);
        }
    }
    else if((u8OpCode == SOCKET_CMD_RECV) || (u8OpCode == SOCKET_CMD_RECVFROM) || (u8OpCode == SOCKET_CMD_SSL_RECV))
    {
        SOCKET              sock;
        sint16              s16RecvStatus;
        tstrRecvReply       strRecvReply;
        uint16              u16ReadSize;
        tstrSocketRecvMsg   strRecvMsg;
        uint8               u8CallbackMsgID = SOCKET_MSG_RECV;
        uint16              u16DataOffset;

        if(u8OpCode == SOCKET_CMD_RECVFROM)
            u8CallbackMsgID = SOCKET_MSG_RECVFROM;

        /* Read RECV REPLY data structure.
        */
        u16ReadSize = sizeof(tstrRecvReply);
        if(hif_receive(u32Address, (uint8 *)&strRecvReply, u16ReadSize, 0) == M2M_SUCCESS)
        {
            if((strRecvReply.sock >= 0) && (strRecvReply.sock < MAX_SOCKET))
            {
                uint16 u16SessionID = 0;

                sock            = strRecvReply.sock;
                u16SessionID = strRecvReply.u16SessionID;
                M2M_DBG("recv callback session ID = %d\r\n", u16SessionID);

                /* Reset the Socket RX Pending Flag.
                */
                gastrSockets[sock].bIsRecvPending = 0;

                s16RecvStatus   = NM_BSP_B_L_16(strRecvReply.s16RecvStatus);
                u16DataOffset   = NM_BSP_B_L_16(strRecvReply.u16DataOffset);
                strRecvMsg.strRemoteAddr.sin_port           = strRecvReply.strRemoteAddr.u16Port;
                strRecvMsg.strRemoteAddr.sin_addr.s_addr    = strRecvReply.strRemoteAddr.u32IPAddr;

                if(u16SessionID == gastrSockets[sock].u16SessionID)
                {
                    if((s16RecvStatus > 0) && (s16RecvStatus < u16BufferSize))
                    {
                        /* Skip incoming bytes until reaching the Start of Application Data.
                        */
                        u32Address += u16DataOffset;

                        /* Read the Application data and deliver it to the application callback in
                        the given application buffer. Firmware since 19.6.4 only sends data up to
                        the size of the application buffer. For TCP, a new call to recv is needed
                        in order to retrieve any outstanding data from firmware.
                        */
                        u16ReadSize = (uint16)s16RecvStatus;
                        Socket_ReadSocketData(sock, &strRecvMsg, u8CallbackMsgID, u32Address, u16ReadSize);
                    }
                    else
                    {
                        /* Don't tidy up here. Application must call close().
                        */
                        strRecvMsg.s16BufferSize    = s16RecvStatus;
                        strRecvMsg.pu8Buffer        = NULL;
                        if(gpfAppSocketCb)
                            gpfAppSocketCb(sock, u8CallbackMsgID, &strRecvMsg);
                    }
                }
                else
                {
                    M2M_DBG("Discard recv callback %d %d \r\n", u16SessionID, gastrSockets[sock].u16SessionID);
                    if(u16ReadSize < u16BufferSize)
					{
                        if(hif_receive(0, NULL, 0, 1) != M2M_SUCCESS)
						{
                            M2M_ERR("hif rx done failed\n");
						}
					}
                }
            }
        }
    }
    else if((u8OpCode == SOCKET_CMD_SEND) || (u8OpCode == SOCKET_CMD_SENDTO) || (u8OpCode == SOCKET_CMD_SSL_SEND))
    {
        SOCKET          sock;
        sint16          s16Rcvd;
        tstrSendReply   strReply;
        uint8           u8CallbackMsgID = SOCKET_MSG_SEND;

        if(u8OpCode == SOCKET_CMD_SENDTO)
            u8CallbackMsgID = SOCKET_MSG_SENDTO;

        if(hif_receive(u32Address, (uint8 *)&strReply, sizeof(tstrSendReply), 0) == M2M_SUCCESS)
        {
            if((strReply.sock >=0) && (strReply.sock < MAX_SOCKET))
            {
                uint16 u16SessionID = 0;

                sock = strReply.sock;
                u16SessionID = strReply.u16SessionID;
                M2M_DBG("send callback session ID = %d\r\n", u16SessionID);

                s16Rcvd = NM_BSP_B_L_16(strReply.s16SentBytes);

                if(u16SessionID == gastrSockets[sock].u16SessionID)
                {
                    if(gpfAppSocketCb)
                        gpfAppSocketCb(sock, u8CallbackMsgID, &s16Rcvd);
                }
                else
                {
                    M2M_DBG("Discard send callback %d %d \r\n", u16SessionID, gastrSockets[sock].u16SessionID);
                }
            }
        }
    }
    else if(u8OpCode == SOCKET_CMD_PING)
    {
        tstrPingReply   strPingReply;
        if(hif_receive(u32Address, (uint8 *)&strPingReply, sizeof(tstrPingReply), 1) == M2M_SUCCESS)
        {
            if((gu32PingId == strPingReply.u32CmdPrivate) && (gfpPingCb != NULL))
            {
                gfpPingCb(strPingReply.u32IPAddr, strPingReply.u32RTT, strPingReply.u8ErrorCode);
            }
        }
    }
}

/*********************************************************************
Function
        socketInit

Description

Return
        None.

Author
        Ahmed Ezzat

Version
        1.0

Date
        4 June 2012
*********************************************************************/
void socketInit(void)
{
    if(gbSocketInit == 0)
    {
        m2m_memset((uint8 *)gastrSockets, 0, MAX_SOCKET * sizeof(tstrSocket));
        hif_register_cb(M2M_REQ_GROUP_IP, m2m_ip_cb);
        gbSocketInit    = 1;
        gu16SessionID   = 0;
    }
}

/*********************************************************************
Function
        socketDeinit

Description

Return
        None.

Author
        Samer Sarhan

Version
        1.0

Date
        27 Feb 2015
*********************************************************************/
void socketDeinit(void)
{
    m2m_memset((uint8 *)gastrSockets, 0, MAX_SOCKET * sizeof(tstrSocket));
    hif_register_cb(M2M_REQ_GROUP_IP, NULL);
    gpfAppSocketCb  = NULL;
    gpfAppResolveCb = NULL;
    gbSocketInit    = 0;
}
/*********************************************************************
Function
        registerSocketCallback

Description

Return
        None.

Author
        Ahmed Ezzat

Version
        1.0

Date
        4 June 2012
*********************************************************************/
void registerSocketCallback(tpfAppSocketCb pfAppSocketCb, tpfAppResolveCb pfAppResolveCb)
{
    gpfAppSocketCb  = pfAppSocketCb;
    gpfAppResolveCb = pfAppResolveCb;
}

/*********************************************************************
Function
        socket

Description
        Creates a socket.

Return
        - Negative value for error.
        - ZERO or positive value as a socket ID if successful.

Author
        Ahmed Ezzat

Version
        1.0

Date
        4 June 2012
*********************************************************************/
SOCKET socket(uint16 u16Domain, uint8 u8Type, uint8 u8Config)
{
    SOCKET                  sock = -1;
    uint8                   u8SockID;
    uint8                   u8Count;
    volatile tstrSocket     *pstrSock;
    static volatile uint8   u8NextTcpSock   = 0;
    static volatile uint8   u8NextUdpSock   = 0;

    /* The only supported family is the AF_INET for UDP and TCP transport layer protocols. */
    if(u16Domain == AF_INET)
    {
        if(u8Type == SOCK_STREAM)
        {
            for(u8Count = 0; u8Count < TCP_SOCK_MAX; u8Count ++)
            {
                u8SockID    = u8NextTcpSock;
                pstrSock    = &gastrSockets[u8NextTcpSock];
                u8NextTcpSock = (u8NextTcpSock + 1) % TCP_SOCK_MAX;
                if(!pstrSock->bIsUsed)
                {
                    sock = (SOCKET)u8SockID;
                    break;
                }
            }
        }
        else if(u8Type == SOCK_DGRAM)
        {
            volatile tstrSocket *pastrUDPSockets = &gastrSockets[TCP_SOCK_MAX];
            for(u8Count = 0; u8Count < UDP_SOCK_MAX; u8Count ++)
            {
                u8SockID        = u8NextUdpSock;
                pstrSock        = &pastrUDPSockets[u8NextUdpSock];
                u8NextUdpSock   = (u8NextUdpSock + 1) % UDP_SOCK_MAX;
                if(!pstrSock->bIsUsed)
                {
                    sock = (SOCKET)(u8SockID + TCP_SOCK_MAX);
                    break;
                }
            }
        }

        if(sock >= 0)
        {
            m2m_memset((uint8 *)pstrSock, 0, sizeof(tstrSocket));
            pstrSock->bIsUsed = 1;

            /* The session ID is used to distinguish different socket connections
                by comparing the assigned session ID to the one reported by the firmware*/
            ++gu16SessionID;
            if(gu16SessionID == 0)
                ++gu16SessionID;

            pstrSock->u16SessionID = gu16SessionID;
            M2M_INFO("Socket %d session ID = %d\r\n", sock, gu16SessionID);

            if((u8Type == SOCK_STREAM) && (u8Config != SOCKET_CONFIG_SSL_OFF))
            {
                tstrSSLSocketCreateCmd  strSSLCreate;
                strSSLCreate.sslSock = sock;
                SOCKET_REQUEST(SOCKET_CMD_SSL_CREATE, (uint8 *)&strSSLCreate, sizeof(tstrSSLSocketCreateCmd), 0, 0, 0);

                pstrSock->u8SSLFlags = SSL_FLAGS_ACTIVE | SSL_FLAGS_NO_TX_COPY;
                if(u8Config == SOCKET_CONFIG_SSL_DELAY)
                    pstrSock->u8SSLFlags |= SSL_FLAGS_DELAY;
            }
        }
    }
    return sock;
}

/*********************************************************************
Function
        bind

Description
        Request to bind a socket on a local address.

Return


Author
        Ahmed Ezzat

Version
        1.0

Date
        5 June 2012
*********************************************************************/
sint8 bind(SOCKET sock, struct sockaddr *pstrAddr, uint8 u8AddrLen)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;
    if((pstrAddr != NULL) && (sock >= 0) && (sock < MAX_SOCKET) && (gastrSockets[sock].bIsUsed == 1) && (u8AddrLen != 0))
    {
        tstrBindCmd         strBind;
        uint8               u8CMD = SOCKET_CMD_BIND;
        if(gastrSockets[sock].u8SSLFlags & SSL_FLAGS_ACTIVE)
        {
            u8CMD = SOCKET_CMD_SSL_BIND;
        }

        /* Build the bind request. */
        strBind.sock = sock;
        m2m_memcpy((uint8 *)&strBind.strAddr, (uint8 *)pstrAddr, sizeof(tstrSockAddr));
        strBind.u16SessionID        = gastrSockets[sock].u16SessionID;

        /* Send the request. */
        s8Ret = SOCKET_REQUEST(u8CMD, (uint8 *)&strBind, sizeof(tstrBindCmd), NULL, 0, 0);
        if(s8Ret != SOCK_ERR_NO_ERROR)
        {
            s8Ret = SOCK_ERR_INVALID;
        }
    }
    return s8Ret;
}

/*********************************************************************
Function
        listen

Description


Return


Author
        Ahmed Ezzat

Version
        1.0

Date
        5 June 2012
*********************************************************************/
sint8 listen(SOCKET sock, uint8 backlog)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;

    if((sock >= 0) && (sock < MAX_SOCKET) && (gastrSockets[sock].bIsUsed == 1))
    {
        tstrListenCmd       strListen;

        strListen.sock = sock;
        strListen.u8BackLog = backlog;
        strListen.u16SessionID      = gastrSockets[sock].u16SessionID;

        s8Ret = SOCKET_REQUEST(SOCKET_CMD_LISTEN, (uint8 *)&strListen, sizeof(tstrListenCmd), NULL, 0, 0);
        if(s8Ret != SOCK_ERR_NO_ERROR)
        {
            s8Ret = SOCK_ERR_INVALID;
        }
    }
    return s8Ret;
}
/*********************************************************************
Function
        accept

Description

Return


Author
        Ahmed Ezzat

Version
        1.0

Date
        5 June 2012
*********************************************************************/
sint8 accept(SOCKET sock, struct sockaddr *addr, uint8 *addrlen)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;

    if((sock >= 0) && (sock < MAX_SOCKET) && (gastrSockets[sock].bIsUsed == 1))
    {
        s8Ret = SOCK_ERR_NO_ERROR;
    }
    return s8Ret;
}
/*********************************************************************
Function
        connect

Description
        Connect to a remote TCP Server.

Return


Author
        Ahmed Ezzat

Version
        1.0

Date
        5 June 2012
*********************************************************************/
sint8 connect(SOCKET sock, struct sockaddr *pstrAddr, uint8 u8AddrLen)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;
    if((sock >= 0) && (sock < MAX_SOCKET) && (pstrAddr != NULL) && (gastrSockets[sock].bIsUsed == 1) && (u8AddrLen != 0))
    {
        tstrConnectCmd  strConnect;
        uint8           u8Cmd = SOCKET_CMD_CONNECT;
        if((gastrSockets[sock].u8SSLFlags) & SSL_FLAGS_ACTIVE)
        {
            u8Cmd = SOCKET_CMD_SSL_CONNECT;
            strConnect.u8SslFlags = gastrSockets[sock].u8SSLFlags;
        }
        strConnect.sock = sock;
        m2m_memcpy((uint8 *)&strConnect.strAddr, (uint8 *)pstrAddr, sizeof(tstrSockAddr));

        strConnect.u16SessionID     = gastrSockets[sock].u16SessionID;
        s8Ret = SOCKET_REQUEST(u8Cmd, (uint8 *)&strConnect, sizeof(tstrConnectCmd), NULL, 0, 0);
        if(s8Ret != SOCK_ERR_NO_ERROR)
        {
            s8Ret = SOCK_ERR_INVALID;
        }
    }
    return s8Ret;
}
/*********************************************************************
Function
        secure

Description
        Make secure (TLS) an open TCP client connection.

Return


Author
        Matthew Gunton

Version
        1.0

Date
        7 November 2019
*********************************************************************/
sint8 secure(SOCKET sock)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;
    if((sock >= 0) && (sock < MAX_SOCKET) && (gastrSockets[sock].bIsUsed == 1))
    {
        if(
                (gastrSockets[sock].u8SSLFlags & SSL_FLAGS_ACTIVE)
            &&  (gastrSockets[sock].u8SSLFlags & SSL_FLAGS_DELAY)
            &&  (gastrSockets[sock].u16DataOffset != 0)
        )
        {
            tstrConnectCmd strConnect;

            gastrSockets[sock].u8SSLFlags &= ~SSL_FLAGS_DELAY;
            strConnect.u8SslFlags = gastrSockets[sock].u8SSLFlags;
            strConnect.sock = sock;
            strConnect.u16SessionID = gastrSockets[sock].u16SessionID;

            s8Ret = SOCKET_REQUEST(SOCKET_CMD_SECURE, (uint8*)&strConnect, sizeof(tstrConnectCmd), NULL, 0, 0);
            if(s8Ret != SOCK_ERR_NO_ERROR)
            {
                s8Ret = SOCK_ERR_INVALID;
            }
        }
    }
    return s8Ret;
}
/*********************************************************************
Function
        send

Description

Return

Author
        Ahmed Ezzat

Version
        1.0

Date
        5 June 2012
*********************************************************************/
sint16 send(SOCKET sock, void *pvSendBuffer, uint16 u16SendLength, uint16 flags)
{
    sint16  s16Ret = SOCK_ERR_INVALID_ARG;

    if((sock >= 0) && (sock < MAX_SOCKET) && (pvSendBuffer != NULL) && (u16SendLength <= SOCKET_BUFFER_MAX_LENGTH) && (gastrSockets[sock].bIsUsed == 1))
    {
        uint16          u16DataOffset;
        tstrSendCmd     strSend;
        uint8           u8Cmd;

        u8Cmd           = SOCKET_CMD_SEND;
        u16DataOffset   = TCP_TX_PACKET_OFFSET;

        strSend.sock            = sock;
        strSend.u16DataSize     = NM_BSP_B_L_16(u16SendLength);
        strSend.u16SessionID    = gastrSockets[sock].u16SessionID;

        if(sock >= TCP_SOCK_MAX)
        {
            u16DataOffset = UDP_TX_PACKET_OFFSET;
        }
        if(
                (gastrSockets[sock].u8SSLFlags & SSL_FLAGS_ACTIVE)
            &&  (!(gastrSockets[sock].u8SSLFlags & SSL_FLAGS_DELAY))
        )
        {
            u8Cmd           = SOCKET_CMD_SSL_SEND;
            u16DataOffset   = gastrSockets[sock].u16DataOffset;
        }

        s16Ret = SOCKET_REQUEST(u8Cmd | M2M_REQ_DATA_PKT, (uint8*)&strSend, sizeof(tstrSendCmd), (uint8*)pvSendBuffer, u16SendLength, u16DataOffset);
        if(s16Ret != SOCK_ERR_NO_ERROR)
        {
            s16Ret = SOCK_ERR_BUFFER_FULL;
        }
    }
    return s16Ret;
}
/*********************************************************************
Function
        sendto

Description

Return

Author
        Ahmed Ezzat

Version
        1.0

Date
        4 June 2012
*********************************************************************/
sint16 sendto(SOCKET sock, void *pvSendBuffer, uint16 u16SendLength, uint16 flags, struct sockaddr *pstrDestAddr, uint8 u8AddrLen)
{
    sint16  s16Ret = SOCK_ERR_INVALID_ARG;

    if((sock >= 0) && (sock < MAX_SOCKET) && (pvSendBuffer != NULL) && (u16SendLength <= SOCKET_BUFFER_MAX_LENGTH) && (gastrSockets[sock].bIsUsed == 1))
    {
        if(gastrSockets[sock].bIsUsed)
        {
            tstrSendCmd strSendTo;

            m2m_memset((uint8 *)&strSendTo, 0, sizeof(tstrSendCmd));

            strSendTo.sock          = sock;
            strSendTo.u16DataSize   = NM_BSP_B_L_16(u16SendLength);
            strSendTo.u16SessionID  = gastrSockets[sock].u16SessionID;

            if(pstrDestAddr != NULL)
            {
                sockaddr_in *pstrAddr = reinterpret_cast<sockaddr_in*>(pstrDestAddr);

                strSendTo.strAddr.u16Family = pstrAddr->sin_family;
                strSendTo.strAddr.u16Port   = pstrAddr->sin_port;
                strSendTo.strAddr.u32IPAddr = pstrAddr->sin_addr.s_addr;
            }
            s16Ret = SOCKET_REQUEST(SOCKET_CMD_SENDTO|M2M_REQ_DATA_PKT, (uint8 *)&strSendTo,  sizeof(tstrSendCmd),
                                    reinterpret_cast<uint8*>(pvSendBuffer), u16SendLength, UDP_TX_PACKET_OFFSET);

            if(s16Ret != SOCK_ERR_NO_ERROR)
            {
                s16Ret = SOCK_ERR_BUFFER_FULL;
            }
        }
    }
    return s16Ret;
}
/*********************************************************************
Function
        recv

Description

Return


Author
        Ahmed Ezzat

Version
        1.0
        2.0  9 April 2013 --> Add timeout for recv operation.

Date
        5 June 2012
*********************************************************************/
sint16 recv(SOCKET sock, void *pvRecvBuf, uint16 u16BufLen, uint32 u32Timeoutmsec)
{
    sint16  s16Ret = SOCK_ERR_INVALID_ARG;

    if((sock >= 0) && (sock < MAX_SOCKET) && (pvRecvBuf != NULL) && (u16BufLen != 0) && (gastrSockets[sock].bIsUsed == 1))
    {
        s16Ret = SOCK_ERR_NO_ERROR;
        gastrSockets[sock].pu8UserBuffer        = (uint8 *)pvRecvBuf;
        gastrSockets[sock].u16UserBufferSize    = u16BufLen;

        if(!gastrSockets[sock].bIsRecvPending)
        {
            tstrRecvCmd strRecv;
            uint8       u8Cmd = SOCKET_CMD_RECV;

            gastrSockets[sock].bIsRecvPending = 1;
            if(
                    (gastrSockets[sock].u8SSLFlags & SSL_FLAGS_ACTIVE)
                &&  (!(gastrSockets[sock].u8SSLFlags & SSL_FLAGS_DELAY))
            )
            {
                u8Cmd = SOCKET_CMD_SSL_RECV;
            }

            /* Check the timeout value. */
            if(u32Timeoutmsec == 0)
                strRecv.u32Timeoutmsec = 0xFFFFFFFF;
            else
                strRecv.u32Timeoutmsec = NM_BSP_B_L_32(u32Timeoutmsec);
            strRecv.sock = sock;
            strRecv.u16SessionID        = gastrSockets[sock].u16SessionID;
            strRecv.u16BufLen           = u16BufLen;
		
			s16Ret = SOCKET_REQUEST(u8Cmd, (uint8*)&strRecv, sizeof(tstrRecvCmd), NULL , 0, 0);
            if(s16Ret != SOCK_ERR_NO_ERROR)
            {
                s16Ret = SOCK_ERR_BUFFER_FULL;
            }
        }
    }
    return s16Ret;
}
/*********************************************************************
Function
        close

Description

Return
        None.

Author
        Ahmed Ezzat

Version
        1.0

Date
        4 June 2012
*********************************************************************/
sint8 close(SOCKET sock)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;
    M2M_INFO("Sock to delete <%d>\n", sock);
    if((sock >= 0) && (sock < MAX_SOCKET) && (gastrSockets[sock].bIsUsed == 1))
    {
        uint8   u8Cmd = SOCKET_CMD_CLOSE;
        tstrCloseCmd strclose;
        strclose.sock = sock;
        strclose.u16SessionID       = gastrSockets[sock].u16SessionID;

        if(gastrSockets[sock].u8SSLFlags & SSL_FLAGS_ACTIVE)
        {
            u8Cmd = SOCKET_CMD_SSL_CLOSE;
        }
        s8Ret = SOCKET_REQUEST(u8Cmd, (uint8 *)&strclose, sizeof(tstrCloseCmd), NULL, 0, 0);
        if(s8Ret != SOCK_ERR_NO_ERROR)
        {
            s8Ret = SOCK_ERR_INVALID;
        }
        m2m_memset((uint8 *)&gastrSockets[sock], 0, sizeof(tstrSocket));
    }
    return s8Ret;
}
/*********************************************************************
Function
        recvfrom

Description

Return


Author
        Ahmed Ezzat

Version
        1.0
        2.0  9 April 2013 --> Add timeout for recv operation.

Date
        5 June 2012
*********************************************************************/
sint16 recvfrom(SOCKET sock, void *pvRecvBuf, uint16 u16BufLen, uint32 u32Timeoutmsec)
{
    sint16  s16Ret = SOCK_ERR_NO_ERROR;
    if((sock >= 0) && (sock < MAX_SOCKET) && (pvRecvBuf != NULL) && (u16BufLen != 0) && (gastrSockets[sock].bIsUsed == 1))
    {
        if(gastrSockets[sock].bIsUsed)
        {
            s16Ret = SOCK_ERR_NO_ERROR;
            gastrSockets[sock].pu8UserBuffer = (uint8 *)pvRecvBuf;
            gastrSockets[sock].u16UserBufferSize = u16BufLen;

            if(!gastrSockets[sock].bIsRecvPending)
            {
                tstrRecvCmd strRecv;
                gastrSockets[sock].bIsRecvPending = 1;

                /* Check the timeout value. */
                if(u32Timeoutmsec == 0)
                    strRecv.u32Timeoutmsec = 0xFFFFFFFF;
                else
                    strRecv.u32Timeoutmsec = NM_BSP_B_L_32(u32Timeoutmsec);
                strRecv.sock = sock;
                strRecv.u16SessionID        = gastrSockets[sock].u16SessionID;
                strRecv.u16BufLen           = u16BufLen;
				
				s16Ret = SOCKET_REQUEST(SOCKET_CMD_RECVFROM, (uint8*)&strRecv, sizeof(tstrRecvCmd), NULL , 0, 0);
				if(s16Ret != SOCK_ERR_NO_ERROR)
                {
                    s16Ret = SOCK_ERR_BUFFER_FULL;
                }
            }
        }
    }
    else
    {
        s16Ret = SOCK_ERR_INVALID_ARG;
    }
    return s16Ret;
}
/*********************************************************************
Function
        nmi_inet_addr

Description

Return
        Unsigned 32-bit integer representing the IP address in Network
        byte order.

Author
        Ahmed Ezzat

Version
        1.0

Date
        4 June 2012
*********************************************************************/
uint32 nmi_inet_addr(char *pcIpAddr)
{
    uint8   tmp   = 0;
    uint32  u32IP = 0;
    uint8   au8IP[4];
    uint8   c;
    uint8   i, j;

    for(i = 0; i < 4; ++i)
    {
        j = 0;
        do
        {
            c = *pcIpAddr;
            ++j;
            if(j > 4)
            {
                return 0;
            }
            if(c == '.' || c == 0)
            {
                au8IP[i] = tmp;
                tmp = 0;
            }
            else if(c >= '0' && c <= '9')
            {
                tmp = (tmp * 10) + (c - '0');
            }
            else
            {
                return 0;
            }
            ++pcIpAddr;
        } while(c != '.' && c != 0);
    }
    m2m_memcpy((uint8 *)&u32IP, au8IP, 4);
    return u32IP;
}

/*********************************************************************
Function
        gethostbyname

Description

Return
        None.

Author
        Ahmed Ezzat

Version
        1.0

Date
        4 June 2012
*********************************************************************/
sint8 gethostbyname(uint8 *pcHostName)
{
    sint8   s8Err = SOCK_ERR_INVALID_ARG;
    uint8   u8HostNameSize = (uint8)m2m_strlen(pcHostName);
    if(u8HostNameSize <= HOSTNAME_MAX_SIZE)
    {
        s8Err = SOCKET_REQUEST(SOCKET_CMD_DNS_RESOLVE, (uint8 *)pcHostName, u8HostNameSize + 1, NULL, 0, 0);
    }
    return s8Err;
}

/*********************************************************************
Function
        setsockopt

Description

Return
        None.

Author
        Abdelrahman Diab

Version
        1.0

Date
        9 September 2014
*********************************************************************/
static sint8 sslSetSockOpt(SOCKET sock, uint8  u8Opt, const void *pvOptVal, uint16 u16OptLen)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;
    if(sock < TCP_SOCK_MAX)
    {
        if(gastrSockets[sock].u8SSLFlags & SSL_FLAGS_ACTIVE)
        {
            uint8   sslFlag = 0;

            s8Ret = SOCK_ERR_NO_ERROR;
            if(u16OptLen == sizeof(int))
            {
                if(u8Opt == SO_SSL_BYPASS_X509_VERIF)
                {
                    sslFlag = SSL_FLAGS_BYPASS_X509;
                }
                else if(u8Opt == SO_SSL_ENABLE_SESSION_CACHING)
                {
                    sslFlag = SSL_FLAGS_CACHE_SESSION;
                }
                else if(u8Opt == SO_SSL_ENABLE_SNI_VALIDATION)
                {
                    sslFlag = SSL_FLAGS_CHECK_SNI;
                }
            }
            if(sslFlag)
            {
                int optVal = *((int*)pvOptVal);
                if(optVal)
                {
                    gastrSockets[sock].u8SSLFlags |= sslFlag;
                }
                else
                {
                    gastrSockets[sock].u8SSLFlags &= ~sslFlag;
                }
            }
            else if(
                ((u8Opt == SO_SSL_SNI) && (u16OptLen < HOSTNAME_MAX_SIZE))
                || ((u8Opt == SO_SSL_ALPN) && (u16OptLen <= ALPN_LIST_MAX_SIZE))
            )
            {
                tstrSSLSetSockOptCmd strCmd;

                strCmd.sock         = sock;
                strCmd.u16SessionID = gastrSockets[sock].u16SessionID;
                strCmd.u8Option     = u8Opt;
                strCmd.u32OptLen    = u16OptLen;
                m2m_memcpy(strCmd.au8OptVal, (uint8*)pvOptVal, u16OptLen);

                s8Ret = SOCKET_REQUEST(SOCKET_CMD_SSL_SET_SOCK_OPT, (uint8*)&strCmd, sizeof(tstrSSLSetSockOptCmd), 0, 0, 0);
                if(s8Ret == M2M_ERR_MEM_ALLOC)
                {
                    s8Ret = SOCKET_REQUEST(SOCKET_CMD_SSL_SET_SOCK_OPT | M2M_REQ_DATA_PKT,
                                           (uint8*)&strCmd, sizeof(tstrSSLSetSockOptCmd), 0, 0, 0);
                }
            }
            else
            {
                M2M_ERR("Unknown SSL Socket Option %d\n", u8Opt);
                s8Ret = SOCK_ERR_INVALID_ARG;
            }
        }
        else
        {
            M2M_ERR("Not SSL Socket\n");
        }
    }
    return s8Ret;
}

/*********************************************************************
Function
        setsockopt

Description

Return
        None.

Author
        Abdelrahman Diab

Version
        1.0

Date
        9 September 2014
*********************************************************************/
sint8 setsockopt(SOCKET sock, uint8  u8Level, uint8  option_name,
                 const void *option_value, uint16 u16OptionLen)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;
    if((sock >= 0) && (sock < MAX_SOCKET) && (option_value != NULL) && (gastrSockets[sock].bIsUsed == 1))
    {
        if(u8Level == SOL_SSL_SOCKET)
        {
            s8Ret = sslSetSockOpt(sock, option_name, option_value, u16OptionLen);
        }
        else if(u8Level == SOL_SOCKET)
        {
            if(u16OptionLen == sizeof(uint32))
            {
                uint8   u8Cmd = SOCKET_CMD_SET_SOCKET_OPTION;
                tstrSetSocketOptCmd strSetSockOpt;
                strSetSockOpt.u8Option=option_name;
                strSetSockOpt.sock = sock;
                strSetSockOpt.u32OptionValue = *(uint32*)option_value;
                strSetSockOpt.u16SessionID      = gastrSockets[sock].u16SessionID;

                s8Ret = SOCKET_REQUEST(u8Cmd, (uint8*)&strSetSockOpt, sizeof(tstrSetSocketOptCmd), NULL, 0, 0);
                if(s8Ret != SOCK_ERR_NO_ERROR)
                {
                    s8Ret = SOCK_ERR_INVALID;
                }
            }
        }
    }
    return s8Ret;
}

/*********************************************************************
Function
        getsockopt

Description

Return
        None.

Author
        Ahmed Ezzat

Version
        1.0

Date
        24 August 2014
*********************************************************************/
sint8 getsockopt(SOCKET sock, uint8 u8Level, uint8 u8OptName, const void *pvOptValue, uint8 *pu8OptLen)
{
    // This is not implemented so return a value that will cause failure should this be used.
    return SOCK_ERR_INVALID_ARG;
}

/*********************************************************************
Function
    m2m_ping_req

Description
    Send Ping request.

Return

Author
    Ahmed Ezzat

Version
    1.0

Date
    4 June 2015
*********************************************************************/
sint8 m2m_ping_req(uint32 u32DstIP, uint8 u8TTL, tpfPingCb fpPingCb)
{
    sint8   s8Ret = M2M_ERR_INVALID_ARG;

    if((u32DstIP != 0) && (fpPingCb != NULL))
    {
        tstrPingCmd strPingCmd;
        strPingCmd.u16PingCount     = 1;
        strPingCmd.u32DestIPAddr    = u32DstIP;
        strPingCmd.u32CmdPrivate    = ++gu32PingId;
        strPingCmd.u8TTL            = u8TTL;

        gfpPingCb = fpPingCb;
        s8Ret = SOCKET_REQUEST(SOCKET_CMD_PING, (uint8 *)&strPingCmd, sizeof(tstrPingCmd), NULL, 0, 0);
    }
    return s8Ret;
}

/*********************************************************************
Function
    set_alpn_protocol_list

Description
    This function sets the protocol list used for application-layer protocol negotiation (ALPN).
    If used, it must be called after creating a SSL socket (using @ref socket) and before
    connecting/binding (using @ref connect or @ref bind).

Return
    The function returns @ref M2M_SUCCESS for successful operations and a negative value otherwise.
*********************************************************************/
sint8 set_alpn_list(SOCKET sock, const char *pcProtocolList)
{
    sint8   s8Ret = SOCK_ERR_INVALID_ARG;

    if((sock >= 0) && (sock < TCP_SOCK_MAX) && (pcProtocolList != NULL))
    {
        uint8   u8Length = m2m_strlen((uint8 *)pcProtocolList);
        if((u8Length > 0) && (u8Length < ALPN_LIST_MAX_APP_LENGTH))
        {
            /*
                ALPN socket option requires Alpn list in this format:
                 0       1       2       3 ... (bytes)
                +-------+-------+-------+  ...        +-------+  ...        +-------+  ...
                | Length L (BE) | len1  | name1...    | len2  | name2...    | len3  | name3...
                +-------+-------+-------+  ...        +-------+  ...        +-------+  ...
                Length fields do not include themselves.
            */
            uint8   au8AlpnList[ALPN_LIST_MAX_SIZE] = {0};
            uint8   *pu8Ptr = &au8AlpnList[3] + u8Length;
            uint8   u8Len = 0;

            m2m_memcpy(&au8AlpnList[3], (uint8 *)pcProtocolList, u8Length);
            u8Length++;
            au8AlpnList[1] = u8Length;
            au8AlpnList[2] = ' ';

            /* Convert space characters into length fields. */
            while(u8Length--)
            {
                if(*--pu8Ptr == ' ')
                {
                    if(u8Len == 0) goto ERR;
                    *pu8Ptr = u8Len;
                    u8Len = 0;
                }
                else u8Len++;
            }
            s8Ret = setsockopt(sock, SOL_SSL_SOCKET, SO_SSL_ALPN, au8AlpnList, sizeof(au8AlpnList));
        }
    }
ERR:
    return s8Ret;
}
/*********************************************************************
Function
    get_alpn_protocol_index

Description
    This function gets the protocol list used for application-layer protocol negotiation (ALPN).
    If used, it must be called after creating a SSL socket (using @ref socket) and before
    connecting/binding (using @ref connect or @ref bind).

Return
    The function returns the index of the selected application-layer protocol.
    Special values:
    0: no negotiation has occurred.
    <0: error.
*********************************************************************/
sint8 get_alpn_index(SOCKET sock)
{
    if(sock >= TCP_SOCK_MAX || sock < 0)
        return SOCK_ERR_INVALID_ARG;
    if(!(gastrSockets[sock].u8SSLFlags & SSL_FLAGS_ACTIVE) || !gastrSockets[sock].bIsUsed)
        return SOCK_ERR_INVALID_ARG;
    return gastrSockets[sock].u8AlpnStatus;
}

/*********************************************************************
Function
    sslEnableCertExpirationCheck

Description
    Enable/Disable TLS Certificate Expiration Check.

Return

Author
    Ahmed Ezzat

Version
    1.0

Date

*********************************************************************/
sint8 sslEnableCertExpirationCheck(tenuSslCertExpSettings enuValidationSetting)
{
    tstrSslCertExpSettings  strSettings;
    strSettings.u32CertExpValidationOpt = (uint32)enuValidationSetting;
    return SOCKET_REQUEST(SOCKET_CMD_SSL_EXP_CHECK, (uint8 *)&strSettings, sizeof(tstrSslCertExpSettings), NULL, 0, 0);
}

/*********************************************************************
Function
        IsSocketReady

Description

Return
        None.

Author


Version
        1.0

Date
        24 Apr 2018
*********************************************************************/
uint8 IsSocketReady(void)
{
    return gbSocketInit;
}
/*********************************************************************
Function
    get_error_detail

Description
    This function gets detail about a socket failure.
    The application can call this when notified of a socket failure via
    @ref SOCKET_MSG_CONNECT or @ref SOCKET_MSG_RECV.
    If used, it must be called before @ref close.

Return
    The function returns @ref SOCK_ERR_NO_ERROR if the request is successful
    and a negative value otherwise.
*********************************************************************/
sint8 get_error_detail(SOCKET sock, tstrSockErr *pstrErr)
{
    if((sock >= TCP_SOCK_MAX) || (sock < 0) || (pstrErr == NULL))
        return SOCK_ERR_INVALID_ARG;
    if(!gastrSockets[sock].bIsUsed)
        return SOCK_ERR_INVALID_ARG;
    pstrErr->enuErrSource = static_cast<tenuSockErrSource>(gastrSockets[sock].u8ErrSource);
    pstrErr->u8ErrCode = gastrSockets[sock].u8ErrCode;
    return SOCK_ERR_NO_ERROR;
}
