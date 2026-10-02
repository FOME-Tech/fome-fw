#include "pch.h"

#if EFI_WIFI

#include "wifi_socket.h"
#include "thread_controller.h"
#include "driver/include/m2m_wifi.h"
#include "socket/include/socket.h"

static chibios_rt::BinarySemaphore isrSemaphore(/* taken =*/true);

/*static*/ ServerSocket* ServerSocket::s_serverList = nullptr;

static bool s_clientConnected = false;
static systime_t s_lastDhcpEvent = 0;
static systime_t s_lastSocketActivity = 0;

static void noteSocketActivity() {
	s_lastSocketActivity = chVTGetSystemTimeX();
}

ServerSocket::ServerSocket() {
	// Add server to linked list
	m_nextServer = s_serverList;
	s_serverList = this;

	// Set up queue
	iqObjectInit(&m_recvQueue, m_recvQueueBuffer, sizeof(m_recvQueueBuffer), nullptr, nullptr);
}

void ServerSocket::startListening(const sockaddr_in& addr, int backlog) {
	m_listenAddr = addr;
	m_backlog = backlog;

	m_listenerSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
	if (m_listenerSocket >= 0) {
		bind(m_listenerSocket, (sockaddr*)&m_listenAddr, sizeof(m_listenAddr));
	}
}

void ServerSocket::onAccept(int connectedSocket) {
	noteSocketActivity();

	if (m_connectedSocket != -1) {
		efiPrintf("WiFi: Rejecting parallel sock %d (active sock %d)", connectedSocket, (int)m_connectedSocket);
		close(connectedSocket);
		return;
	}

	m_connectedSocket = connectedSocket;
	m_connectedTime = chVTGetSystemTimeX();

	recv(m_connectedSocket, &m_recvBuf, 1, 0);

	// Wake any thread that is waiting for a connection
	m_acceptSemaphore.signal();
}

bool ServerSocket::closeSocket() {
	bool wasOpen = m_connectedSocket != -1;
	if (wasOpen) {
		m_pendingCloseSocket = m_connectedSocket;
		m_connectedSocket = -1;
		isrSemaphore.signal();
	}

	{
		chibios_rt::CriticalSectionLocker csl;
		iqResetI(&m_recvQueue);

		m_sendRequest = false;
		m_sendDoneSemaphore.resetI(true);
		m_acceptSemaphore.resetI(true);
	}

	return wasOpen;
}

void ServerSocket::onClose() {
	closeSocket();
}

void ServerSocket::onRecv(uint8_t* buffer, size_t recvSize, size_t remaining) {
	noteSocketActivity();

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

bool ServerSocket::waitForConnection(int timeoutMs) {
	// Fast-path: already connected before going to sleep
	if (m_connectedSocket != -1) {
		return true;
	}
	return m_acceptSemaphore.wait(TIME_MS2I(timeoutMs)) == MSG_OK;
}

/*static*/ bool ServerSocket::checkSend() {
	bool result = false;
	static ServerSocket* s_lastServiced = nullptr;

	ServerSocket* start = s_lastServiced ? s_lastServiced->m_nextServer : nullptr;
	if (!start) {
		start = s_serverList;
	}

	ServerSocket* current = start;
	do {
		if (current && current->trySendImpl()) {
			result = true;
			s_lastServiced = current;
			break;
		}
		current = current ? current->m_nextServer : nullptr;
		if (!current) {
			current = s_serverList;
		}
	} while (current != start);

	return result;
}

void ServerSocket::send(const void* buffer, size_t size) {
	if (!hasConnectedSocket() || buffer == nullptr || size == 0) {
		return;
	}

	noteSocketActivity();

	const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffer);
	while (size > 0 && hasConnectedSocket()) {
		size_t toSend = std::min(size, static_cast<size_t>(SOCKET_BUFFER_MAX_LENGTH));
		memcpy(m_txBuf, ptr, toSend);

		m_sendBuffer = m_txBuf;
		m_sendSize = toSend;
		m_sendRequest = true;

		// Wake the driver to perform the actual send
		isrSemaphore.signal();

		// Wait for this chunk to complete over the air.
		// 5s timeout guards against a driver that never fires SOCKET_MSG_SEND.
		msg_t result = m_sendDoneSemaphore.wait(TIME_MS2I(5000));
		if (result != MSG_OK) {
			// Timeout, or the semaphore was reset by closeSocket() without a successful
			// send — cancel the pending request and close so callers see the failure.
			m_sendRequest = false;
			closeSocket();
			break;
		}

		ptr += toSend;
		size -= toSend;
	}
}

