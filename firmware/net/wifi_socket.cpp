#include "pch.h"
#include "wifi_socket.h"
#include "thread_controller.h"

#include "driver/include/m2m_wifi.h"

#include "lwipthread.h"

#include <lwip/opt.h>
#include <lwip/def.h>
#include <lwip/mem.h>
#include <lwip/pbuf.h>
#include <lwip/sys.h>
#include <lwip/stats.h>
#include <lwip/snmp.h>
#include <lwip/tcpip.h>
#include <netif/etharp.h>
#include <lwip/netifapi.h>

static chibios_rt::BinarySemaphore isrSemaphore(/* taken =*/ true);
static chibios_rt::BinarySemaphore sendDoneSemaphore{/* taken =*/ true};

void os_hook_isr() {
	isrSemaphore.signalI();
}

static void wifiCallback(uint8 u8MsgType, void* pvMsg) {
	switch (u8MsgType) {
		case M2M_WIFI_REQ_DHCP_CONF: {
			auto& dhcpInfo = *reinterpret_cast<tstrM2MIPConfig*>(pvMsg);
			uint8_t* addr = reinterpret_cast<uint8_t*>(&dhcpInfo.u32StaticIP);
			efiPrintf("WiFi client connected DHCP IP is %d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
		} break;
		default:
			efiPrintf("WifiCallback: %d", (int)u8MsgType);
			break;
	}
}

static netif thisif;

void ethernetCallback(uint8 u8MsgType, void * pvMsg,void * pvCtrlBuf) {
	switch (u8MsgType) {
	case M2M_WIFI_RESP_ETHERNET_RX_PACKET:
		auto frame = reinterpret_cast<const uint8_t*>(pvMsg);
		auto ctrlBuf = reinterpret_cast<const tstrM2mIpCtrlBuf*>(pvCtrlBuf);

		size_t len = ctrlBuf->u16DataSize;

		#if ETH_PAD_SIZE
			len += ETH_PAD_SIZE;        /* allow room for Ethernet padding */
		#endif

		pbuf* p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

		if (!p) {
			// couldn't allocate, drop the frame
			return;
		}

		#if ETH_PAD_SIZE
			pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
		#endif

		// copy the frame in to the pbuf
		pbuf_take(p, frame, len);

		#if ETH_PAD_SIZE
			pbuf_header(*p, ETH_PAD_SIZE); /* reclaim the padding word */
		#endif

		auto ethhdr = reinterpret_cast<const eth_hdr*>(p->payload);
		switch (htons(ethhdr->type)) {
			/* IP or ARP packet? */
			case ETHTYPE_IP:
			case ETHTYPE_ARP:
				/* full packet send to tcpip_thread to process */
				if (thisif.input(p, &thisif) == ERR_OK)
				break;
				LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
			/* Falls through */
			default:
				pbuf_free(p);
		}

		break;
	}
}

static pbuf* outgoing_pbuf = nullptr;

void checkOutgoingPbuf() {
	if (outgoing_pbuf) {
		#if ETH_PAD_SIZE
			// drop the padding word
			pbuf_header(outgoing_pbuf, -ETH_PAD_SIZE);
		#endif

		static NO_CACHE uint8_t ethernetTxBuffer[1600];

		// copy pbuf -> tx buffer
		pbuf_copy_partial(outgoing_pbuf, ethernetTxBuffer, outgoing_pbuf->tot_len, 0);

		// transmit the frame
		m2m_wifi_send_ethernet_pkt(ethernetTxBuffer, outgoing_pbuf->tot_len);

		outgoing_pbuf = nullptr;
		sendDoneSemaphore.signal();
	}
}

static thread_t* wifiThreadRef = nullptr;

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
	if (netif != &thisif) {
		return ERR_IF;
	}

	#if ETH_PAD_SIZE
		// drop the padding word
		pbuf_header(p, -ETH_PAD_SIZE);
	#endif

	outgoing_pbuf = p;

	if (wifiThreadRef == chThdGetSelfX()) {
		checkOutgoingPbuf();
	} else {
		isrSemaphore.signal();
	}

	sendDoneSemaphore.wait();

	#if ETH_PAD_SIZE
		// reclaim the padding word
		pbuf_header(p, ETH_PAD_SIZE);
	#endif

	// isrSemaphore.signal();

	return ERR_OK;
}

