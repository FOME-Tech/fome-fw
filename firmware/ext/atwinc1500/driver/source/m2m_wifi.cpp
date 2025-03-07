/**
 *
 * \file
 *
 * \brief This module contains M2M Wi-Fi APIs implementation.
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

#include "driver/include/m2m_wifi.h"
#include "driver/source/m2m_hif.h"
#include "driver/source/nmasic.h"
#include <string.h>

/* Require authentication of server. */
#define WIFI_1X_TLS_HS_FLAGS_PEER_AUTH          NBIT1
/* Enable expiry checking of server certificate chain. */
#define WIFI_1X_TLS_HS_FLAGS_PEER_CERTTIMECHECK NBIT2
/* Require local system time to be known (i.e. fail expiry checking if time is not known locally). */
#define WIFI_1X_TLS_HS_FLAGS_REQUIRE_TIME       NBIT3
/* Enable TLS session caching. */
#define WIFI_1X_TLS_HS_FLAGS_SESSION_CACHING    NBIT4
/* Reserved, this bit must be clear. */
#define WIFI_1X_TLS_HS_FLAGS_RSV5               NBIT5
/* Require server authentication to be against a specified root certificate. */
#define WIFI_1X_TLS_HS_FLAGS_SPECIFY_ROOTCERT   NBIT6
/* Reserved, this bit must be clear. */
#define WIFI_1X_TLS_HS_FLAGS_RSV7               NBIT7

#define WIFI_1X_TLS_HS_FLAGS_DEFAULT    (           \
            WIFI_1X_TLS_HS_FLAGS_PEER_AUTH          \
        |   WIFI_1X_TLS_HS_FLAGS_PEER_CERTTIMECHECK \
        |   WIFI_1X_TLS_HS_FLAGS_SESSION_CACHING    \
)

static volatile uint8 gu8ChNum;
static volatile uint8 gu8WifiState      = WIFI_STATE_DEINIT;
static tpfAppWifiCb gpfAppWifiCb        = NULL;
static volatile uint8 gu8scanInProgress = 0;

static uint32   gu321xTlsHsFlags      = WIFI_1X_TLS_HS_FLAGS_DEFAULT;
static uint8    gau81xRootSha1[20]    = {0};

#ifdef ETH_MODE
static tpfAppEthCb  gpfAppEthCb   = NULL;
static uint8       *gau8ethRcvBuf = NULL;
static uint16       gu16ethRcvBufSize;
#endif