void ServerSocket::onSendDone() {
	noteSocketActivity();
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

/*static*/ void ServerSocket::closeAllConnected() {
	auto current = s_serverList;
	while (current) {
		current->closeSocket();
		current = current->m_nextServer;
	}
}

void ServerSocket::tryCloseImpl() {
	if (m_pendingCloseSocket != -1) {
		close(m_pendingCloseSocket);
		m_pendingCloseSocket = -1;
	}
}

/*static*/ void ServerSocket::checkClose() {
	auto current = s_serverList;
	while (current) {
		current->tryCloseImpl();
		current = current->m_nextServer;
	}
}

/*static*/ bool ServerSocket::hasAnyConnectedSocket() {
	auto current = s_serverList;
	while (current) {
		if (current->hasConnectedSocket()) {
			return true;
		}
		current = current->m_nextServer;
	}
	return false;
}

bool ServerSocket::trySendImpl() {
	if ((m_connectedSocket != -1) && m_sendRequest) {
		sint16 result = ::send(m_connectedSocket, (void*)m_sendBuffer, m_sendSize, 0);

		if (result == SOCK_ERR_NO_ERROR) {
			m_sendRequest = false;
			return true;
		} else if (result == SOCK_ERR_BUFFER_FULL) {
			// Driver buffers are full, retry on the next helper tick
			return false;
		} else {
			// Permanent error: wake the caller so it isn't deadlocked waiting for a
			// send-done that will never arrive. It detects the error on next operation.
			efiPrintf("WiFi: send error %d on sock %d", (int)result, m_connectedSocket);
			m_sendRequest = false;
			closeSocket();
			return true;
		}
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
			s_clientConnected = true;
			s_lastDhcpEvent = chVTGetSystemTimeX();
			s_lastSocketActivity = s_lastDhcpEvent;
		} break;
		case M2M_WIFI_RESP_CON_STATE_CHANGED: {
			auto* pstrWifiState = reinterpret_cast<tstrM2mWifiStateChanged*>(pvMsg);
			if (pstrWifiState->u8CurrState == M2M_WIFI_DISCONNECTED) {
				efiPrintf("WiFi: Client disconnected (err %d)", (int)pstrWifiState->u8ErrCode);
				ServerSocket::closeAllConnected();
				s_clientConnected = false;
				s_lastDhcpEvent = 0;
				s_lastSocketActivity = 0;
			} else if (pstrWifiState->u8CurrState == M2M_WIFI_CONNECTED) {
				efiPrintf("WiFi: Client connected");
			}
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
				if (auto server = ServerSocket::findListener(sock)) {
					int backlog = server->getBacklog();
					efiPrintf("WiFi: Bind ok on sock %d, listening with backlog %d", (int)sock, backlog);
					listen(sock, backlog);
				} else {
					efiPrintf("WiFi: Bind ok on unknown sock %d", (int)sock);
				}
			} else {
				efiPrintf("WiFi: Bind failed on sock %d with %d", (int)sock, bindMsg ? (int)bindMsg->status : -99);
			}
		} break;
		case SOCKET_MSG_LISTEN: {
			// accept() is implicit; just surface a failure if listen didn't take
			auto listenMsg = reinterpret_cast<tstrSocketListenMsg*>(pvMsg);
			if (listenMsg && listenMsg->status == 0) {
				efiPrintf("WiFi: Listening ok on sock %d", (int)sock);
			} else {
				efiPrintf(
						"WiFi: Listen failed on sock %d with %d", (int)sock, listenMsg ? (int)listenMsg->status : -99);
			}
		} break;
		case SOCKET_MSG_ACCEPT: {
			auto acceptMsg = reinterpret_cast<tstrSocketAcceptMsg*>(pvMsg);
			if (acceptMsg && (acceptMsg->sock >= 0)) {
				// Enable TCP keep-alive on the connected socket to detect dead peers
				int keepAlive = 1;
				setsockopt(acceptMsg->sock, SOL_SOCKET, SO_TCP_KEEPALIVE, &keepAlive, sizeof(keepAlive));

				// Detect dead peers: 10s idle
				int keepIdle = 20; // units of 500ms (10s)
				setsockopt(acceptMsg->sock, SOL_SOCKET, SO_TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));

				if (auto server = ServerSocket::findListener(sock)) {
					server->onAccept(acceptMsg->sock);
				} else {
					// No server owns this listener — don't leak the accepted socket
					efiPrintf("WiFi: No listener for sock %d", (int)sock);
					close(acceptMsg->sock);
				}
			}
		} break;
		case SOCKET_MSG_RECV: {
			auto recvMsg = reinterpret_cast<tstrSocketRecvMsg*>(pvMsg);
			if (recvMsg && (recvMsg->s16BufferSize > 0)) {
				noteSocketActivity();
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
			auto sentBytes = reinterpret_cast<sint16*>(pvMsg);
			if (sentBytes && *sentBytes < 0) {
				efiPrintf("WiFi: Send failed with %d on sock %d", (int)*sentBytes, (int)sock);
				if (auto server = ServerSocket::findConnected(sock)) {
					server->closeSocket();
				}
			} else {
				if (auto server = ServerSocket::findConnected(sock)) {
					server->onSendDone();
				}
			}
		} break;
	}
}