static err_t ethernetif_init(struct netif *netif) {
	netif->linkoutput = low_level_output;
	netif->output = etharp_output;
	netif->mtu = LWIP_NETIF_MTU;
	/* device capabilities */
	/* don't set NETIF_FLAG_ETHARP if this device is not an Ethernet one */
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

	netif->name[0] = 'w';
	netif->name[1] = '0';

	netif->hwaddr_len = ETHARP_HWADDR_LEN;

	netif->state = NULL;

	return ERR_OK;
}

bool setupLwip() {
	tcpip_init(NULL, NULL);

	ip4_addr_t ip, gateway, netmask;
	LWIP_IPADDR(&ip);
	LWIP_GATEWAY(&gateway);
	LWIP_NETMASK(&netmask);

	auto result = netifapi_netif_add(&thisif, &ip, &netmask, &gateway, NULL, ethernetif_init, tcpip_input);
	if (result != ERR_OK) {
		return false;
	}

	netifapi_netif_set_default(&thisif);
	netifapi_netif_set_up(&thisif);
	netifapi_netif_set_link_up(&thisif);

	return true;
}

class WifiHelperThread : public ThreadController<4096> {
public:
	WifiHelperThread() : ThreadController("WiFi", WIFI_THREAD_PRIORITY) {}
	void ThreadTask() override {
		wifiThreadRef = chThdGetSelfX();

		if (!initWifi()) {
			return;
		}

		m_initDone = true;

		while (true)
		{
			// check for outgoing frame
			checkOutgoingPbuf();

			m2m_wifi_handle_events(nullptr);

			isrSemaphore.wait(TIME_MS2I(1));
		}
	}

	bool initDone() const {
		return m_initDone;
	}

private:
	bool initWifi() {
		// Initialize the WiFi module
		static tstrWifiInitParam param;
		param.pfAppWifiCb = wifiCallback;

		// Ethernet options
		static NO_CACHE uint8_t ethernetRxBuffer[1600];
		param.strEthInitParam.pfAppEthCb = ethernetCallback;
		param.strEthInitParam.au8ethRcvBuf = ethernetRxBuffer;
		param.strEthInitParam.u16ethRcvBufSize = sizeof(ethernetRxBuffer);
		param.strEthInitParam.u8EthernetEnable = M2M_WIFI_MODE_ETHERNET;

		if (auto ret = m2m_wifi_init(&param); M2M_SUCCESS != ret) {
			efiPrintf("Wifi init failed with: %d", ret);
			return false;
		}

		static tstrM2MAPConfig apConfig;
		strncpy(apConfig.au8SSID, config->wifiAccessPointSsid, std::min(sizeof(apConfig.au8SSID), sizeof(config->wifiAccessPointSsid)));
		apConfig.u8ListenChannel = 1;
		apConfig.u8SsidHide = 0;

		size_t keyLength = strlen(config->wifiAccessPointPassword);
		if (keyLength > 0) {
			apConfig.u8SecType = M2M_WIFI_SEC_WPA_PSK;
			apConfig.u8KeySz = keyLength;
			strncpy((char*)apConfig.au8Key, config->wifiAccessPointPassword, std::min(sizeof(apConfig.au8Key), sizeof(config->wifiAccessPointPassword)));
		} else {
			apConfig.u8SecType = M2M_WIFI_SEC_OPEN;
		}

		// IP Address
		apConfig.au8DHCPServerIP[0]	= 192;
		apConfig.au8DHCPServerIP[1]	= 168;
		apConfig.au8DHCPServerIP[2]	= 10;
		apConfig.au8DHCPServerIP[3]	= 1;

		// Trigger AP
		if (M2M_SUCCESS != m2m_wifi_enable_ap(&apConfig)) {
			return false;
		}

		// Set the real WiFi chipset's MAC address on the lwip interface
		if (M2M_SUCCESS != m2m_wifi_get_mac_address(thisif.hwaddr)) {
			return false;
		}

		return setupLwip();
	}

	bool m_initDone = false;
};

static NO_CACHE WifiHelperThread wifiHelper;

void initWifi() {
	wifiHelper.start();
}

void waitForWifiInit() {
	while (!wifiHelper.initDone()) {
		chThdSleepMilliseconds(10);
	}
}