/**
@fn         void m2m_wifi_cb(uint8 u8OpCode, uint16 u16DataSize, uint32 u32Addr, uint8 grp)
@brief      Internal WiFi callback function.
@param[in]  u8OpCode
                HIF Opcode type.
@param[in]  u16DataSize
                HIF data length.
@param[in]  u32Addr
                HIF address.
@param[in]  grp
                HIF group type.
*/
static void m2m_wifi_cb(uint8 u8OpCode, uint16 u16DataSize, uint32 u32Addr)
{
    uint8 rx_buf[8];
    if(u8OpCode == M2M_WIFI_RESP_CON_STATE_CHANGED)
    {
        tstrM2mWifiStateChanged strState;
        if(hif_receive(u32Addr, (uint8 *) &strState, sizeof(tstrM2mWifiStateChanged), 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_CON_STATE_CHANGED, &strState);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_GET_SYS_TIME)
    {
        tstrSystemTime strSysTime;
        if(hif_receive(u32Addr, (uint8 *) &strSysTime, sizeof(tstrSystemTime), 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_GET_SYS_TIME, &strSysTime);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_CONN_INFO)
    {
        tstrM2MConnInfo     strConnInfo;
        if(hif_receive(u32Addr, (uint8 *)&strConnInfo, sizeof(tstrM2MConnInfo), 1) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_CONN_INFO, &strConnInfo);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_MEMORY_RECOVER)
    {
#if 0
        if(hif_receive(u32Addr, rx_buf, 4, 1) == M2M_SUCCESS)
        {
            tstrM2mWifiStateChanged strState;
            m2m_memcpy((uint8 *) &strState, rx_buf, sizeof(tstrM2mWifiStateChanged));
            if(app_wifi_recover_cb)
                app_wifi_recover_cb(strState.u8CurrState);
        }
#endif
    }
    else if(u8OpCode == M2M_WIFI_REQ_DHCP_CONF)
    {
        tstrM2MIPConfig strIpConfig;
        if(hif_receive(u32Addr, (uint8 *)&strIpConfig, sizeof(tstrM2MIPConfig), 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_REQ_DHCP_CONF, (uint8 *)&strIpConfig);
        }
    }
    else if(u8OpCode == M2M_WIFI_REQ_DHCP_FAILURE)
    {
        if(hif_receive(u32Addr, NULL, 0, 1) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_REQ_DHCP_FAILURE, NULL);
        }
    }
    else if(u8OpCode == M2M_WIFI_REQ_WPS)
    {
        tstrM2MWPSInfo strWps;
        m2m_memset((uint8 *)&strWps, 0, sizeof(tstrM2MWPSInfo));
        if(hif_receive(u32Addr, (uint8 *)&strWps, sizeof(tstrM2MWPSInfo), 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_REQ_WPS, &strWps);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_IP_CONFLICT)
    {
        uint32  u32ConflictedIP;
        if(hif_receive(u32Addr, (uint8 *)&u32ConflictedIP, sizeof(u32ConflictedIP), 0) == M2M_SUCCESS)
        {
            M2M_INFO("Conflicted IP \" %u.%u.%u.%u \" \n",
                     BYTE_0(u32ConflictedIP), BYTE_1(u32ConflictedIP), BYTE_2(u32ConflictedIP), BYTE_3(u32ConflictedIP));
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_IP_CONFLICT, NULL);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_SCAN_DONE)
    {
        tstrM2mScanDone strState;
        gu8scanInProgress = 0;
        if(hif_receive(u32Addr, (uint8 *)&strState, sizeof(tstrM2mScanDone), 0) == M2M_SUCCESS)
        {
            gu8ChNum = strState.u8NumofCh;
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_SCAN_DONE, &strState);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_SCAN_RESULT)
    {
        tstrM2mWifiscanResult strScanResult;
        if(hif_receive(u32Addr, (uint8 *)&strScanResult, sizeof(tstrM2mWifiscanResult), 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_SCAN_RESULT, &strScanResult);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_CURRENT_RSSI)
    {
        if(hif_receive(u32Addr, rx_buf, 4, 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_CURRENT_RSSI, rx_buf);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_CLIENT_INFO)
    {
        if(hif_receive(u32Addr, rx_buf, 4, 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_CLIENT_INFO, rx_buf);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_PROVISION_INFO)
    {
        tstrM2MProvisionInfo    strProvInfo;
        if(hif_receive(u32Addr, (uint8 *)&strProvInfo, sizeof(tstrM2MProvisionInfo), 1) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_PROVISION_INFO, &strProvInfo);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_DEFAULT_CONNECT)
    {
        tstrM2MDefaultConnResp  strResp;
        if(hif_receive(u32Addr, (uint8 *)&strResp, sizeof(tstrM2MDefaultConnResp), 1) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_RESP_DEFAULT_CONNECT, &strResp);
        }
    }
    else if(u8OpCode == M2M_WIFI_REQRSP_DELETE_APID)
    {
        tstrM2MGenericResp strResp;
        if(hif_receive(u32Addr, (uint8 *)&strResp, sizeof(tstrM2MGenericResp), 0) == M2M_SUCCESS)
        {
            if(gpfAppWifiCb)
                gpfAppWifiCb(M2M_WIFI_REQRSP_DELETE_APID, &strResp);
        }
    }
    else if(u8OpCode == M2M_WIFI_RESP_GET_PRNG)
    {
        tstrPrng strPrng;
        if(hif_receive(u32Addr, (uint8 *)&strPrng, sizeof(tstrPrng), 0) == M2M_SUCCESS)
        {
            if(hif_receive(u32Addr + sizeof(tstrPrng), strPrng.pu8RngBuff, strPrng.u16PrngSize, 1) == M2M_SUCCESS)
            {
                if(gpfAppWifiCb)
                    gpfAppWifiCb(M2M_WIFI_RESP_GET_PRNG, &strPrng);
            }
        }
    }
#ifdef ETH_MODE
    else if(u8OpCode == M2M_WIFI_RESP_ETHERNET_RX_PACKET)
    {
        uint8 u8SetRxDone;
        tstrM2mIpRsvdPkt strM2mRsvd;
        if(hif_receive(u32Addr, &strM2mRsvd, sizeof(tstrM2mIpRsvdPkt), 0) == M2M_SUCCESS)
        {
            tstrM2mIpCtrlBuf  strM2mIpCtrlBuf;
            uint16 u16Offset = strM2mRsvd.u16PktOffset;
            strM2mIpCtrlBuf.u16RemainingDataSize = strM2mRsvd.u16PktSz;
            if((gpfAppEthCb) && (gau8ethRcvBuf) && (gu16ethRcvBufSize > 0))
            {
                do
                {
                    u8SetRxDone = 1;
                    if(strM2mIpCtrlBuf.u16RemainingDataSize > gu16ethRcvBufSize)
                    {
                        u8SetRxDone = 0;
                        strM2mIpCtrlBuf.u16DataSize = gu16ethRcvBufSize;
                    }
                    else
                    {
                        strM2mIpCtrlBuf.u16DataSize = strM2mIpCtrlBuf.u16RemainingDataSize;
                    }

                    if(hif_receive(u32Addr + u16Offset, gau8ethRcvBuf, strM2mIpCtrlBuf.u16DataSize, u8SetRxDone) == M2M_SUCCESS)
                    {
                        strM2mIpCtrlBuf.u16RemainingDataSize -= strM2mIpCtrlBuf.u16DataSize;
                        u16Offset += strM2mIpCtrlBuf.u16DataSize;
                        gpfAppEthCb(M2M_WIFI_RESP_ETHERNET_RX_PACKET, gau8ethRcvBuf, &(strM2mIpCtrlBuf));
                    }
                    else
                    {
                        break;
                    }
                } while(strM2mIpCtrlBuf.u16RemainingDataSize > 0);
            }
        }
    }
#endif  /* ETH_MODE */
    else
    {
        M2M_ERR("REQ Not defined %d\n", u8OpCode);
    }
}

sint8 m2m_wifi_download_mode()
{
    sint8 ret = M2M_SUCCESS;
    /* Apply device specific initialization. */
    ret = nm_drv_init_download_mode();
    if(ret != M2M_SUCCESS)  goto _EXIT0;

    enable_interrupts();
    gu8WifiState = WIFI_STATE_INIT;

_EXIT0:
    return ret;
}

static sint8 m2m_validate_ap_parameters(CONST tstrM2MAPModeConfig *pstrM2MAPModeConfig)
{
    sint8 s8Ret = M2M_SUCCESS;
    /* Check for incoming pointer */
    if(pstrM2MAPModeConfig == NULL)
    {
        M2M_ERR("INVALID POINTER\n");
        s8Ret = M2M_ERR_FAIL;
        goto ERR1;
    }
    /* Check for SSID */
    if((m2m_strlen((uint8 *)pstrM2MAPModeConfig->strApConfig.au8SSID) <= 0) || (m2m_strlen((uint8 *)pstrM2MAPModeConfig->strApConfig.au8SSID) >= M2M_MAX_SSID_LEN))
    {
        M2M_ERR("INVALID SSID\n");
        s8Ret = M2M_ERR_FAIL;
        goto ERR1;
    }
    /* Check for Channel */
    if(pstrM2MAPModeConfig->strApConfig.u8ListenChannel > M2M_WIFI_CH_14 || pstrM2MAPModeConfig->strApConfig.u8ListenChannel < M2M_WIFI_CH_1)
    {
        M2M_ERR("INVALID CH\n");
        s8Ret = M2M_ERR_FAIL;
        goto ERR1;
    }
    /* Check for DHCP Server IP address */
    if(!(pstrM2MAPModeConfig->strApConfig.au8DHCPServerIP[0] || pstrM2MAPModeConfig->strApConfig.au8DHCPServerIP[1]))
    {
        if(!(pstrM2MAPModeConfig->strApConfig.au8DHCPServerIP[2]))
        {
            M2M_ERR("INVALID DHCP SERVER IP\n");
            s8Ret = M2M_ERR_FAIL;
            goto ERR1;
        }
    }
    /* Check for Security */
    if(pstrM2MAPModeConfig->strApConfig.u8SecType == M2M_WIFI_SEC_OPEN)
    {
        goto ERR1;
    }
    else if(pstrM2MAPModeConfig->strApConfig.u8SecType == M2M_WIFI_SEC_WEP)
    {
        /* As of 19.7.5 the WEP protocol is deprecated */
        s8Ret = M2M_ERR_FAIL;
        goto ERR1;
    }
    else if(pstrM2MAPModeConfig->strApConfig.u8SecType == M2M_WIFI_SEC_WPA_PSK)
    {
        /* Check for WPA Key size */
        if(((pstrM2MAPModeConfig->strApConfig.u8KeySz + 1) < M2M_MIN_PSK_LEN) || ((pstrM2MAPModeConfig->strApConfig.u8KeySz + 1) > M2M_MAX_PSK_LEN))
        {
            M2M_ERR("INVALID WPA KEY SIZE\n");
            s8Ret = M2M_ERR_FAIL;
            goto ERR1;
        }
    }
    else
    {
        M2M_ERR("INVALID AUTHENTICATION MODE\n");
        s8Ret = M2M_ERR_FAIL;
        goto ERR1;
    }

ERR1:
    return s8Ret;
}

static sint8 m2m_validate_scan_options(tstrM2MScanOption *ptstrM2MScanOption)
{
    sint8 s8Ret = M2M_SUCCESS;
    /* Check for incoming pointer */
    if(ptstrM2MScanOption == NULL)
    {
        M2M_ERR("INVALID POINTER\n");
        s8Ret = M2M_ERR_FAIL;
        goto ERR;
    }
    /* Check for valid No of slots */
    if(ptstrM2MScanOption->u8NumOfSlot == 0)
    {
        M2M_ERR("INVALID No of scan slots! %d\n", ptstrM2MScanOption->u8NumOfSlot);
        s8Ret = M2M_ERR_FAIL;
        goto ERR;
    }
    /* Check for valid time of slots */
    if(ptstrM2MScanOption->u8SlotTime < 10 || ptstrM2MScanOption->u8SlotTime > 250)
    {
        M2M_ERR("INVALID scan slot time! %d\n", ptstrM2MScanOption->u8SlotTime);
        s8Ret = M2M_ERR_FAIL;
        goto ERR;
    }
    /* Check for valid No of probe requests per slot */
    if((ptstrM2MScanOption->u8ProbesPerSlot == 0) || (ptstrM2MScanOption->u8ProbesPerSlot > M2M_SCAN_DEFAULT_NUM_PROBE))
    {
        M2M_ERR("INVALID No of probe requests per scan slot %d\n", ptstrM2MScanOption->u8ProbesPerSlot);
        s8Ret = M2M_ERR_FAIL;
        goto ERR;
    }
    /* Check for valid RSSI threshold */
    if(ptstrM2MScanOption->s8RssiThresh >= 0)
    {
        M2M_ERR("INVALID RSSI threshold %d \n", ptstrM2MScanOption->s8RssiThresh);
        s8Ret = M2M_ERR_FAIL;
    }

ERR:
    return s8Ret;
}

sint8 m2m_wifi_send_crl(tstrTlsCrlInfo *pCRL)
{
    sint8 s8Ret = M2M_ERR_FAIL;
    s8Ret = hif_send(M2M_REQ_GROUP_SSL, M2M_SSL_IND_CRL|M2M_REQ_DATA_PKT, NULL, 0, (uint8 *)pCRL, sizeof(tstrTlsCrlInfo), 0);
    return s8Ret;
}

sint8 m2m_wifi_init_hold(void)
{
    sint8 ret = M2M_ERR_FAIL;

    /* Apply device specific initialization. */
    ret = nm_drv_init_hold();

    if(M2M_SUCCESS == ret)
        gu8WifiState = WIFI_STATE_INIT;

    return ret;
}

sint8 m2m_wifi_init_start(tstrWifiInitParam *pWifiInitParam)
{
    tstrM2mRev strtmp;
    sint8 ret = M2M_SUCCESS;
    uint8 u8WifiMode = M2M_WIFI_MODE_NORMAL;

    if(pWifiInitParam == NULL) {
        ret = M2M_ERR_FAIL;
        goto _EXIT0;
    }

    gpfAppWifiCb = pWifiInitParam->pfAppWifiCb;

#ifdef ETH_MODE
    gpfAppEthCb       = pWifiInitParam->strEthInitParam.pfAppEthCb;
    gau8ethRcvBuf     = pWifiInitParam->strEthInitParam.au8ethRcvBuf;
    gu16ethRcvBufSize = pWifiInitParam->strEthInitParam.u16ethRcvBufSize;
	if (pWifiInitParam->strEthInitParam.u8EthernetEnable)
		u8WifiMode = M2M_WIFI_MODE_ETHERNET;
#endif /* ETH_MODE */

    gu8scanInProgress = 0;
    /* Apply device specific initialization. */
    ret = nm_drv_init_start(&u8WifiMode);
    if(ret != M2M_SUCCESS) goto _EXIT0;

    gu8WifiState = WIFI_STATE_START;

    /* Initialize host interface module */
    ret = hif_init(NULL);
    if(ret != M2M_SUCCESS) goto _EXIT1;

    hif_register_cb(M2M_REQ_GROUP_WIFI, m2m_wifi_cb);

    ret = nm_get_firmware_full_info(&strtmp);

	M2M_INFO("Firmware ver   : %u.%u.%u Svnrev %u\n", strtmp.u8FirmwareMajor, strtmp.u8FirmwareMinor, strtmp.u8FirmwarePatch,strtmp.u16FirmwareSvnNum);
	M2M_INFO("Firmware Build %s Time %s\n",strtmp.BuildDate,strtmp.BuildTime);
	M2M_INFO("Firmware Min driver ver : %u.%u.%u\n", strtmp.u8DriverMajor, strtmp.u8DriverMinor, strtmp.u8DriverPatch);
	M2M_INFO("Driver ver: %u.%u.%u \n", M2M_RELEASE_VERSION_MAJOR_NO, M2M_RELEASE_VERSION_MINOR_NO, M2M_RELEASE_VERSION_PATCH_NO);
	M2M_INFO("Driver built at %s\t%s\n",__DATE__,__TIME__);
	if(M2M_ERR_FW_VER_MISMATCH == ret)
	{
		M2M_ERR("Mismatch Firmware Version\n");
	}

    goto _EXIT0;

_EXIT1:
    gu8WifiState = WIFI_STATE_DEINIT;
    nm_drv_deinit(NULL);
_EXIT0:

    return ret;
}

sint8 m2m_wifi_init(tstrWifiInitParam *pWifiInitParam)
{
    sint8 ret = M2M_SUCCESS;

    ret = m2m_wifi_init_hold();
    if(ret == M2M_SUCCESS)
    {
        ret = m2m_wifi_init_start(pWifiInitParam);
    }
    return ret;
}

sint8  m2m_wifi_deinit(void *arg)
{
    gu8WifiState = WIFI_STATE_DEINIT;
    hif_deinit(NULL);

    nm_drv_deinit(NULL);

    return M2M_SUCCESS;
}

void m2m_wifi_yield(void)
{
    hif_yield();
}

sint8 m2m_wifi_handle_events(void *arg)
{
    return hif_handle_isr();
}

sint8 m2m_wifi_delete_sc(char *pcSsid, uint8 u8SsidLen)
{
    tstrM2mWifiApId strApId;
    m2m_memset((uint8 *)&strApId, 0, sizeof(strApId));
#if 0
    if(pcSsid != NULL)
    {
        if(u8SsidLen >= M2M_MAX_SSID_LEN)
            return M2M_ERR_INVALID_ARG;
        strApId.au8SSID[0] = u8SsidLen;
        m2m_memcpy(&strApId.au8SSID[1], (uint8 *)pcSsid, u8SsidLen);
    }
    else
#endif
        strApId.au8SSID[0] = 0xFF;  // Special value used to cause fw to delete all entries.
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQRSP_DELETE_APID, (uint8 *)&strApId, sizeof(tstrM2mWifiApId), NULL, 0, 0);
}

sint8 m2m_wifi_default_connect(void)
{
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_DEFAULT_CONNECT, NULL, 0, NULL, 0, 0);
}

/*************************************************************************************************/
/*                                WIFI CONNECT INTERNAL FUNCTIONS                                */
/*************************************************************************************************/
static sint8 m2m_wifi_connect_prepare_msg(
    tenuCredStoreOption enuCredStoreOption,
    tenuM2mSecType      enuAuthType,
    uint16              u16AuthSize,
    tstrNetworkId       *pstrNetworkId,
    tstrM2mWifiConnHdr  *pstrWifiConn
)
{
    sint8   ret = M2M_ERR_FAIL;
    uint16  u16CredSize = sizeof(tstrM2mConnCredCmn) + u16AuthSize;

    /* Check application params. */
    if(
        (pstrNetworkId == NULL)
        || (pstrNetworkId->pu8Ssid == NULL)
        || (pstrNetworkId->u8SsidLen >= M2M_MAX_SSID_LEN)
    )
        goto INVALID_ARG;

    if(pstrWifiConn != NULL)
    {
        tstrM2mConnCredHdr  *pstrHdr = &pstrWifiConn->strConnCredHdr;
        tstrM2mConnCredCmn  *pstrCmn = &pstrWifiConn->strConnCredCmn;

        m2m_memset((uint8 *)pstrWifiConn, 0, sizeof(tstrM2mWifiConnHdr));

        pstrHdr->u16CredSize = u16CredSize;
        switch(enuCredStoreOption)
        {
        case WIFI_CRED_SAVE_ENCRYPTED:
            pstrHdr->u8CredStoreFlags |= M2M_CRED_ENCRYPT_FLAG;
        // intentional fall through...
        case WIFI_CRED_SAVE_UNENCRYPTED:
            pstrHdr->u8CredStoreFlags |= M2M_CRED_STORE_FLAG;
        // intentional fall through...
        case WIFI_CRED_DONTSAVE:
            break;
        default:
            goto INVALID_ARG;
        }

        if(pstrNetworkId->enuChannel == M2M_WIFI_CH_ALL)
            pstrHdr->u8Channel = (uint8)(pstrNetworkId->enuChannel);
        else if((pstrNetworkId->enuChannel <= M2M_WIFI_CH_14) && (pstrNetworkId->enuChannel >= M2M_WIFI_CH_1))
            pstrHdr->u8Channel = (uint8)(pstrNetworkId->enuChannel) - 1;
        else
            goto INVALID_ARG;

        if((enuAuthType == M2M_WIFI_SEC_INVALID) || (enuAuthType >= M2M_WIFI_NUM_AUTH_TYPES))
            goto INVALID_ARG;
        pstrCmn->u8AuthType = (uint8)enuAuthType;

        pstrCmn->u8SsidLen = pstrNetworkId->u8SsidLen;
        m2m_memcpy(pstrCmn->au8Ssid, pstrNetworkId->pu8Ssid, pstrNetworkId->u8SsidLen);
        if(pstrNetworkId->pu8Bssid != NULL)
        {
            pstrCmn->u8Options = M2M_WIFI_CONN_BSSID_FLAG;
            m2m_memcpy(pstrCmn->au8Bssid, pstrNetworkId->pu8Bssid, M2M_MAC_ADDRES_LEN);
        }
        /* Everything is ok, set return value. */
        ret = M2M_SUCCESS;
    }
    return ret;
INVALID_ARG:
    return M2M_ERR_INVALID_ARG;
}

/*************************************************************************************************/
/*                                        WIFI CONNECT APIS                                      */
/*************************************************************************************************/
sint8 m2m_wifi_connect_open(
    tenuCredStoreOption enuCredStoreOption,
    tstrNetworkId      *pstrNetworkId
)
{
    sint8               ret = M2M_ERR_INVALID_ARG;
    tstrM2mWifiConnHdr  strConnHdr;

    ret = m2m_wifi_connect_prepare_msg(enuCredStoreOption, M2M_WIFI_SEC_OPEN, 0, pstrNetworkId, &strConnHdr);
    if(ret == M2M_SUCCESS)
    {
        ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CONN,
                       (uint8 *)&strConnHdr, sizeof(strConnHdr),
                       NULL, 0, 0);
    }
    return ret;
}

sint8 m2m_wifi_connect_wep(
    tenuCredStoreOption enuCredStoreOption,
    tstrNetworkId       *pstrNetworkId,
    tstrAuthWep         *pstrAuthWep
)
{
    /* As of 19.7.5 the WEP protocol is deprecated */
    return M2M_ERR_INVALID;
}

sint8 m2m_wifi_connect_psk(
    tenuCredStoreOption enuCredStoreOption,
    tstrNetworkId       *pstrNetworkId,
    tstrAuthPsk         *pstrAuthPsk
)
{
    sint8   ret = M2M_ERR_INVALID_ARG;

    if(pstrAuthPsk != NULL)
    {
        tstrM2mWifiConnHdr  strConnHdr;

        ret = m2m_wifi_connect_prepare_msg(enuCredStoreOption,
                                           M2M_WIFI_SEC_WPA_PSK,
                                           sizeof(tstrM2mWifiPsk),
                                           pstrNetworkId,
                                           &strConnHdr);

        if(ret == M2M_SUCCESS)
        {
            tstrM2mWifiPsk  *pstrPsk = (tstrM2mWifiPsk *)malloc(sizeof(tstrM2mWifiPsk));
            if(pstrPsk != NULL)
            {
                m2m_memset((uint8 *)pstrPsk, 0, sizeof(tstrM2mWifiPsk));
                if(pstrAuthPsk->pu8Psk != NULL)
                {
                    if(pstrAuthPsk->pu8Passphrase != NULL)
                        ret = M2M_ERR_INVALID_ARG;
                    else
                    {
                        pstrPsk->u8PassphraseLen = M2M_MAX_PSK_LEN-1;
                        /* Use hexstr_2_bytes to verify pu8Psk input. */
                        if(M2M_SUCCESS != hexstr_2_bytes(pstrPsk->au8Passphrase, pstrAuthPsk->pu8Psk, pstrPsk->u8PassphraseLen/2))
                            ret = M2M_ERR_INVALID_ARG;
                        m2m_memcpy(pstrPsk->au8Passphrase, pstrAuthPsk->pu8Psk, pstrPsk->u8PassphraseLen);
                    }
                }
                else if(pstrAuthPsk->pu8Passphrase != NULL)
                {
                    if(pstrAuthPsk->u8PassphraseLen > M2M_MAX_PSK_LEN-1)
                        ret = M2M_ERR_INVALID_ARG;
                    else
                    {
                        pstrPsk->u8PassphraseLen = pstrAuthPsk->u8PassphraseLen;
                        m2m_memcpy(pstrPsk->au8Passphrase, pstrAuthPsk->pu8Passphrase, pstrPsk->u8PassphraseLen);
                    }
                }
                else
                    ret = M2M_ERR_INVALID_ARG;
                if(ret == M2M_SUCCESS)
                {
                    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CONN | M2M_REQ_DATA_PKT,
                                   (uint8 *)&strConnHdr, sizeof(tstrM2mWifiConnHdr),
                                   (uint8 *)pstrPsk, sizeof(tstrM2mWifiPsk), sizeof(tstrM2mWifiConnHdr));
                }
                free(pstrPsk);
            }
            else
                ret = M2M_ERR_MEM_ALLOC;
        }
    }
    return ret;
}

