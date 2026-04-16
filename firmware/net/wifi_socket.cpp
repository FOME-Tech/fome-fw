#include "pch.h"

#if EFI_WIFI

#include "wifi_socket.h"
#include "thread_controller.h"
#include "driver/include/m2m_wifi.h"
#include "socket/include/socket.h"

static chibios_rt::BinarySemaphore isrSemaphore(/* taken =*/true);

/*static*/ ServerSocket* ServerSocket::s_serverList = nullptr;

ServerSocket::ServerSocket(const char* name) : m_name(name) {
	// Add server to linked list
	m_nextServer = s_serverList;
	s_serverList = this;

	// Set up queue
	iqObjectInit(&m_recvQueue, m_recvQueueBuffer, sizeof(m_recvQueueBuffer), nullptr, nullptr);
}

void ServerSocket::startListening(const sockaddr_in& addr) {
	m_listenerSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
	efiPrintf("WiFi: [%s] Listener socket %d for port %d", m_name, m_listenerSocket, _ntohs(addr.sin_port));
	bind(m_listenerSocket, (sockaddr*)&addr, sizeof(addr));
}

void ServerSocket::onAccept(int connectedSocket) {
	// Reject if the handler thread is actively processing a request.
	if (m_busy) {
		efiPrintf("WiFi: [%s] Rejecting sock %d (busy)", m_name, connectedSocket);
		close(connectedSocket);
		return;
	}

	// If a connection is already pending (accepted but not yet handled), reject
	// the new one rather than replacing it.  Replacing would silently drop the
	// pending request before the handler thread has had a chance to serve it —
	// this is the root cause of the "sent back an empty page" browser error.
	// The rejected connection will be retried by the browser once the current
	// one is fully served and m_connectedSocket is cleared.
	if (m_connectedSocket != -1) {
		efiPrintf("WiFi: [%s] Rejecting sock %d (pending %d)", m_name, connectedSocket, (int)m_connectedSocket);
		close(connectedSocket);
		return;
	}

	efiPrintf("WiFi: [%s] Accepting sock %d", m_name, connectedSocket);

	m_connectedSocket = connectedSocket;
	m_sendRequest = false;

	{
		chibios_rt::CriticalSectionLocker csl;
		m_sendDoneSemaphore.resetI(true);
	}

#if !EFI_BOOTLOADER
	m_remainingRecv = 0;
	m_recvActive = false;
#endif

	{
		chibios_rt::CriticalSectionLocker csl;
		iqResetI(&m_recvQueue);
	}
}

bool ServerSocket::closeSocket() {
	bool wasOpen = m_connectedSocket != -1;
	if (wasOpen) {
		efiPrintf("WiFi: [%s] Closing sock %d", m_name, (int)m_connectedSocket);
		close(m_connectedSocket);
		m_connectedSocket = -1;
	}

#if !EFI_BOOTLOADER
	m_recvActive = false;
#endif

	{
		chibios_rt::CriticalSectionLocker csl;
		iqResetI(&m_recvQueue);

		// Any thread waiting on m_sendDoneSemaphore (in send()) needs to be woken up,
		// otherwise they will deadlock forever waiting for a hardware confirmation
		// that will never come for a closed socket.
		// We use resetI(true) to wake them up with MSG_RESET and ensure the
		// semaphore is in a 'taken' state for the next connection.
		m_sendRequest = false;
		m_sendDoneSemaphore.resetI(true);
	}

	return wasOpen;
}

void ServerSocket::onClose() {
	closeSocket();
}

