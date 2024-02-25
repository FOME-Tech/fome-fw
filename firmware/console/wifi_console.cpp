#include "pch.h"

#define EFI_WIFI 1

#if EFI_WIFI

#include "driver/include/m2m_wifi.h"
#include "socket/include/socket.h"

#include "thread_controller.h"
#include "tunerstudio.h"

static int listenerSocket = -1;
static int connectionSocket = -1;

static void do_connection() {
	if (connectionSocket != -1) {
		auto localCopy = connectionSocket;
		connectionSocket = -1;

		close(localCopy);
	}

	connectionSocket = accept(listenerSocket, nullptr, 0);
}


class WifiChannel : public TsChannelBase {
public:
	WifiChannel()
		: TsChannelBase("WiFi")
	{
	}

	bool isReady() const override {
		return false;
	}

	void write(const uint8_t* buffer, size_t size, bool /*isEndOfPacket*/) override {
		// If not the end of a packet, set the MSG_MORE flag to indicate to the transport
		// that we have more to add to the buffer before queuing a flush.
		// auto flags = isEndOfPacket ? 0 : MSG_MORE;
		send(connectionSocket, const_cast<uint8_t*>(buffer), size, 0);
	}

	size_t readTimeout(uint8_t* buffer, size_t size, int timeout) override {
		auto result = recv(connectionSocket, buffer, size, timeout);

		if (result != 0) {
			do_connection();
			return 0;
		}

		return size;
	}
};

static WifiChannel wifiChannel;

class WifiHelperThread : public ThreadController<4096> {
public:
	WifiHelperThread() : ThreadController("WifiPump", LWIP_THREAD_PRIORITY) {}
	void ThreadTask() override {
		while (true)
		{
			m2m_wifi_handle_events(nullptr);
			chThdSleepMilliseconds(1);
		}
	}
};

static WifiHelperThread wifiHelper;

static tstrWifiInitParam param;

static tstrM2MAPConfig apConfig;

void wifiCallback(uint8 u8MsgType, void* pvMsg) {
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

uint8_t buuf[32];

static void socketCb(SOCKET sock, uint8_t u8Msg, void* pvMsg) {
	switch (u8Msg) {
		case SOCKET_MSG_BIND: {
			auto bindMsg = reinterpret_cast<tstrSocketBindMsg*>(pvMsg);

			if (bindMsg && bindMsg->status == 0) {
				// Socket bind complete, now listen!
				listen(sock, 1);
			}
		} break;
		case SOCKET_MSG_LISTEN: {
			auto listenMsg = reinterpret_cast<tstrSocketListenMsg*>(pvMsg);
			if (listenMsg && listenMsg->status == 0) {
				// Listening, now accept a connection
				accept(sock, nullptr, nullptr);
			}
		} break;
		case SOCKET_MSG_ACCEPT: {
			auto acceptMsg = reinterpret_cast<tstrSocketAcceptMsg*>(pvMsg);
			if (acceptMsg && (acceptMsg->sock >= 0)) {
				connectionSocket = acceptMsg->sock;

				recv(connectionSocket, buuf, 1, 0);
			}
		} break;
		case SOCKET_MSG_RECV: {
			auto recvMsg = reinterpret_cast<tstrSocketRecvMsg*>(pvMsg);
			if (recvMsg) {
				efiPrintf("recv");
			}
		} break;
	}
}

struct WifiConsoleThread : public TunerstudioThread {
	WifiConsoleThread() : TunerstudioThread("WiFi Console") { }

	TsChannelBase* setupChannel() override {
		param.pfAppWifiCb = wifiCallback;
		if (M2M_SUCCESS != m2m_wifi_init(&param)) {
			return nullptr;
		}

		strcpy(apConfig.au8SSID, "FOME EFI");
		apConfig.u8ListenChannel 	= 1;
		apConfig.u8SecType			= M2M_WIFI_SEC_OPEN;
		apConfig.u8SsidHide			= 0;

		// IP Address
		apConfig.au8DHCPServerIP[0]	= 192;
		apConfig.au8DHCPServerIP[1]	= 168;
		apConfig.au8DHCPServerIP[2]	= 1;
		apConfig.au8DHCPServerIP[3]	= 1;

		// Trigger AP
		if (M2M_SUCCESS != m2m_wifi_enable_ap(&apConfig)) {
			return nullptr;
		}

		// Start the helper thread
		wifiHelper.start();

		socketInit();
		registerSocketCallback(socketCb, nullptr);

		// Start listening on the socket
		sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_port = _htons(29000);
		address.sin_addr.s_addr = 0;

		listenerSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
		bind(listenerSocket, (sockaddr*)&address, sizeof(address));

		// do_connection();

		// return &wifiChannel;
		return nullptr;
	}
};

static WifiConsoleThread wifiThread;

void startWifiConsole() {
	wifiThread.start();
}

#endif // EFI_WIFI