sint8 m2m_wifi_1x_set_option(tenu1xOption enuOptionName, const void *pOptionValue, size_t OptionLen)
{
    if((pOptionValue == NULL) && (OptionLen > 0))
        return M2M_ERR_INVALID_ARG;
    switch(enuOptionName)
    {
    case WIFI_1X_BYPASS_SERVER_AUTH:
        if(OptionLen != sizeof(int))
            return M2M_ERR_INVALID_ARG;
        switch(*(int*)pOptionValue)
        {
        case 1:
            gu321xTlsHsFlags &= ~WIFI_1X_TLS_HS_FLAGS_PEER_AUTH;
            break;
        case 0:
            gu321xTlsHsFlags |= WIFI_1X_TLS_HS_FLAGS_PEER_AUTH;
            break;
        default:
            return M2M_ERR_INVALID_ARG;
        }
        break;
    case WIFI_1X_TIME_VERIF_MODE:
        if(OptionLen != sizeof(tenuSslCertExpSettings))
            return M2M_ERR_INVALID_ARG;
        switch(*(tenuSslCertExpSettings*)pOptionValue)
        {
        case SSL_CERT_EXP_CHECK_DISABLE:
            gu321xTlsHsFlags &= ~WIFI_1X_TLS_HS_FLAGS_PEER_CERTTIMECHECK;
            gu321xTlsHsFlags &= ~WIFI_1X_TLS_HS_FLAGS_REQUIRE_TIME;
            break;
        case SSL_CERT_EXP_CHECK_ENABLE:
            gu321xTlsHsFlags |= WIFI_1X_TLS_HS_FLAGS_PEER_CERTTIMECHECK;
            gu321xTlsHsFlags |= WIFI_1X_TLS_HS_FLAGS_REQUIRE_TIME;
            break;
        case SSL_CERT_EXP_CHECK_EN_IF_SYS_TIME:
            gu321xTlsHsFlags |= WIFI_1X_TLS_HS_FLAGS_PEER_CERTTIMECHECK;
            gu321xTlsHsFlags &= ~WIFI_1X_TLS_HS_FLAGS_REQUIRE_TIME;
            break;
        default:
            return M2M_ERR_INVALID_ARG;
        }
        break;
    case WIFI_1X_SESSION_CACHING:
        if(OptionLen != sizeof(int))
            return M2M_ERR_INVALID_ARG;
        switch(*(int*)pOptionValue)
        {
        case 1:
            gu321xTlsHsFlags |= WIFI_1X_TLS_HS_FLAGS_SESSION_CACHING;
            break;
        case 0:
            gu321xTlsHsFlags &= ~WIFI_1X_TLS_HS_FLAGS_SESSION_CACHING;
            break;
        default:
            return M2M_ERR_INVALID_ARG;
        }
        break;
    case WIFI_1X_SPECIFIC_ROOTCERT:
        switch(OptionLen)
        {
        case 20:
            gu321xTlsHsFlags |= WIFI_1X_TLS_HS_FLAGS_SPECIFY_ROOTCERT;
            m2m_memcpy(gau81xRootSha1, (uint8*)pOptionValue, sizeof(gau81xRootSha1));
            break;
        case 0:
            gu321xTlsHsFlags &= ~WIFI_1X_TLS_HS_FLAGS_SPECIFY_ROOTCERT;
            m2m_memset(gau81xRootSha1, 0, sizeof(gau81xRootSha1));
            break;
        default:
            return M2M_ERR_INVALID_ARG;
        }
        break;
    default:
        return M2M_ERR_INVALID_ARG;
    }
    return M2M_SUCCESS;
}

sint8 m2m_wifi_1x_get_option(tenu1xOption enuOptionName, void *pOptionValue, size_t *pOptionLen)
{
    if(pOptionValue == NULL)
        return M2M_ERR_INVALID_ARG;
    switch(enuOptionName)
    {
    case WIFI_1X_BYPASS_SERVER_AUTH:
        if(*pOptionLen < sizeof(int))
            return M2M_ERR_INVALID_ARG;
        *pOptionLen = sizeof(int);
        *(int*)pOptionValue = (gu321xTlsHsFlags & WIFI_1X_TLS_HS_FLAGS_PEER_AUTH) ? 0 : 1;
        break;
    case WIFI_1X_TIME_VERIF_MODE:
        if(*pOptionLen < sizeof(tenuSslCertExpSettings))
            return M2M_ERR_INVALID_ARG;
        *pOptionLen = sizeof(tenuSslCertExpSettings);
        if(!(gu321xTlsHsFlags & WIFI_1X_TLS_HS_FLAGS_PEER_CERTTIMECHECK))
            *(tenuSslCertExpSettings*)pOptionValue = SSL_CERT_EXP_CHECK_DISABLE;
        else if(gu321xTlsHsFlags & WIFI_1X_TLS_HS_FLAGS_REQUIRE_TIME)
            *(tenuSslCertExpSettings*)pOptionValue = SSL_CERT_EXP_CHECK_ENABLE;
        else
            *(tenuSslCertExpSettings*)pOptionValue = SSL_CERT_EXP_CHECK_EN_IF_SYS_TIME;
        break;
    case WIFI_1X_SESSION_CACHING:
        if(*pOptionLen < sizeof(int))
            return M2M_ERR_INVALID_ARG;
        *pOptionLen = sizeof(int);
        *(int*)pOptionValue = (gu321xTlsHsFlags & WIFI_1X_TLS_HS_FLAGS_SESSION_CACHING) ? 1 : 0;
        break;
    case WIFI_1X_SPECIFIC_ROOTCERT:
        if(gu321xTlsHsFlags & WIFI_1X_TLS_HS_FLAGS_SPECIFY_ROOTCERT)
        {
            if(*pOptionLen < sizeof(gau81xRootSha1))
                return M2M_ERR_INVALID_ARG;
            *pOptionLen = sizeof(gau81xRootSha1);
            m2m_memcpy((uint8*)pOptionValue, gau81xRootSha1, sizeof(gau81xRootSha1));
        }
        else
            *pOptionLen = 0;
        break;
    default:
        return M2M_ERR_INVALID_ARG;
    }
    return M2M_SUCCESS;
}

