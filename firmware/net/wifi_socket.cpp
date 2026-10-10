#include "pch.h"

#if EFI_WIFI

#include "wifi_socket.h"
#include "thread_controller.h"
#include "driver/include/m2m_wifi.h"
#include "socket/include/socket.h"

static chibios_rt::BinarySemaphore isrSemaphore(/* taken =*/true);

/*static*/ ServerSocket* ServerSocket::s_serverList = nullptr;

ServerSocket::ServerSocket() {
	// Add server to linked list
	m_nextServer = s_serverList;
	s_serverList = this;

	// Set up queue
	iqObjectInit(&m_recvQueue, m_recvQueueBuffer, sizeof(m_recvQueueBuffer), nullptr, nullptr);
}

void ServerSocket::startListening(const sockaddr_in& addr) {
	// Only the WiFi thread may call in to the driver, so stash the address and let it do the bind
	m_listenPort = addr.sin_port;
	m_listenAddress = addr.sin_addr.s_addr;
	m_listenRequest = true;

	isrSemaphore.signal();
}

void ServerSocket::onAccept(int connectedSocket) {
	m_connectedSocket = connectedSocket;

	recv(m_connectedSocket, &m_recvBuf, 1, 0);
}

bool ServerSocket::closeSocket() {
	if (!hasConnectedSocket()) {
		return false;
	}

	// Only the WiFi thread may call in to the driver, so ask it to do the close
	m_closeRequest = true;
	isrSemaphore.signal();

	// Wait for the close to happen so the caller sees the socket as closed when we return.
	// Bounded so that a stuck WiFi thread can't take the caller down with it.
	for (size_t i = 0; m_closeRequest && i < 1000; i++) {
		chThdSleepMilliseconds(1);
	}

	return true;
}

void ServerSocket::closeSocketImpl() {
	if (m_connectedSocket != -1) {
		close(m_connectedSocket);
		m_connectedSocket = -1;
	}

	{
		chibios_rt::CriticalSectionLocker csl;
		iqResetI(&m_recvQueue);

		// Any thread waiting on m_sendDoneSemaphore (in send()) needs to be woken up,
		// otherwise it will deadlock forever waiting for a hardware confirmation that
		// will never come for a closed socket. resetI(true) wakes it with MSG_RESET
		// and leaves the semaphore 'taken' for the next connection.
		m_sendRequest = false;
		m_sendDoneSemaphore.resetI(true);
	}
}

void ServerSocket::onClose() {
	closeSocketImpl();
}

/*static*/ void ServerSocket::handleRequests() {
	auto current = s_serverList;

	while (current) {
		current->handleRequestsImpl();
		current = current->m_nextServer;
	}
}

void ServerSocket::handleRequestsImpl() {
	if (m_listenRequest) {
		m_listenRequest = false;

		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = m_listenPort;
		addr.sin_addr.s_addr = m_listenAddress;

		m_listenerSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
		bind(m_listenerSocket, (sockaddr*)&addr, sizeof(addr));
	}

	if (m_closeRequest) {
		closeSocketImpl();

		// Clear only once the close is done, closeSocket() is waiting on this
		m_closeRequest = false;
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

	// Wait for this chunk to complete; 5s timeout guards against a driver that never
	// fires SOCKET_MSG_SEND (which would otherwise deadlock this thread forever).
	msg_t result = m_sendDoneSemaphore.wait(TIME_MS2I(5000));
	if (result == MSG_TIMEOUT) {
		// The driver never confirmed the send — cancel the pending request and close
		// so callers see the failure.
		m_sendRequest = false;
		closeSocket();
	}

	// Otherwise it either worked, or the semaphore was reset because the WiFi thread
	// already closed the socket underneath us: nothing left to clean up in that case.
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
			closeSocketImpl();
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
				// A backlog of 3 helps handle rapid discovery from Tunerstudio's port scanner.
				listen(sock, 3);
			}
		} break;
		case SOCKET_MSG_LISTEN: {
			// accept() is implicit; just surface a failure if listen didn't take
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
					// No server owns this listener — don't leak the accepted socket
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
		if (initWifi()) {
			m_initDone = true;

			while (!m_stopRequest) {
				// The driver is not thread safe: this thread is the only one that may call in to it.
				// Anything other threads need from the driver is requested via a flag and done here.
				ServerSocket::handleRequests();

				{
					ScopePerf perf(PE::WifiHandleEvents);
					m2m_wifi_handle_events(nullptr);
				}

				if (!ServerSocket::checkSend()) {
					isrSemaphore.wait(TIME_MS2I(10));
				}
			}

			m2m_wifi_disable_ap();
			chThdSleepMilliseconds(500);
			m2m_wifi_deinit(nullptr);
		}

		m_exited = true;
	}

	// Shut down WiFi on the WiFi thread, and wait for it to finish
	void stop() {
		m_stopRequest = true;
		isrSemaphore.signal();

		while (!m_exited) {
			chThdSleepMilliseconds(10);
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

		// Disable power save to prevent the AP from freezing or dropping out after periods of inactivity
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
		if (ssid[0] != '\0') {
			strncpy(apConfig.au8SSID, ssid, std::min(sizeof(apConfig.au8SSID), sizeof(ssid)));
		} else {
			// The driver refuses to start the AP with an empty SSID, so use the default name instead
			efiPrintf("WiFi: SSID is blank, using default");
			strcpy(apConfig.au8SSID, "FOME EFI");
		}
		apConfig.u8ListenChannel = 1;
		apConfig.u8SsidHide = 0;

		const wifi_string_t& password = getWifiPassword();
		// strnlen isn't declared in the bootloader build
		size_t keyLength = std::find(std::begin(password), std::end(password), '\0') - std::begin(password);
		// WPA requires at least 8 characters, the driver refuses to start the AP with anything shorter
		constexpr size_t minKeyLength = M2M_MIN_PSK_LEN - 1;
		if (keyLength >= minKeyLength) {
			apConfig.u8SecType = M2M_WIFI_SEC_WPA_PSK;
			apConfig.u8KeySz = keyLength;
			strncpy((char*)apConfig.au8Key, password, std::min(sizeof(apConfig.au8Key), sizeof(password)));
		} else {
			if (keyLength > 0) {
				// An unusable password would otherwise mean no AP at all, leaving USB as the only
				// way back in to fix it. Come up open instead, same as if no password were set.
				efiPrintf("WiFi: password shorter than %d characters, starting open AP", (int)minKeyLength);
			}

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
	volatile bool m_stopRequest = false;
	volatile bool m_exited = false;
};

static NO_CACHE WifiHelperThread wifiHelper;
static bool wifiStarted = false;

void initWifi() {
	wifiStarted = true;
	wifiHelper.startThread();
}

void waitForWifiInit() {
	while (!wifiHelper.initDone()) {
		chThdSleepMilliseconds(10);
	}
}

void stopWifi() {
	if (!wifiStarted) {
		return;
	}

	wifiHelper.stop();
}

#endif