void ServerSocket::onRecv(uint8_t* buffer, size_t recvSize, size_t remaining) {
	{
		chibios_rt::CriticalSectionLocker csl;

		for (size_t i = 0; i < recvSize; i++) {
			iqPutI(&m_recvQueue, buffer[i]);
		}
	}

#if !EFI_BOOTLOADER
	m_remainingRecv = remaining;
	m_recvActive = false; // We finished this recv, wait for checkRecv() to re-arm when space permits
#else
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
#endif
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

	// Wait for this chunk to complete; 5s timeout guards against a driver that
	// never fires SOCKET_MSG_SEND (which would otherwise deadlock this thread).
	msg_t result = m_sendDoneSemaphore.wait(TIME_MS2I(5000));
	if (result != MSG_OK) {
		// Timeout or semaphore reset without a successful send — cancel the
		// pending request and close the socket so callers see isReady()==false.
		m_sendRequest = false;
		closeSocket();
	}
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
		int8_t result = ::send(m_connectedSocket, (void*)m_sendBuffer, m_sendSize, 0);

		if (result == SOCK_ERR_NO_ERROR) {
			m_sendRequest = false;
			return true;
		} else if (result == SOCK_ERR_BUFFER_FULL) {
			// Driver buffers are full, retry on next helper tick
			return false;
		} else {
			// Permanent error?
			efiPrintf("WiFi: [%s] send error %d on sock %d", m_name, (int)result, m_connectedSocket);
			// Wake up the caller so they aren't deadlocked; they'll detect the error on next operation
			closeSocket();
			return true;
		}
	}

	return false;
}

#if !EFI_BOOTLOADER
bool ServerSocket::tryRecvImpl() {
	if (m_connectedSocket != -1 && !m_recvActive) {
		chibios_rt::CriticalSectionLocker csl;
		size_t space = iqGetEmptyI(&m_recvQueue);
		
		size_t nextRecv = m_remainingRecv > 0 ? m_remainingRecv : sizeof(m_recvBuf);
		if (nextRecv > sizeof(m_recvBuf)) nextRecv = sizeof(m_recvBuf);

		// Apply flow control: only request more data if we guarantee we can buffer it!
		if (space >= nextRecv) {
			int8_t result = recv(m_connectedSocket, &m_recvBuf, nextRecv, 0);
			if (result == SOCK_ERR_NO_ERROR) {
				m_recvActive = true;
				return true;
			} else if (result == SOCK_ERR_BUFFER_FULL) {
				// Rare for recv, but retry later
				return false;
			} else {
				efiPrintf("WiFi: [%s] recv error %d on sock %d", m_name, (int)result, m_connectedSocket);
				// Treat as a fatal socket error: close now so the TS thread sees
				// isReady()==false immediately rather than retrying every 10ms.
				closeSocket();
				return false;
			}
		}
	}

	return false;
}

/*static*/ bool ServerSocket::checkRecv() {
	bool result = false;
	auto current = s_serverList;
	while (current) {
		result |= current->tryRecvImpl();
		current = current->m_nextServer;
	}
	return result;
}
#endif

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
				// A backlog of 3 helps handle rapid discovery's from the console's Port Scanner.
				listen(sock, 3);
			}
		} break;
		case SOCKET_MSG_LISTEN: {
			auto listenMsg = reinterpret_cast<tstrSocketListenMsg*>(pvMsg);
			if (listenMsg && listenMsg->status != 0) {
				efiPrintf("WiFi: Listen failed on sock %d with %d", (int)sock, (int)listenMsg->status);
			}
		} break;
		case SOCKET_MSG_ACCEPT: {
			auto acceptMsg = reinterpret_cast<tstrSocketAcceptMsg*>(pvMsg);
			if (acceptMsg && (acceptMsg->sock >= 0)) {
				// Enable TCP keep-alive on the connected socket to detect dead peers
				int keepAlive = 1;
				setsockopt(acceptMsg->sock, SOL_SOCKET, SO_TCP_KEEPALIVE, &keepAlive, sizeof(keepAlive));

				// Use a shorter idle time before keep-alive starts (10 seconds instead of default 60)
				int keepIdle = 20; // units of 500ms
				setsockopt(acceptMsg->sock, SOL_SOCKET, SO_TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));

				if (auto server = ServerSocket::findListener(sock)) {
					server->onAccept(acceptMsg->sock);
				} else {
					efiPrintf("WiFi: No listener for sock %d", (int)sock);
					close(acceptMsg->sock);
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

			bool didWork = ServerSocket::checkSend();
#if !EFI_BOOTLOADER
			didWork |= ServerSocket::checkRecv();
#endif

			if (!didWork) {
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