sint8 m2m_wifi_connect_1x_mschap2(
    tenuCredStoreOption enuCredStoreOption,
    tstrNetworkId       *pstrNetworkId,
    tstrAuth1xMschap2   *pstrAuth1xMschap2
)
{
    sint8 ret = M2M_ERR_INVALID_ARG;
    if(pstrAuth1xMschap2 != NULL)
    {
        if(pstrAuth1xMschap2->pu8Domain == NULL)
            pstrAuth1xMschap2->u16DomainLen = 0;
        if(
            (pstrAuth1xMschap2->pu8UserName != NULL)
            && (pstrAuth1xMschap2->pu8Password != NULL)
            && ((uint32)(pstrAuth1xMschap2->u16DomainLen) + pstrAuth1xMschap2->u16UserNameLen <= M2M_AUTH_1X_USER_LEN_MAX)
            && (pstrAuth1xMschap2->u16PasswordLen <= M2M_AUTH_1X_PASSWORD_LEN_MAX)
        )
        {
            tstrM2mWifiConnHdr  strConnHdr;
            uint16              u16AuthSize =   sizeof(tstrM2mWifi1xHdr) +
                                                pstrAuth1xMschap2->u16DomainLen +
                                                pstrAuth1xMschap2->u16UserNameLen +
                                                pstrAuth1xMschap2->u16PasswordLen;

            ret = m2m_wifi_connect_prepare_msg(enuCredStoreOption,
                                               M2M_WIFI_SEC_802_1X,
                                               u16AuthSize,
                                               pstrNetworkId,
                                               &strConnHdr);

            if(ret == M2M_SUCCESS)
            {
                tstrM2mWifi1xHdr    *pstr1xHdr = (tstrM2mWifi1xHdr *)malloc(u16AuthSize);
                if(pstr1xHdr != NULL)
                {
                    uint8   *pu8AuthPtr = pstr1xHdr->au81xAuthDetails;
                    m2m_memset((uint8 *)pstr1xHdr, 0, u16AuthSize);

                    pstr1xHdr->u8Flags = M2M_802_1X_MSCHAP2_FLAG;
                    if(pstrAuth1xMschap2->bUnencryptedUserName == true)
                        pstr1xHdr->u8Flags |= M2M_802_1X_UNENCRYPTED_USERNAME_FLAG;
                    if(pstrAuth1xMschap2->bPrependDomain == true)
                        pstr1xHdr->u8Flags |= M2M_802_1X_PREPEND_DOMAIN_FLAG;

                    pstr1xHdr->u8HdrLength = sizeof(tstrM2mWifi1xHdr);
                    pstr1xHdr->u32TlsHsFlags = gu321xTlsHsFlags;
                    m2m_memcpy(pstr1xHdr->au8TlsSpecificRootNameSha1, gau81xRootSha1, sizeof(gau81xRootSha1));

                    pstr1xHdr->u8DomainLength = 0;
                    if(pstrAuth1xMschap2->pu8Domain != NULL)
                    {
                        pstr1xHdr->u8DomainLength = (uint8)(pstrAuth1xMschap2->u16DomainLen);
                        m2m_memcpy(pu8AuthPtr, pstrAuth1xMschap2->pu8Domain, pstr1xHdr->u8DomainLength);
                        pu8AuthPtr += pstr1xHdr->u8DomainLength;
                    }

                    pstr1xHdr->u8UserNameLength = (pstrAuth1xMschap2->u16UserNameLen);
                    m2m_memcpy(pu8AuthPtr, pstrAuth1xMschap2->pu8UserName, pstr1xHdr->u8UserNameLength);
                    pu8AuthPtr += pstr1xHdr->u8UserNameLength;

                    pstr1xHdr->u16PrivateKeyOffset = pu8AuthPtr - pstr1xHdr->au81xAuthDetails;
                    pstr1xHdr->u16PrivateKeyLength = pstrAuth1xMschap2->u16PasswordLen;
                    m2m_memcpy(pu8AuthPtr, pstrAuth1xMschap2->pu8Password, pstr1xHdr->u16PrivateKeyLength);

                    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CONN | M2M_REQ_DATA_PKT,
                                   (uint8 *)&strConnHdr, sizeof(tstrM2mWifiConnHdr),
                                   (uint8 *)pstr1xHdr, u16AuthSize,
                                   sizeof(tstrM2mWifiConnHdr));
                    free(pstr1xHdr);
                }
                else
                    ret = M2M_ERR_MEM_ALLOC;
            }
        }
    }
    return ret;
}

sint8 m2m_wifi_connect_1x_tls(
    tenuCredStoreOption enuCredStoreOption,
    tstrNetworkId       *pstrNetworkId,
    tstrAuth1xTls       *pstrAuth1xTls
)
{
    sint8 ret = M2M_ERR_INVALID_ARG;
    if(pstrAuth1xTls != NULL)
    {
        if(pstrAuth1xTls->pu8Domain == NULL)
            pstrAuth1xTls->u16DomainLen = 0;
        if(
            (pstrAuth1xTls->pu8UserName != NULL)
            && (pstrAuth1xTls->pu8PrivateKey_Mod != NULL)
            && (pstrAuth1xTls->pu8PrivateKey_Exp != NULL)
            && (pstrAuth1xTls->pu8Certificate != NULL)
            && ((uint32)(pstrAuth1xTls->u16DomainLen) + pstrAuth1xTls->u16UserNameLen <= M2M_AUTH_1X_USER_LEN_MAX)
            && (pstrAuth1xTls->u16PrivateKeyLen <= M2M_AUTH_1X_PRIVATEKEY_LEN_MAX)
            && (pstrAuth1xTls->u16CertificateLen <= M2M_AUTH_1X_CERT_LEN_MAX)
        )
        {
            tstrM2mWifiConnHdr  strConnHdr;
            uint16              u16AuthSize =   sizeof(tstrM2mWifi1xHdr) +
                                                pstrAuth1xTls->u16DomainLen +
                                                pstrAuth1xTls->u16UserNameLen +
                                                (2 * pstrAuth1xTls->u16PrivateKeyLen) +
                                                pstrAuth1xTls->u16CertificateLen;

            ret = m2m_wifi_connect_prepare_msg(enuCredStoreOption,
                                               M2M_WIFI_SEC_802_1X,
                                               u16AuthSize,
                                               pstrNetworkId,
                                               &strConnHdr);

            if(ret == M2M_SUCCESS)
            {
                uint16              u16Payload1Size = u16AuthSize - pstrAuth1xTls->u16CertificateLen;
                tstrM2mWifi1xHdr    *pstr1xHdr = (tstrM2mWifi1xHdr *)malloc(u16Payload1Size);
                if(pstr1xHdr != NULL)
                {
                    tstrM2mWifiAuthInfoHdr strInfoHdr = {};

                    uint8   *pu8AuthPtr = pstr1xHdr->au81xAuthDetails;
                    m2m_memset((uint8 *)pstr1xHdr, 0, u16Payload1Size);

                    pstr1xHdr->u8Flags = M2M_802_1X_TLS_FLAG;
                    if(pstrAuth1xTls->bUnencryptedUserName == true)
                        pstr1xHdr->u8Flags |= M2M_802_1X_UNENCRYPTED_USERNAME_FLAG;
                    if(pstrAuth1xTls->bPrependDomain == true)
                        pstr1xHdr->u8Flags |= M2M_802_1X_PREPEND_DOMAIN_FLAG;

                    pstr1xHdr->u8HdrLength = sizeof(tstrM2mWifi1xHdr);
                    pstr1xHdr->u32TlsHsFlags = gu321xTlsHsFlags;
                    m2m_memcpy(pstr1xHdr->au8TlsSpecificRootNameSha1, gau81xRootSha1, sizeof(gau81xRootSha1));

                    pstr1xHdr->u8DomainLength = 0;
                    if(pstrAuth1xTls->pu8Domain != NULL)
                    {
                        pstr1xHdr->u8DomainLength = (uint8)(pstrAuth1xTls->u16DomainLen);
                        m2m_memcpy(pu8AuthPtr, pstrAuth1xTls->pu8Domain, pstr1xHdr->u8DomainLength);
                        pu8AuthPtr += pstr1xHdr->u8DomainLength;
                    }

                    pstr1xHdr->u8UserNameLength = (pstrAuth1xTls->u16UserNameLen);
                    m2m_memcpy(pu8AuthPtr, pstrAuth1xTls->pu8UserName, pstr1xHdr->u8UserNameLength);
                    pu8AuthPtr += pstr1xHdr->u8UserNameLength;

                    pstr1xHdr->u16PrivateKeyOffset = pu8AuthPtr - pstr1xHdr->au81xAuthDetails;
                    pstr1xHdr->u16PrivateKeyLength = pstrAuth1xTls->u16PrivateKeyLen;
                    m2m_memcpy(pu8AuthPtr, pstrAuth1xTls->pu8PrivateKey_Mod, pstr1xHdr->u16PrivateKeyLength);
                    pu8AuthPtr += pstr1xHdr->u16PrivateKeyLength;
                    m2m_memcpy(pu8AuthPtr, pstrAuth1xTls->pu8PrivateKey_Exp, pstr1xHdr->u16PrivateKeyLength);
                    pu8AuthPtr += pstr1xHdr->u16PrivateKeyLength;

                    pstr1xHdr->u16CertificateOffset = pu8AuthPtr - pstr1xHdr->au81xAuthDetails;
                    pstr1xHdr->u16CertificateLength = pstrAuth1xTls->u16CertificateLen;

                    strInfoHdr.u8Type = M2M_802_1X_TLS_CLIENT_CERTIFICATE;
                    strInfoHdr.u16InfoPos = pstr1xHdr->u16CertificateOffset;
                    strInfoHdr.u16InfoLen = pstr1xHdr->u16CertificateLength;
                    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_IND_CONN_PARAM | M2M_REQ_DATA_PKT,
                                   (uint8 *)&strInfoHdr, sizeof(tstrM2mWifiAuthInfoHdr),
                                   pstrAuth1xTls->pu8Certificate, pstrAuth1xTls->u16CertificateLen,
                                   sizeof(tstrM2mWifiAuthInfoHdr));

                    if(ret == M2M_SUCCESS)
                    {
                        ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CONN | M2M_REQ_DATA_PKT,
                                       (uint8 *)&strConnHdr, sizeof(tstrM2mWifiConnHdr),
                                       (uint8 *)pstr1xHdr, u16Payload1Size,
                                       sizeof(tstrM2mWifiConnHdr));
                    }
                    free(pstr1xHdr);
                }
                else
                    ret = M2M_ERR_MEM_ALLOC;
            }
        }
    }
    return ret;
}

