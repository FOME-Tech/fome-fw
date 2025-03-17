#include "pch.h"

#if EFI_WIFI

#include "wifi_socket.h"
#include "thread_controller.h"
#include "driver/include/m2m_wifi.h"
#include "socket/include/socket.h"

static chibios_rt::BinarySemaphore isrSemaphore(/* taken =*/ true);

/*static*/ ServerSocket* ServerSocket::s_serverList = nullptr;

ServerSocket::ServerSocket() {
	// Add server to linked list
	m_nextServer = s_serverList;
	s_serverList = this;

	// Set up queue
	iqObjectInit(&m_recvQueue, m_recvQueueBuffer, sizeof(m_recvQueueBuffer), nullptr, nullptr);
}

void ServerSocket::startListening(const sockaddr_in& addr) {
	m_listenerSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
	bind(m_listenerSocket, (sockaddr*)&addr, sizeof(addr));
}

void ServerSocket::onAccept(int connectedSocket) {
	m_connectedSocket = connectedSocket;

	recv(m_connectedSocket, &m_recvBuf, 1, 0);
}

void ServerSocket::onClose() {
	close(m_connectedSocket);

	m_connectedSocket = -1;

	{
		chibios_rt::CriticalSectionLocker csl;
		iqResetI(&m_recvQueue);
	}
}

void ServerSocket::onRecv(uint8_t* buffer, size_t recvSize, size_t remaining) {
	{
		chibios_rt::CriticalSectionLocker csl;

		for (size_t i = 0; i < recvSize; i++) {
			iqPutI(&m_recvQueue, buffer[i]);
		}
	}

	size_t nextRecv;
	if (remaining < 1) {
		// Always try to read at least 1 byte
		nextRecv = 1;
	} else if (remaining > sizeof(m_recvBuf)) {
		// Remaining is too big for the buffer, so just read one buffer worth
		nextRecv = sizeof(m_recvBuf);
	} else {
		// The full thing will fit, try to read it
		nextRecv = remaining;
	}

	// start the next recv
	recv(m_connectedSocket, &m_recvBuf, nextRecv, 0);
}

bool ServerSocket::hasConnectedSocket() const {
	return m_connectedSocket != -1;
}

/*static*/ bool ServerSocket::checkSend() {
	bool result = false;

	auto current = s_serverList;

	while (current) {
		result |= current->trySendImpl();
		current = current->m_nextServer;
	}

	return result;
}

void ServerSocket::send(uint8_t* buffer, size_t size) {
	m_sendBuffer = buffer;
	m_sendSize = size;
	m_sendRequest = true;

	// Wake the driver to perform the actual send
	isrSemaphore.signal();

	// Wait for this chunk to complete
	m_sendDoneSemaphore.wait();
}

void ServerSocket::onSendDone() {
	// Send completed, notify caller!
	chibios_rt::CriticalSectionLocker csl;
	m_sendDoneSemaphore.signalI();
}

size_t ServerSocket::recvTimeout(uint8_t* buffer, size_t size, int timeout) {
	return iqReadTimeout(&m_recvQueue, buffer, size, timeout);
}

/*static*/ ServerSocket* ServerSocket::findListener(int sock) {
	auto current = s_serverList;

	while (current) {
		if (current->m_listenerSocket == sock) {
			break;
		}

		current = current->m_nextServer;
	}

	return current;
}

/*static*/ ServerSocket* ServerSocket::findConnected(int sock) {
	auto current = s_serverList;

	while (current) {
		if (current->m_connectedSocket == sock) {
			break;
		}

		current = current->m_nextServer;
	}

	return current;
}

bool ServerSocket::trySendImpl() {
	if ((m_connectedSocket != -1) && m_sendRequest) {
		::send(m_connectedSocket, (void*)m_sendBuffer, m_sendSize, 0);
		m_sendRequest = false;

		return true;
	}

	return false;
}

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

static void socketCallback(SOCKET sock, uint8_t u8Msg, void* pvMsg) {
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
				if (auto server = ServerSocket::findListener(sock)) {
					server->onAccept(acceptMsg->sock);
				}
			}
		} break;
		case SOCKET_MSG_RECV: {
			auto recvMsg = reinterpret_cast<tstrSocketRecvMsg*>(pvMsg);
			if (recvMsg && (recvMsg->s16BufferSize > 0)) {
				if (auto server = ServerSocket::findConnected(sock)) {
					server->onRecv(recvMsg->pu8Buffer, recvMsg->s16BufferSize, recvMsg->u16RemainingSize);
				}
			} else {
				if (auto server = ServerSocket::findConnected(sock)) {
					server->onClose();
				}
			}
		} break;
		case SOCKET_MSG_SEND: {
			if (auto server = ServerSocket::findConnected(sock)) {
				server->onSendDone();
			}
		} break;
	}
}

class WifiHelperThread : public ThreadController<4096> {
public:
	WifiHelperThread() : ThreadController("WiFi", WIFI_THREAD_PRIORITY) {}
	void ThreadTask() override {
		if (!initWifi()) {
			return;
		}

		m_initDone = true;

		while (true)
		{
			m2m_wifi_handle_events(nullptr);

			if (!ServerSocket::checkSend()) {
				isrSemaphore.wait(TIME_MS2I(1));
			}
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

		// Set up the socket APIs
		socketInit();
		registerSocketCallback(socketCallback, nullptr);

		return true;
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

#endif
