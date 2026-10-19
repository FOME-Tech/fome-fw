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

void ServerSocket::startListening(const sockaddr_in& addr, int backlog) {
	// Only the WiFi thread may call in to the driver, so stash the address and let it do the bind
	m_listenPort = addr.sin_port;
	m_listenAddress = addr.sin_addr.s_addr;
	m_backlog = backlog;
	m_listenRequest = true;

	isrSemaphore.signal();
}

void ServerSocket::onAccept(int connectedSocket) {
	// A new connection replaces any existing one: the old peer may have vanished without closing
	// (laptop went to sleep, etc), and if we just forgot about its socket it would never be closed.
	closeSocketImpl();

	m_connectedSocket = connectedSocket;

	recv(m_connectedSocket, &m_recvBuf, 1, 0);

	// Wake any thread that is waiting for a connection
	m_acceptSemaphore.signal();
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

		m_sendRequest = false;
		m_sendDoneSemaphore.resetI(true);
		m_acceptSemaphore.resetI(true);
	}
}

void ServerSocket::onClose() {
	closeSocketImpl();
}

void ServerSocket::onLinkDown() {
	// The client dropped off the network, so nothing using the old link is coming back: drop the
	// connection, and rebuild the listener so the client can connect again when it returns.
	// Called from inside the driver's event handler, so only request it here.
	m_closeRequest = true;

	if (m_listenerSocket != -1) {
		m_listenRequest = true;
	}
}

/*static*/ void ServerSocket::onLinkDownAll() {
	auto current = s_serverList;

	while (current) {
		current->onLinkDown();
		current = current->m_nextServer;
	}
}

/*static*/ void ServerSocket::handleRequests() {
	auto current = s_serverList;

	while (current) {
		current->handleRequestsImpl();
		current = current->m_nextServer;
	}
}