sint8 m2m_wifi_connect(char *pcSsid, uint8 u8SsidLen, uint8 u8SecType, void *pvAuthInfo, uint16 u16Ch)
{
    return m2m_wifi_connect_sc(pcSsid, u8SsidLen, u8SecType, pvAuthInfo,  u16Ch, 0);
}

sint8 m2m_wifi_connect_sc(char *pcSsid, uint8 u8SsidLen, uint8 u8SecType, void *pvAuthInfo, uint16 u16Ch, uint8 u8NoSaveCred)
{
    sint8               s8Ret              = M2M_ERR_INVALID_ARG;
    tstrNetworkId       strNetworkId       = {NULL, (uint8 *)pcSsid, u8SsidLen, (tenuM2mScanCh)u16Ch};
    tenuCredStoreOption enuCredStoreOption = u8NoSaveCred ? WIFI_CRED_DONTSAVE : WIFI_CRED_SAVE_ENCRYPTED;

    /* This API does not support SSIDs which contain '\0'. If there is a '\0' character within the
     * first u8SsidLen characters, then assume that the input u8SsidLen was incorrect - set length
     * to strlen(pcSsid) and continue. This is to avoid a change from the behaviour of previously
     * released drivers. */
    if(u8SsidLen < M2M_MAX_SSID_LEN)
        while(u8SsidLen--)
            if(strNetworkId.pu8Ssid[u8SsidLen] == 0)
                strNetworkId.u8SsidLen = u8SsidLen;

    switch((tenuM2mSecType)u8SecType)
    {
    case M2M_WIFI_SEC_OPEN:
        s8Ret = m2m_wifi_connect_open(enuCredStoreOption, &strNetworkId);
        break;
    case M2M_WIFI_SEC_WPA_PSK:
        if(pvAuthInfo != NULL)
        {
            tstrAuthPsk strAuthPsk = {NULL, NULL, 0};
            uint16      len        = m2m_strlen((uint8 *)pvAuthInfo);

            if(len == M2M_MAX_PSK_LEN-1)
            {
                strAuthPsk.pu8Psk = (uint8 *)pvAuthInfo;
            }
            else
            {
                strAuthPsk.pu8Passphrase   = (uint8 *)pvAuthInfo;
                strAuthPsk.u8PassphraseLen = len;
            }
            s8Ret = m2m_wifi_connect_psk(enuCredStoreOption, &strNetworkId, &strAuthPsk);
        }
        break;
    case M2M_WIFI_SEC_WEP:
        if(pvAuthInfo != NULL)
        {
            tstrM2mWifiWepParams    *pstrWepParams = (tstrM2mWifiWepParams *)pvAuthInfo;
            tstrAuthWep             strAuthWep     = {pstrWepParams->au8WepKey, (uint8)(pstrWepParams->u8KeySz - 1), pstrWepParams->u8KeyIndx};

            s8Ret = m2m_wifi_connect_wep(enuCredStoreOption, &strNetworkId, &strAuthWep);
        }
        break;
    case M2M_WIFI_SEC_802_1X:
        if(pvAuthInfo != NULL)
        {
            tstr1xAuthCredentials   *pstr1xParams    = (tstr1xAuthCredentials *)pvAuthInfo;
            tstrAuth1xMschap2       strAuth1xMschap2 = {NULL,
                                                        pstr1xParams->au8UserName,
                                                        pstr1xParams->au8Passwd,
                                                        0,
                                                        m2m_strlen(pstr1xParams->au8UserName),
                                                        m2m_strlen(pstr1xParams->au8Passwd),
                                                        false,
                                                        false};

            s8Ret = m2m_wifi_connect_1x_mschap2(enuCredStoreOption, &strNetworkId, &strAuth1xMschap2);
        }
        break;
    default:
        break;
    }
    return s8Ret;
}

sint8 m2m_wifi_disconnect(void)
{
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_DISCONNECT, NULL, 0, NULL, 0, 0);
}

sint8 m2m_wifi_set_mac_address(uint8 au8MacAddress[6])
{
    tstrM2mSetMacAddress strTmp;
    m2m_memcpy((uint8 *) strTmp.au8Mac, (uint8 *) au8MacAddress, 6);
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_MAC_ADDRESS,
                    (uint8 *) &strTmp, sizeof(tstrM2mSetMacAddress), NULL, 0, 0);
}

sint8 m2m_wifi_set_static_ip(tstrM2MIPConfig *pstrStaticIPConf)
{
    pstrStaticIPConf->u32DNS = NM_BSP_B_L_32(pstrStaticIPConf->u32DNS);
    pstrStaticIPConf->u32Gateway = NM_BSP_B_L_32(pstrStaticIPConf->u32Gateway);
    pstrStaticIPConf->u32StaticIP = NM_BSP_B_L_32(
                                        pstrStaticIPConf->u32StaticIP);
    pstrStaticIPConf->u32SubnetMask = NM_BSP_B_L_32(
                                          pstrStaticIPConf->u32SubnetMask);
    return hif_send(M2M_REQ_GROUP_IP, M2M_IP_REQ_STATIC_IP_CONF,
                    (uint8 *) pstrStaticIPConf, sizeof(tstrM2MIPConfig), NULL, 0, 0);
}

sint8 m2m_wifi_request_dhcp_client(void)
{
    /*legacy API should be removed */
    return 0;
}

sint8 m2m_wifi_request_dhcp_server(uint8 *addr)
{
    /*legacy API should be removed */
    return 0;
}

/*!
@fn         NMI_API sint8 m2m_wifi_set_lsn_int(tstrM2mLsnInt * pstrM2mLsnInt);
@brief      Set the Wi-Fi listen interval for power save operation. It is represented in units
            of AP Beacon periods.
@param [in] pstrM2mLsnInt
            Structure holding the listen interval configurations.
@return     The function SHALL return 0 for success and a negative value otherwise.
@sa         tstrM2mLsnInt , m2m_wifi_set_sleep_mode
@pre        m2m_wifi_set_sleep_mode shall be called first
@warning    The Function called once after initialization.
*/
sint8 m2m_wifi_enable_dhcp(uint8  u8DhcpEn)
{
    uint8 u8Req;
    u8Req = u8DhcpEn ? M2M_IP_REQ_ENABLE_DHCP : M2M_IP_REQ_DISABLE_DHCP;
    return hif_send(M2M_REQ_GROUP_IP, u8Req, NULL, 0, NULL, 0, 0);
}

sint8 m2m_wifi_set_lsn_int(tstrM2mLsnInt *pstrM2mLsnInt)
{
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_LSN_INT, (uint8 *)pstrM2mLsnInt, sizeof(tstrM2mLsnInt), NULL, 0, 0);
}

sint8 m2m_wifi_set_cust_InfoElement(uint8 *pau8M2mCustInfoElement)
{
    sint8  ret = M2M_ERR_FAIL;
    if(pau8M2mCustInfoElement != NULL)
    {
        if((pau8M2mCustInfoElement[0] + 1) < M2M_CUST_IE_LEN_MAX)
        {
            ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CUST_INFO_ELEMENT|M2M_REQ_DATA_PKT, (uint8 *)pau8M2mCustInfoElement, pau8M2mCustInfoElement[0]+1, NULL, 0, 0);
        }
    }
    return ret;
}

sint8 m2m_wifi_set_scan_options(tstrM2MScanOption *ptstrM2MScanOption)
{
    sint8   s8Ret = M2M_ERR_FAIL;
    if(m2m_validate_scan_options(ptstrM2MScanOption) == M2M_SUCCESS)
    {
        s8Ret =  hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_SCAN_OPTION, (uint8 *)ptstrM2MScanOption, sizeof(tstrM2MScanOption), NULL, 0, 0);
    }
    return s8Ret;
}

sint8 m2m_wifi_set_stop_scan_on_first(uint8 u8StopScanOption)
{
    sint8   s8Ret = M2M_ERR_FAIL;

    tstrM2MStopScanOption StopScanOption = {};

    if(1 >= u8StopScanOption)
    {
        StopScanOption.u8StopOnFirstResult = u8StopScanOption;

        s8Ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_STOP_SCAN_OPTION, (uint8 *)&StopScanOption, sizeof(tstrM2MStopScanOption), NULL, 0, 0);

        M2M_INFO("Scan will %s stop on first result.\n", StopScanOption.u8StopOnFirstResult ? "" : "NOT");
    }
    else
    {
        s8Ret = M2M_ERR_INVALID_ARG;
    }

    return s8Ret;
}

sint8 m2m_wifi_set_scan_region(uint16  ScanRegion)
{
    sint8   s8Ret = M2M_ERR_FAIL;
    tstrM2MScanRegion strScanRegion;
    strScanRegion.u16ScanRegion = ScanRegion;
    s8Ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_SCAN_REGION, (uint8 *)&strScanRegion, sizeof(tstrM2MScanRegion), NULL, 0, 0);
    return s8Ret;
}
sint8 m2m_wifi_request_scan(uint8 ch)
{
    sint8   s8Ret = M2M_SUCCESS;

    if(!gu8scanInProgress)
    {
        if(((ch >= M2M_WIFI_CH_1) && (ch <= M2M_WIFI_CH_14)) || (ch == M2M_WIFI_CH_ALL))
        {
            tstrM2MScan strtmp;
            strtmp.u8ChNum = ch;
            s8Ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SCAN, (uint8 *)&strtmp, sizeof(tstrM2MScan), NULL, 0, 0);
            if(s8Ret == M2M_SUCCESS)
            {
                gu8scanInProgress = 1;
            }
        }
        else
        {
            s8Ret = M2M_ERR_INVALID_ARG;
        }
    }
    else
    {
        s8Ret = M2M_ERR_SCAN_IN_PROGRESS;
    }
    return s8Ret;
}

sint8 m2m_wifi_request_scan_passive(uint8 ch, uint16 scan_time)
{
    sint8 s8Ret = M2M_SUCCESS;

    if(!gu8scanInProgress)
    {
        if(((ch >= M2M_WIFI_CH_1) && (ch <= M2M_WIFI_CH_14)) || (ch == M2M_WIFI_CH_ALL))
        {
            tstrM2MScan strtmp;
            strtmp.u8ChNum = ch;

            strtmp.u16PassiveScanTime = scan_time;

            s8Ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_PASSIVE_SCAN, (uint8 *)&strtmp, sizeof(tstrM2MScan), NULL, 0, 0);
            if(s8Ret == M2M_SUCCESS)
            {
                gu8scanInProgress = 1;
            }
        }
        else
        {
            s8Ret = M2M_ERR_INVALID_ARG;
        }
    }
    else
    {
        s8Ret = M2M_ERR_SCAN_IN_PROGRESS;
    }
    return s8Ret;
}