__attribute__((weak)) const wifi_string_t& getWifiSsid() {
	return config->wifiAccessPointSsid;
}

__attribute__((weak)) const wifi_string_t& getWifiPassword() {
	return config->wifiAccessPointPassword;
}

class WifiHelperThread : public ThreadController<4096> {
public:
	WifiHelperThread()
		: ThreadController("WiFi", WIFI_THREAD_PRIORITY) {}
	void ThreadTask() override {
		if (!initWifi()) {
			return;
		}

		m_initDone = true;

		while (true) {
			{
				ScopePerf perf(PE::WifiHandleEvents);
				m2m_wifi_handle_events(nullptr);
			}

			ServerSocket::checkClose();

			if (!ServerSocket::checkSend()) {
				isrSemaphore.wait(TIME_MS2I(10));
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

		// Disable power save to prevent the AP from freezing after long periods of inactivity
		m2m_wifi_set_sleep_mode(M2M_NO_PS, 1);

#ifdef WIFI_OFFSET_MAC
		{
			uint8_t mac[6];
			m2m_wifi_get_mac_address(mac);
			mac[5]++;
			m2m_wifi_set_mac_address(mac);
		}
#endif

		static tstrM2MAPConfig apConfig;
		const wifi_string_t& ssid = getWifiSsid();
		strncpy(apConfig.au8SSID, ssid, std::min(sizeof(apConfig.au8SSID), sizeof(ssid)));
		apConfig.u8ListenChannel = 1;
		apConfig.u8SsidHide = 0;

		const wifi_string_t& password = getWifiPassword();
		size_t keyLength = strlen(password);
		if (keyLength > 0) {
			apConfig.u8SecType = M2M_WIFI_SEC_WPA_PSK;
			apConfig.u8KeySz = keyLength;
			strncpy((char*)apConfig.au8Key, password, std::min(sizeof(apConfig.au8Key), sizeof(password)));
		} else {
			apConfig.u8SecType = M2M_WIFI_SEC_OPEN;
		}

		// IP Address
		apConfig.au8DHCPServerIP[0] = 192;
		apConfig.au8DHCPServerIP[1] = 168;
		apConfig.au8DHCPServerIP[2] = 10;
		apConfig.au8DHCPServerIP[3] = 1;

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
	wifiHelper.startThread();
}

void waitForWifiInit() {
	while (!wifiHelper.initDone()) {
		chThdSleepMilliseconds(10);
	}
}

void stopWifi() {
	m2m_wifi_disable_ap();
	chThdSleepMilliseconds(500);
	m2m_wifi_deinit(nullptr);
}

#endif