void ServerSocket::handleRequestsImpl() {
	if (m_closeRequest) {
		closeSocketImpl();

		// Clear only once the close is done, closeSocket() is waiting on this
		m_closeRequest = false;
	}

	if (m_listenRequest) {
		m_listenRequest = false;

		// Replacing an existing listener
		if (m_listenerSocket != -1) {
			close(m_listenerSocket);
		}

		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = m_listenPort;
		addr.sin_addr.s_addr = m_listenAddress;

		m_listenerSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
		bind(m_listenerSocket, (sockaddr*)&addr, sizeof(addr));
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

	const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffer);
	while (size > 0 && hasConnectedSocket()) {
		size_t toSend = std::min(size, static_cast<size_t>(SOCKET_BUFFER_MAX_LENGTH));
		memcpy(m_txBuf, ptr, toSend);

		m_sendBuffer = m_txBuf;
		m_sendSize = toSend;
		m_sendRequest = true;

		// Wake the driver to perform the actual send
		isrSemaphore.signal();

		// Wait for the WiFi thread to hand this chunk to the module; 5s timeout guards against
		// a module that never has room for it (which would otherwise deadlock this thread forever).
		msg_t result = m_sendDoneSemaphore.wait(TIME_MS2I(5000));
		if (result == MSG_TIMEOUT) {
			// The module never accepted the data -- cancel the pending request and close
			// so callers see the failure.
			m_sendRequest = false;
			closeSocket();
			break;
		} else if (result != MSG_OK) {
			// The semaphore was reset because the WiFi thread already closed the socket
			// underneath us: nothing left to clean up, but the rest of the data has nowhere to go.
			break;
		}

		ptr += toSend;
		size -= toSend;
	}
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

			// The driver has copied the data out to the module, so the caller can queue its next
			// chunk right away. Don't wait for SOCKET_MSG_SEND: the module only reports that once
			// the peer has ACKed, and a peer that sees a single segment at a time sits on its
			// delayed ACK timer for each one. The module pushes back with SOCK_ERR_BUFFER_FULL.
			m_sendDoneSemaphore.signal();
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

// ---------------------------------------------------------------------------
// STA mode state -- only accessed from the WiFi thread
// ---------------------------------------------------------------------------

// Written by wifiCallback, read by tryOneStaConnect -- both on the WiFi thread
static volatile bool s_staConnectResult = false; // true = M2M_WIFI_CONNECTED
static volatile bool s_staConnectDone = false;	 // set when a terminal connect event arrives
static bool s_connectingSta = false;			 // true while tryOneStaConnect() is active

// Runtime state flags
static bool s_inStaMode = false;				// true while operating as STA (not AP)
static volatile bool s_staDisconnected = false; // set by callback when STA link drops at runtime

static void wifiCallback(uint8 u8MsgType, void* pvMsg) {
	switch (u8MsgType) {
		case M2M_WIFI_RESP_CON_STATE_CHANGED: {
			auto& state = *reinterpret_cast<tstrM2mWifiStateChanged*>(pvMsg);

			if (state.u8CurrState == M2M_WIFI_DISCONNECTED) {
				if (s_connectingSta) {
					// Connect attempt failed -- signal tryOneStaConnect immediately
					efiPrintf("WiFi STA: connect failed (err %d)", (int)state.u8ErrCode);
					s_staConnectResult = false;
					s_staConnectDone = true;
				} else if (s_inStaMode) {
					// Runtime link loss while operating in STA mode
					efiPrintf("WiFi STA: disconnected (err %d)", (int)state.u8ErrCode);
					s_staDisconnected = true;
					ServerSocket::onLinkDownAll();
				} else {
					// AP mode: a client disconnected from our AP
					efiPrintf("WiFi client disconnected");
					ServerSocket::onLinkDownAll();
				}
			} else if (state.u8CurrState == M2M_WIFI_CONNECTED) {
				if (s_connectingSta) {
					efiPrintf("WiFi STA: associated with AP (waiting for DHCP)");
					s_staConnectResult = true;
					s_staConnectDone = true;
				} else if (s_inStaMode) {
					efiPrintf("WiFi STA: reassociated");
				} else {
					efiPrintf("WiFi AP: client connected");
				}
			} else {
				efiPrintf("WiFi con state changed: %d", (int)state.u8CurrState);
			}
		} break;

		case M2M_WIFI_REQ_DHCP_CONF: {
			auto& ipConfig = *reinterpret_cast<tstrM2MIPConfig*>(pvMsg);
			uint8_t* addr = reinterpret_cast<uint8_t*>(&ipConfig.u32StaticIP);
			if (s_inStaMode) {
				efiPrintf("WiFi STA: connected, board IP is %d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
			} else {
				efiPrintf("WiFi AP: client connected, client IP is %d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
			}
		} break;

		case M2M_WIFI_REQ_DHCP_FAILURE: {
			if (s_inStaMode) {
				efiPrintf("WiFi STA: DHCP failed");
				s_staDisconnected = true;
				ServerSocket::onLinkDownAll();
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
					// No server owns this listener -- don't leak the accepted socket
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
			auto sentBytes = reinterpret_cast<sint16*>(pvMsg);
			if (sentBytes && *sentBytes < 0) {
				efiPrintf("WiFi: Send failed with %d on sock %d", (int)*sentBytes, (int)sock);
				if (auto server = ServerSocket::findConnected(sock)) {
					server->onClose();
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

#ifndef EFI_BOOTLOADER

// ---------------------------------------------------------------------------
// STA mode connection logic -- runs on the WiFi thread only
// ---------------------------------------------------------------------------

// Credentials of the currently-connected STA network (for reconnection)
// Sized +1 so a maximum-length (24 char) wifi_string_t retains its null terminator.
static char s_connectedSsid[sizeof(wifi_string_t) + 1] = {};
static char s_connectedPassword[sizeof(wifi_string_t) + 1] = {};

// How long to wait for each network to connect (scan + WPA handshake + DHCP)
static constexpr int STA_CONNECT_TIMEOUT_MS = 10000;
// How many times to retry the primary connected network before cycling
static constexpr int STA_RECONNECT_RETRIES = 3;

/**
 * Attempt to connect to a single SSID. Pumps the driver event loop while waiting.
 * Returns true when M2M_WIFI_CONNECTED arrives, false on disconnect or timeout.
 * Must only be called from the WiFi thread.
 */
static bool tryOneStaConnect(const char* ssid, const char* password) {
	s_connectingSta = true;
	s_staConnectDone = false;
	s_staConnectResult = false;

	size_t ssidLen = std::find(ssid, ssid + sizeof(wifi_string_t), '\0') - ssid;
	size_t passwordLen = std::find(password, password + sizeof(wifi_string_t), '\0') - password;

	tstrNetworkId networkId = {};
	networkId.pu8Bssid = nullptr;
	networkId.pu8Ssid = reinterpret_cast<uint8*>(const_cast<char*>(ssid));
	networkId.u8SsidLen = static_cast<uint8>(ssidLen);
	networkId.enuChannel = M2M_WIFI_CH_ALL;

	sint8 ret;

	if (passwordLen == 0) {
		ret = m2m_wifi_connect_open(WIFI_CRED_DONTSAVE, &networkId);
	} else {
		tstrAuthPsk authPsk = {};
		authPsk.pu8Psk = nullptr;
		authPsk.pu8Passphrase = reinterpret_cast<uint8*>(const_cast<char*>(password));
		authPsk.u8PassphraseLen = static_cast<uint8>(passwordLen);
		ret = m2m_wifi_connect_psk(WIFI_CRED_DONTSAVE, &networkId, &authPsk);
	}

	if (ret != M2M_SUCCESS) {
		efiPrintf("WiFi STA: connect request for \"%s\" failed (ret=%d)", ssid, (int)ret);
		s_connectingSta = false;
		return false;
	}

	// Pump events until we get a definitive result or the timeout expires
	int elapsed = 0;
	while (!s_staConnectDone && elapsed < STA_CONNECT_TIMEOUT_MS) {
		m2m_wifi_handle_events(nullptr);
		chThdSleepMilliseconds(10);
		elapsed += 10;
	}

	if (!s_staConnectDone) {
		efiPrintf("WiFi STA: timeout waiting for \"%s\"", ssid);
		m2m_wifi_disconnect();
		// Drain pending disconnect event while s_connectingSta is still true
		// so it is consumed as part of this attempt and does not trigger s_staDisconnected
		for (int i = 0; i < 20; i++) {
			m2m_wifi_handle_events(nullptr);
			chThdSleepMilliseconds(10);
		}
		s_connectingSta = false;
		return false;
	}

	s_connectingSta = false;
	return s_staConnectResult;
}

/**
 * Iterate configured STA networks. Returns true when one succeeds and
 * populates s_connectedSsid / s_connectedPassword / s_inStaMode.
 */
static bool tryStaConnect() {
	bool anyAttempted = false;
	for (size_t i = 0; i < std::size(config->wifiStaNetworks); i++) {
		const auto& net = config->wifiStaNetworks[i];
		if (net.ssid[0] == '\0') {
			continue;
		}

		anyAttempted = true;
		efiPrintf("WiFi STA: trying network %zu \"%s\"", i + 1, net.ssid);

		if (tryOneStaConnect(net.ssid, net.password)) {
			strncpy(s_connectedSsid, net.ssid, sizeof(s_connectedSsid) - 1);
			s_connectedSsid[sizeof(s_connectedSsid) - 1] = '\0';
			strncpy(s_connectedPassword, net.password, sizeof(s_connectedPassword) - 1);
			s_connectedPassword[sizeof(s_connectedPassword) - 1] = '\0';
			s_inStaMode = true;
			return true;
		}
	}

	if (anyAttempted) {
		efiPrintf("WiFi STA: all configured networks failed, falling back to AP");
	}
	return false;
}

/**
 * Called from the main WiFi thread loop when s_staDisconnected is set.
 * Tries to reconnect: STA_RECONNECT_RETRIES on the same network, then all
 * configured networks, then starts AP as a last resort.
 * Returns true if STA reconnection succeeded.
 */
static bool handleStaReconnect(tstrM2MAPConfig& apConfig) {
	efiPrintf("WiFi STA: link lost, attempting reconnect...");

	// Phase 1: retry the previously-connected network
	for (int attempt = 0; attempt < STA_RECONNECT_RETRIES; attempt++) {
		efiPrintf("WiFi STA: reconnect attempt %d/%d to \"%s\"", attempt + 1, STA_RECONNECT_RETRIES, s_connectedSsid);
		if (tryOneStaConnect(s_connectedSsid, s_connectedPassword)) {
			efiPrintf("WiFi STA: reconnected to \"%s\"", s_connectedSsid);
			s_staDisconnected = false;
			return true;
		}
	}

	// Phase 2: primary network unreachable -- cycle all configured slots
	efiPrintf("WiFi STA: primary unreachable, cycling all configured networks...");
	if (tryStaConnect()) {
		s_staDisconnected = false;
		return true;
	}

	// Phase 3: all options exhausted -- fall back to AP
	efiPrintf("WiFi STA: reconnect failed, falling back to AP mode");
	s_inStaMode = false;
	s_staDisconnected = false;
	m2m_wifi_disconnect();
	for (int i = 0; i < 10; i++) {
		m2m_wifi_handle_events(nullptr);
		chThdSleepMilliseconds(10);
	}
	m2m_wifi_enable_ap(&apConfig);
	return false;
}

#endif // !EFI_BOOTLOADER

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

#ifndef EFI_BOOTLOADER
				// Handle runtime STA disconnection: attempt reconnect or fall back to AP
				if (s_inStaMode && s_staDisconnected) {
					handleStaReconnect(m_apConfig);
				}
#endif // !EFI_BOOTLOADER

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

		m_apConfig = apConfig;

#ifndef EFI_BOOTLOADER
		if (config->wifiHostname[0] != '\0') {
			size_t hostLen = std::find(std::begin(config->wifiHostname), std::end(config->wifiHostname), '\0') -
							 std::begin(config->wifiHostname);
			if (hostLen > 0) {
				if (hostLen >= M2M_DEVICE_NAME_MAX) {
					hostLen = M2M_DEVICE_NAME_MAX - 1;
				}
				efiPrintf("WiFi: setting hostname to \"%.*s\"", (int)hostLen, config->wifiHostname);
				m2m_wifi_set_device_name(reinterpret_cast<uint8*>(config->wifiHostname), static_cast<uint8>(hostLen));
			}
		}

		// Try to join configured STA networks first; fall through to AP if all fail or none configured
		if (tryStaConnect()) {
			// Running in STA mode -- socket layer is all we need
			socketInit();
			registerSocketCallback(socketCallback, nullptr);
			return true;
		}
#endif // !EFI_BOOTLOADER

		// Start AP mode (original behaviour, and STA fallback)
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
	tstrM2MAPConfig m_apConfig = {};
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