sint8 m2m_wifi_request_scan_ssid_list(uint8 ch, uint8 *u8Ssidlist)
{
    sint8   s8Ret = M2M_ERR_INVALID_ARG;

    if(!gu8scanInProgress)
    {
        if((((ch >= M2M_WIFI_CH_1) && (ch <= M2M_WIFI_CH_14)) || (ch == M2M_WIFI_CH_ALL))&&(u8Ssidlist != NULL))
        {
            tstrM2MScan strtmp;
            uint16 u16Lsize = 0;
            uint8 u8Apnum = u8Ssidlist[u16Lsize];
            if(u8Apnum <= MAX_HIDDEN_SITES)
            {
                u16Lsize++;
                while(u8Apnum)
                {
                    if(u8Ssidlist[u16Lsize] >= M2M_MAX_SSID_LEN) {
                        goto EXIT;
                    } else {
                        u16Lsize += u8Ssidlist[u16Lsize] + 1;
                        u8Apnum--;
                    }
                }
                strtmp.u8ChNum = ch;
                s8Ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SCAN_SSID_LIST|M2M_REQ_DATA_PKT, (uint8 *)&strtmp, sizeof(tstrM2MScan), u8Ssidlist, u16Lsize, sizeof(tstrM2MScan));
                if(s8Ret == M2M_SUCCESS)
                {
                    gu8scanInProgress = 1;
                }
            }
        }
    }
    else
    {
        s8Ret = M2M_ERR_SCAN_IN_PROGRESS;
    }
EXIT:
    return s8Ret;
}
sint8 m2m_wifi_wps(uint8 u8TriggerType, const char  *pcPinNumber)
{
    tstrM2MWPSConnect strtmp;

    /* Stop Scan if it is ongoing.
    */
    gu8scanInProgress = 0;
    strtmp.u8TriggerType = u8TriggerType;
    /*If WPS is using PIN METHOD*/
    if(u8TriggerType == WPS_PIN_TRIGGER)
        m2m_memcpy((uint8 *)strtmp.acPinNumber, (uint8 *) pcPinNumber, 8);
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_WPS, (uint8 *)&strtmp, sizeof(tstrM2MWPSConnect), NULL, 0, 0);
}
sint8 m2m_wifi_wps_disable(void)
{
    sint8 ret = M2M_SUCCESS;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_DISABLE_WPS, NULL, 0, NULL, 0, 0);
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_req_client_ctrl(uint8 cmd)
@brief      Send a command to the PS Client (An WINC board running the ps_firmware),
            if the PS client send any commands it will be received in wifi_cb @ref M2M_WIFI_RESP_CLIENT_INFO.
@param[in]  cmd
                Control command sent from PS Server to PS Client (command values defined by the application).
@return     The function returns @ref M2M_SUCCESS for success and a negative value otherwise.
@sa         m2m_wifi_req_server_init, M2M_WIFI_RESP_CLIENT_INFO
@pre        m2m_wifi_req_server_init should be called first
*/
sint8 m2m_wifi_req_client_ctrl(uint8 u8Cmd)
{
    sint8 ret = M2M_SUCCESS;
#ifdef _PS_SERVER_
    tstrM2Mservercmd    strCmd;
    strCmd.u8cmd = u8Cmd;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CLIENT_CTRL, (uint8 *)&strCmd, sizeof(tstrM2Mservercmd), NULL, 0, 0);
#else
    M2M_ERR("_PS_SERVER_ is not defined\n");
#endif
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_req_server_init(uint8 ch)
@brief      Initialize the PS Server, The WINC support non secure communication with another WINC,
            (SERVER/CLIENT) through one byte command (probe request and probe response) without any connection setup.
@param[in]  ch
                Server listening channel
@return     The function returns @ref M2M_SUCCESS for success and a negative value otherwise.
@sa         m2m_wifi_req_client_ctrl
@warning    The server mode can't be used with any other modes (STA/AP).
*/
sint8 m2m_wifi_req_server_init(uint8 ch)
{
    sint8 ret = M2M_SUCCESS;
#ifdef _PS_SERVER_
    tstrM2mServerInit strServer;
    strServer.u8Channel = ch;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SERVER_INIT, (uint8 *)&strServer, sizeof(tstrM2mServerInit), NULL, 0, 0);
#else
    M2M_ERR("_PS_SERVER_ is not defined\n");
#endif
    return ret;
}

sint8 m2m_wifi_p2p(uint8 u8Channel)
{
    sint8 ret = M2M_SUCCESS;
    if((u8Channel == M2M_WIFI_CH_1) || (u8Channel == M2M_WIFI_CH_6) || (u8Channel == M2M_WIFI_CH_11))
    {
        tstrM2MP2PConnect strtmp;
        strtmp.u8ListenChannel = u8Channel;
        ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_ENABLE_P2P, (uint8 *)&strtmp, sizeof(tstrM2MP2PConnect), NULL, 0, 0);
    }
    else
    {
        M2M_ERR("Listen channel should only be M2M_WIFI_CH_1/6/11 \n");
        ret = M2M_ERR_FAIL;
    }
    return ret;
}

sint8 m2m_wifi_p2p_disconnect(void)
{
    sint8 ret = M2M_SUCCESS;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_DISABLE_P2P, NULL, 0, NULL, 0, 0);
    return ret;
}

sint8 m2m_wifi_enable_ap(CONST tstrM2MAPConfig *pstrM2MAPConfig)
{
    tstrM2MAPModeConfig strM2MAPModeConfig;

    m2m_memcpy((uint8 *)&strM2MAPModeConfig.strApConfig, (uint8 *)pstrM2MAPConfig, sizeof(tstrM2MAPConfig));
    m2m_memcpy(strM2MAPModeConfig.strApConfigExt.au8DefRouterIP, pstrM2MAPConfig->au8DHCPServerIP, 4);
    m2m_memcpy(strM2MAPModeConfig.strApConfigExt.au8DNSServerIP, pstrM2MAPConfig->au8DHCPServerIP, 4);

    strM2MAPModeConfig.strApConfigExt.au8SubnetMask[0] = 0;

    return m2m_wifi_enable_ap_ext(&strM2MAPModeConfig);
}

sint8 m2m_wifi_enable_ap_ext(CONST tstrM2MAPModeConfig *pstrM2MAPModeConfig)
{
    sint8 ret = M2M_ERR_FAIL;
    if(M2M_SUCCESS == m2m_validate_ap_parameters(pstrM2MAPModeConfig))
    {
        ret = hif_send(M2M_REQ_GROUP_WIFI, (M2M_REQ_DATA_PKT|M2M_WIFI_REQ_ENABLE_AP), NULL, 0, (uint8 *)pstrM2MAPModeConfig, sizeof(tstrM2MAPModeConfig), 0);
    }
    return ret;
}

sint8 m2m_wifi_set_gains(tstrM2mWifiGainsParams *pstrM2mGain)
{
    sint8 ret = M2M_ERR_FAIL;
    if(pstrM2mGain != NULL)
    {
        ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_GAINS, (uint8 *)pstrM2mGain, sizeof(tstrM2mWifiGainsParams), NULL, 0, 0);
    }
    return ret;
}

sint8 m2m_wifi_disable_ap(void)
{
    sint8 ret = M2M_SUCCESS;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_DISABLE_AP, NULL, 0, NULL, 0, 0);
    return ret;
}

/*!
@fn          NMI_API sint8 m2m_wifi_req_curr_rssi(void);
@brief       Request the current RSSI for the current connected AP,
             the response received in wifi_cb M2M_WIFI_RESP_CURRENT_RSSI
@sa          M2M_WIFI_RESP_CURRENT_RSSI
@return      The function shall return M2M_SUCCESS for success and a negative value otherwise.
*/
sint8 m2m_wifi_req_curr_rssi(void)
{
    sint8 ret = M2M_SUCCESS;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CURRENT_RSSI, NULL, 0, NULL, 0, 0);
    return ret;
}
sint8 m2m_wifi_send_ethernet_pkt(uint8 *pu8Packet, uint16 u16PacketSize)
{
    sint8   s8Ret = -1;
    if((pu8Packet != NULL)&&(u16PacketSize>0))
    {
        tstrM2MWifiTxPacketInfo     strTxPkt;

        strTxPkt.u16PacketSize      = u16PacketSize;
        strTxPkt.u16HeaderLength    = M2M_ETHERNET_HDR_LEN;
        s8Ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SEND_ETHERNET_PACKET | M2M_REQ_DATA_PKT,
                         (uint8 *)&strTxPkt, sizeof(tstrM2MWifiTxPacketInfo), pu8Packet, u16PacketSize,  M2M_ETHERNET_HDR_OFFSET - M2M_HIF_HDR_OFFSET);
    }
    return s8Ret;
}

/*!
@fn          NMI_API sint8 m2m_wifi_get_otp_mac_address(uint8 *pu8MacAddr, uint8 * pu8IsValid);
@brief       Request the MAC address stored on the OTP (one time programmable) memory of the device.
             (the function is Blocking until response received)
@param [out] pu8MacAddr
             Output MAC address buffer of 6 bytes size. Valid only if *pu8Valid=1.
@param [out] pu8IsValid
             A output boolean value to indicate the validity of pu8MacAddr in OTP.
             Output zero if the OTP memory is not programmed, non-zero otherwise.
@return      The function shall return M2M_SUCCESS for success and a negative value otherwise.
@sa          m2m_wifi_get_mac_address
@pre         m2m_wifi_init required to call any WIFI/socket function
*/
sint8 m2m_wifi_get_otp_mac_address(uint8 *pu8MacAddr, uint8 *pu8IsValid)
{
    sint8 ret = M2M_SUCCESS;
    ret = hif_chip_wake();
    if(ret == M2M_SUCCESS)
    {
        ret = nmi_get_otp_mac_address(pu8MacAddr, pu8IsValid);
        if(ret == M2M_SUCCESS)
        {
            ret = hif_chip_sleep();
        }
    }
    return ret;
}

/*!
@fn          NMI_API sint8 m2m_wifi_get_mac_address(uint8 *pu8MacAddr)
@brief       Request the current MAC address of the device (the working mac address).
             (the function is Blocking until response received)
@param [out] pu8MacAddr
             Output MAC address buffer of 6 bytes size.
@return      The function shall return M2M_SUCCESS for success and a negative value otherwise.
@sa          m2m_wifi_get_otp_mac_address
@pre         m2m_wifi_init required to call any WIFI/socket function
*/
sint8 m2m_wifi_get_mac_address(uint8 *pu8MacAddr)
{
    sint8 ret = M2M_SUCCESS;
    ret = hif_chip_wake();
    if(ret == M2M_SUCCESS)
    {
        ret = nmi_get_mac_address(pu8MacAddr);
        if(ret == M2M_SUCCESS)
        {
            ret = hif_chip_sleep();
        }
    }
    return ret;
}

/*!
@fn          NMI_API sint8 m2m_wifi_req_scan_result(uint8 index);
@brief       Reads the AP information from the Scan Result list with the given index,
             the response received in wifi_cb M2M_WIFI_RESP_SCAN_RESULT,
             the response pointer should be casted with tstrM2mWifiscanResult structure
@param [in]  index
             Index for the requested result, the index range start from 0 till number of AP's found
@sa          tstrM2mWifiscanResult,m2m_wifi_get_num_ap_found,m2m_wifi_request_scan
@return      The function shall return M2M_SUCCESS for success and a negative value otherwise
@pre         m2m_wifi_request_scan need to be called first, then m2m_wifi_get_num_ap_found
             to get the number of AP's found
@warning     Function used only in STA mode only. the scan result updated only if scan request called,
             else it will be cashed in firmware for the host scan request result,
             which mean if large delay occur between the scan request and the scan result request,
             the result will not be up-to-date
*/
sint8 m2m_wifi_req_scan_result(uint8 index)
{
    sint8 ret = M2M_SUCCESS;
    tstrM2mReqScanResult strReqScanRlt;
    strReqScanRlt.u8Index = index;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SCAN_RESULT, (uint8 *) &strReqScanRlt, sizeof(tstrM2mReqScanResult), NULL, 0, 0);
    return ret;
}

/*!
@fn          NMI_API uint8 m2m_wifi_get_num_ap_found(void);
@brief       Reads the number of AP's found in the last Scan Request,
             The function read the number of AP's from global variable which updated in the
             wifi_cb in M2M_WIFI_RESP_SCAN_DONE.
@sa          m2m_wifi_request_scan
@return      Return the number of AP's found in the last Scan Request.
@pre         m2m_wifi_request_scan need to be called first
@warning     That function need to be called in the wifi_cb in M2M_WIFI_RESP_SCAN_DONE,
             calling that function in any other place will return undefined/undated numbers.
             Function used only in STA mode only.
*/
uint8 m2m_wifi_get_num_ap_found(void)
{
    return gu8ChNum;
}

/*!
@fn         NMI_API uint8 m2m_wifi_get_sleep_mode(void);
@brief      Get the current Power save mode.
@return     The current operating power saving mode.
@sa         tenuPowerSaveModes , m2m_wifi_set_sleep_mode
*/
uint8 m2m_wifi_get_sleep_mode(void)
{
    return hif_get_sleep_mode();
}

/*!
@fn         NMI_API sint8 m2m_wifi_set_sleep_mode(uint8 PsTyp, uint8 BcastEn);
@brief      Set the power saving mode for the WINC1500.
@param [in] PsTyp
            Desired power saving mode. Supported types are defined in tenuPowerSaveModes.
@param [in] BcastEn
            Broadcast reception enable flag.
            If it is 1, the WINC1500 must be awake each DTIM Beacon for receiving Broadcast traffic.
            If it is 0, the WINC1500 will not wakeup at the DTIM Beacon, but its wakeup depends only
            on the the configured Listen Interval.
@return     The function SHALL return 0 for success and a negative value otherwise.
@sa         tenuPowerSaveModes
@warning    The function called once after initialization.
*/
sint8 m2m_wifi_set_sleep_mode(uint8 PsTyp, uint8 BcastEn)
{
    sint8 ret = M2M_SUCCESS;
    tstrM2mPsType strPs;
    strPs.u8PsType = PsTyp;
    strPs.u8BcastEn = BcastEn;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SLEEP, (uint8 *) &strPs, sizeof(tstrM2mPsType), NULL, 0, 0);
    M2M_INFO("POWER SAVE %d\n", PsTyp);
    hif_set_sleep_mode(PsTyp);
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_request_sleep(void)
@brief      Request from WINC device to Sleep for specific time in the M2M_PS_MANUAL Power save mode (only).
@param [in] u32SlpReqTime
            Request Sleep in ms
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@sa         tenuPowerSaveModes , m2m_wifi_set_sleep_mode
@warning    the Function should be called in M2M_PS_MANUAL power save only
*/
sint8 m2m_wifi_request_sleep(uint32 u32SlpReqTime)
{
    sint8 ret = M2M_SUCCESS;
    uint8 psType;
    psType = hif_get_sleep_mode();
    if(psType == M2M_PS_MANUAL)
    {
        tstrM2mSlpReqTime strPs;
        strPs.u32SleepTime = u32SlpReqTime;
        ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_DOZE, (uint8 *) &strPs, sizeof(tstrM2mSlpReqTime), NULL, 0, 0);
    }
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_set_device_name(uint8 *pu8DeviceName, uint8 u8DeviceNameLength);
@brief      Sets the WINC device name. The name string is used as a device name in DHCP hostname (option 12).
@param [in] pu8DeviceName
            Buffer holding the device name.
@param [in] u8DeviceNameLength
            Length of the device name.
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@warning    The Function called once after initialization.
*/
sint8 m2m_wifi_set_device_name(uint8 *pu8DeviceName, uint8 u8DeviceNameLength)
{
    tstrM2MDeviceNameConfig strDeviceName;
    if(u8DeviceNameLength >= M2M_DEVICE_NAME_MAX)
    {
        u8DeviceNameLength = M2M_DEVICE_NAME_MAX;
    }
    //pu8DeviceName[u8DeviceNameLength] = '\0';
    u8DeviceNameLength ++;
    m2m_memcpy(strDeviceName.au8DeviceName, pu8DeviceName, u8DeviceNameLength);
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_DEVICE_NAME,
                    (uint8 *)&strDeviceName, sizeof(tstrM2MDeviceNameConfig), NULL, 0, 0);
}

/*!
@fn         NMI_API sint8 m2m_wifi_configure_sntp(uint8 *pu8NTPServerName, uint8 u8NTPServerNameLength, tenuSNTPUseDHCP useDHCP);
@brief      Configures what NTP server the SNTP client should use.
@param [in] pu8NTPServerName
            Buffer holding the NTP server name. If the first character is an asterisk (*) then it will be treated as a server pool, where the asterisk will
            be replaced with an incrementing value from 0 to 3 each time a server fails (example: *.pool.ntp.org).
@param [in] u8NTPServerNameLength
            Length of the NTP server name. Should not exceed the maximum NTP server name length of @ref M2M_NTP_MAX_SERVER_NAME_LENGTH
@param [in] useDHCP
            Should the NTP server provided by the DHCP server be used.
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
*/
sint8 m2m_wifi_configure_sntp(uint8 *pu8NTPServerName, uint8 u8NTPServerNameLength, tenuSNTPUseDHCP useDHCP)
{
    tstrM2MSNTPConfig strSNTPConfig;
    if(u8NTPServerNameLength > M2M_NTP_MAX_SERVER_NAME_LENGTH)
        return M2M_ERR_FAIL;

    m2m_memcpy((uint8 *)strSNTPConfig.acNTPServer, pu8NTPServerName, u8NTPServerNameLength);
    strSNTPConfig.acNTPServer[u8NTPServerNameLength] = '\0';
    strSNTPConfig.enuUseDHCP                         = useDHCP;

    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CONFIG_SNTP,
                    (uint8 *)&strSNTPConfig, sizeof(tstrM2MSNTPConfig), NULL, 0, 0);
}

sint8 m2m_wifi_get_firmware_version(tstrM2mRev *pstrRev)
{
    sint8 ret = M2M_SUCCESS;
    ret = hif_chip_wake();
    if(ret == M2M_SUCCESS)
    {
        ret = nm_get_firmware_full_info(pstrRev);
        hif_chip_sleep();
    }
    return ret;
}

sint8 m2m_wifi_start_provision_mode(tstrM2MAPConfig *pstrM2MAPConfig, char *pcHttpServerDomainName, uint8 bEnableHttpRedirect)
{
    tstrM2MAPModeConfig strM2MAPModeConfig;

    m2m_memcpy((uint8 *)&strM2MAPModeConfig.strApConfig, (uint8 *)pstrM2MAPConfig, sizeof(tstrM2MAPConfig));
    m2m_memcpy(strM2MAPModeConfig.strApConfigExt.au8DefRouterIP, pstrM2MAPConfig->au8DHCPServerIP, 4);
    m2m_memcpy(strM2MAPModeConfig.strApConfigExt.au8DNSServerIP, pstrM2MAPConfig->au8DHCPServerIP, 4);

    strM2MAPModeConfig.strApConfigExt.au8SubnetMask[0] = 0;

    return m2m_wifi_start_provision_mode_ext(&strM2MAPModeConfig, pcHttpServerDomainName, bEnableHttpRedirect);
}

sint8 m2m_wifi_start_provision_mode_ext(tstrM2MAPModeConfig *pstrAPModeConfig, char *pcHttpServerDomainName, uint8 bEnableHttpRedirect)
{
    sint8   s8Ret = M2M_ERR_FAIL;

    if(pstrAPModeConfig != NULL)
    {
        tstrM2MProvisionModeConfig  strProvConfig;
        if(M2M_SUCCESS == m2m_validate_ap_parameters(pstrAPModeConfig))
        {
            m2m_memcpy((uint8 *)&strProvConfig.strApConfig, (uint8 *)&pstrAPModeConfig->strApConfig, sizeof(tstrM2MAPConfig));
            m2m_memcpy((uint8 *)&strProvConfig.strApConfigExt, (uint8 *)&pstrAPModeConfig->strApConfigExt, sizeof(tstrM2MAPConfigExt));
            if((m2m_strlen((uint8 *)pcHttpServerDomainName) <= 0) || (NULL == pcHttpServerDomainName))
            {
                M2M_ERR("INVALID DOMAIN NAME\n");
                goto ERR1;
            }
            m2m_memcpy((uint8 *)strProvConfig.acHttpServerDomainName, (uint8 *)pcHttpServerDomainName, 64);
            strProvConfig.u8EnableRedirect = bEnableHttpRedirect;

            /* Stop Scan if it is ongoing.
            */
            gu8scanInProgress = 0;
            s8Ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_START_PROVISION_MODE | M2M_REQ_DATA_PKT,
                             (uint8 *)&strProvConfig, sizeof(tstrM2MProvisionModeConfig), NULL, 0, 0);
        }
        else
        {
            /*goto ERR1;*/
        }
    }
ERR1:
    return s8Ret;
}

sint8 m2m_wifi_stop_provision_mode(void)
{
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_STOP_PROVISION_MODE, NULL, 0, NULL, 0, 0);
}

sint8 m2m_wifi_get_connection_info(void)
{
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_GET_CONN_INFO, NULL, 0, NULL, 0, 0);
}

sint8 m2m_wifi_set_system_time(uint32 u32UTCSeconds)
{
    /*
        The firmware accepts timestamps relative to 1900 like NTP Timestamp.
    */
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_SYS_TIME, (uint8 *)&u32UTCSeconds, sizeof(tstrSystemTime), NULL, 0, 0);
}
/*!
 * @fn             NMI_API sint8 m2m_wifi_get_system_time(void);
 * @see            m2m_wifi_enable_sntp
                    tstrSystemTime
 * @note         get the system time from the sntp client
 *               using the API \ref m2m_wifi_get_system_time.
 * @return        The function returns @ref M2M_SUCCESS for successful operations and a negative value otherwise.
 */
sint8 m2m_wifi_get_system_time(void)
{
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_GET_SYS_TIME, NULL, 0, NULL, 0, 0);
}

sint8 m2m_wifi_enable_sntp(uint8 bEnable)
{
    uint8 u8Req;
    u8Req = bEnable ? M2M_WIFI_REQ_ENABLE_SNTP_CLIENT : M2M_WIFI_REQ_DISABLE_SNTP_CLIENT;
    return hif_send(M2M_REQ_GROUP_WIFI, u8Req, NULL, 0, NULL, 0, 0);
}

/*!
@fn         NMI_API sint8 m2m_wifi_set_power_profile(uint8 u8PwrMode);
@brief      Change the power profile mode
@param [in] u8PwrMode
            Change the WINC power profile to different mode
            PWR_LOW1/PWR_LOW2/PWR_HIGH/PWR_AUTO (tenuM2mPwrMode)
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@sa         tenuM2mPwrMode
@pre        m2m_wifi_init
@warning    must be called after the initializations and before any connection request and can't be changed in run time,
*/
sint8 m2m_wifi_set_power_profile(uint8 u8PwrMode)
{
    sint8 ret = M2M_SUCCESS;
    tstrM2mPwrMode strM2mPwrMode;
    strM2mPwrMode.u8PwrMode = u8PwrMode;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_POWER_PROFILE, (uint8 *)&strM2mPwrMode, sizeof(tstrM2mPwrMode), NULL, 0, 0);
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_set_tx_power(uint8 u8TxPwrLevel);
@brief      set the TX power tenuM2mTxPwrLevel
@param [in] u8TxPwrLevel
            change the TX power tenuM2mTxPwrLevel
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@sa         tenuM2mTxPwrLevel
@pre        m2m_wifi_init
@warning
*/
sint8 m2m_wifi_set_tx_power(uint8 u8TxPwrLevel)
{
    sint8 ret = M2M_SUCCESS;
    tstrM2mTxPwrLevel strM2mTxPwrLevel;
    strM2mTxPwrLevel.u8TxPwrLevel = u8TxPwrLevel;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_TX_POWER, (uint8 *)&strM2mTxPwrLevel, sizeof(tstrM2mTxPwrLevel), NULL, 0, 0);
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_set_gain_table_idx(uint8 u8GainTableIdx);
@brief      set the gain table index corresponding to a specific WiFi region
@param [in] u8GainTableIdx
            change the gain table index
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@pre        The gain tables must be written to the flash through gain builder tool.
            m2m_wifi_init
@warning
*/
sint8 m2m_wifi_set_gain_table_idx(uint8 u8GainTableIdx)
{
    sint8 ret = M2M_SUCCESS;
    tstrM2mWiFiGainIdx strM2mGainTableIdx;
    strM2mGainTableIdx.u8GainTableIdx = u8GainTableIdx;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_GAIN_TABLE_IDX, (uint8 *)&strM2mGainTableIdx, sizeof(tstrM2mWiFiGainIdx), NULL, 0, 0);
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_enable_firmware_logs(uint8 u8Enable);
@brief      Enable or Disable logs in run time (Disable Firmware logs will
            enhance the firmware start-up time and performance)
@param [in] u8Enable
            Set 1 to enable the logs 0 for disable
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@sa         __DISABLE_FIRMWARE_LOGS__ (build option to disable logs from initializations)
@pre        m2m_wifi_init
@warning
*/
sint8 m2m_wifi_enable_firmware_logs(uint8 u8Enable)
{
    sint8 ret = M2M_SUCCESS;
    tstrM2mEnableLogs strM2mEnableLogs;
    strM2mEnableLogs.u8Enable = u8Enable;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_ENABLE_LOGS, (uint8 *)&strM2mEnableLogs, sizeof(tstrM2mEnableLogs), NULL, 0, 0);
    return ret;
}

/*!
@fn         NMI_API sint8 m2m_wifi_set_battery_voltage(uint16 u16BattVoltx100);
@brief      Enable or Disable logs in run time (Disable Firmware logs will
            enhance the firmware start-up time and performance)
@param [in] u16BattVoltx100
            battery voltage multiplied by 100
@return     The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
@sa         __DISABLE_FIRMWARE_LOGS__ (build option to disable logs from initializations)
@pre        m2m_wifi_init
@warning
*/
sint8 m2m_wifi_set_battery_voltage(uint16 u16BattVoltx100)
{
    sint8 ret = M2M_SUCCESS;
    tstrM2mBatteryVoltage strM2mBattVol = {};
    strM2mBattVol.u16BattVolt = u16BattVoltx100;
    ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_BATTERY_VOLTAGE, (uint8 *)&strM2mBattVol, sizeof(tstrM2mBatteryVoltage), NULL, 0, 0);
    return ret;
}

/*!
@fn              sint8 m2m_wifi_prng_get_random_bytes(uint8 * pu8PrngBuff,uint16 u16PrngSize)
@brief       Get random bytes using the PRNG bytes.
@param [in]    u16PrngSize
             Size of the required random bytes to be generated.
@param [in]    pu8PrngBuff
                Pointer to user allocated buffer.
@return           The function SHALL return M2M_SUCCESS for success and a negative value otherwise.
*/
sint8 m2m_wifi_prng_get_random_bytes(uint8 *pu8PrngBuff, uint16 u16PrngSize)
{
    sint8 ret = M2M_ERR_FAIL;
    tstrPrng   strRng = {};
    if(
            (u16PrngSize <= (M2M_HIF_MAX_PACKET_SIZE - (M2M_HIF_HDR_OFFSET + sizeof(tstrPrng))))
        &&  (pu8PrngBuff != NULL)
    )
    {
        strRng.u16PrngSize = u16PrngSize;
        strRng.pu8RngBuff  = pu8PrngBuff;
        ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_GET_PRNG|M2M_REQ_DATA_PKT, (uint8 *)&strRng, sizeof(tstrPrng), NULL, 0, 0);
    }
    else
    {
        M2M_ERR("PRNG Buffer exceeded maximum size %d or NULL Buffer\n", u16PrngSize);
    }
    return ret;
}

/*!
@fn \
     NMI_API sint8 m2m_wifi_conf_auto_rate(tstrConfAutoRate * pstrConfAutoRate)

@brief
    Allow the host MCU app to configure auto TX rate selection algorithm. The application can use this
    API to tweak the algorithm performance. Moreover, it allows the application to force a specific WLAN
    PHY rate for transmitted data packets to favor range vs. throughput needs.

@param [in] pstrConfAutoRate
    The Auto rate configuration parameters as listed in tstrConfAutoRate.

@warning
    The application will be responsible for keeping track of what values are set and, if required,
    going back to default values. Changes will become permanent until the chip is power-cycled.

@sa
    tstrConfAutoRate

@return
    The function SHALL return 0 for success and a negative value otherwise.
*/

NMI_API sint8 m2m_wifi_conf_auto_rate(tstrConfAutoRate *pstrConfAutoRate)
{
    sint8 s8ret = M2M_ERR_FAIL;
    s8ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_CONG_AUTO_RATE, (uint8 *)pstrConfAutoRate, sizeof(tstrConfAutoRate), NULL, 0, 0);

    return s8ret;
}

#ifdef ETH_MODE
/*!
@fn \
     NMI_API sint8 m2m_wifi_enable_mac_mcast(uint8* pu8MulticastMacAddress, uint8 u8AddRemove)

@brief
    Add MAC filter to receive Multicast packets.

@param [in] pu8MulticastMacAddress
                Pointer to the MAC address.
@param [in] u8AddRemove
                Flag to Add/Remove MAC address.
@return
    The function SHALL return 0 for success and a negative value otherwise.
*/

NMI_API sint8 m2m_wifi_enable_mac_mcast(uint8 *pu8MulticastMacAddress, uint8 u8AddRemove)
{
    sint8 s8ret = M2M_ERR_FAIL;
    tstrM2MMulticastMac  strMulticastMac;

    if(pu8MulticastMacAddress != NULL)
    {
        strMulticastMac.u8AddRemove = u8AddRemove;
        m2m_memcpy(strMulticastMac.au8macaddress, pu8MulticastMacAddress, M2M_MAC_ADDRES_LEN);
        M2M_DBG("mac multicast: %x:%x:%x:%x:%x:%x\r\n", strMulticastMac.au8macaddress[0], strMulticastMac.au8macaddress[1], strMulticastMac.au8macaddress[2], strMulticastMac.au8macaddress[3], strMulticastMac.au8macaddress[4], strMulticastMac.au8macaddress[5]);
        s8ret = hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_SET_MAC_MCAST, (uint8 *)&strMulticastMac, sizeof(tstrM2MMulticastMac), NULL, 0, 0);
    }

    return s8ret;
}

/*!
@fn \
    NMI_API sint8  m2m_wifi_set_receive_buffer(void* pvBuffer,uint16 u16BufferLen);

@brief
    set the ethernet receive buffer, should be called in the receive call back.

@param [in] pvBuffer
                Pointer to the ethernet receive buffer.
@param [in] u16BufferLen
                Length of the buffer.

@return
    The function SHALL return 0 for success and a negative value otherwise.
*/
NMI_API sint8  m2m_wifi_set_receive_buffer(void *pvBuffer, uint16 u16BufferLen)
{
    sint8 s8ret = M2M_SUCCESS;
    if(pvBuffer != NULL)
    {
        gau8ethRcvBuf = pvBuffer;
        gu16ethRcvBufSize= u16BufferLen;
    }
    else
    {
        s8ret = M2M_ERR_FAIL;
        M2M_ERR("Buffer NULL pointer\r\n");
    }
    return s8ret;
}
#endif /* ETH_MODE */

sint8 m2m_wifi_reinit(tstrWifiInitParam *pWifiInitParam)
{
    sint8 ret = m2m_wifi_reinit_hold();
    if(M2M_SUCCESS == ret)
        ret = m2m_wifi_reinit_start(pWifiInitParam);

    return ret;
}

sint8 m2m_wifi_reinit_hold(void)
{
    m2m_wifi_deinit(NULL);
    return m2m_wifi_init_hold();
}

sint8 m2m_wifi_reinit_start(tstrWifiInitParam *pWifiInitParam)
{
    return m2m_wifi_init_start(pWifiInitParam);
}

uint8 m2m_wifi_get_state(void)
{
    return gu8WifiState;
}

sint8 m2m_wifi_enable_roaming(uint8 bEnableDhcp)
{
    tstrM2mWiFiRoaming  strWiFiRoaming;
    strWiFiRoaming.u8EnableRoaming = 1;
    if(0 == bEnableDhcp || 1 == bEnableDhcp)
    {
        strWiFiRoaming.u8EnableDhcp = bEnableDhcp;
        return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_ROAMING,
                        (uint8 *) &strWiFiRoaming, sizeof(tstrM2mWiFiRoaming), NULL, 0, 0);
    }
    else
    {
        return M2M_ERR_INVALID_ARG;
    }
}

sint8 m2m_wifi_disable_roaming(void)
{
    tstrM2mWiFiRoaming  strWiFiRoaming;
    strWiFiRoaming.u8EnableRoaming = 0;
    return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_ROAMING, (uint8 *) &strWiFiRoaming, sizeof(tstrM2mWiFiRoaming), NULL, 0, 0);
}

sint8 m2m_wifi_enable_XO_during_sleep(uint8 bXOSleepEnable)
{
    tstrM2mWiFiXOSleepEnable  strM2mWiFiXOSleepEnable;

    if(0 == bXOSleepEnable || 1 == bXOSleepEnable)
    {
        strM2mWiFiXOSleepEnable.u8EnableXODuringSleep = bXOSleepEnable;
        return hif_send(M2M_REQ_GROUP_WIFI, M2M_WIFI_REQ_XO_SLEEP_ENABLE,
                        (uint8 *) &strM2mWiFiXOSleepEnable, sizeof(strM2mWiFiXOSleepEnable), NULL, 0, 0);
    }
    else
    {
        return M2M_ERR_INVALID_ARG;
    }
}
