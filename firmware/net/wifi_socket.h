#pragma once

void initWifi();
void waitForWifiInit();
void stopWifi();

const wifi_string_t& getWifiSsid();
const wifi_string_t& getWifiPassword();

struct sockaddr_in;

class ServerSocket {
public:
	ServerSocket(const char* name);

	// User functions: listen, recv, send, close
	void startListening(const sockaddr_in& addr);
	size_t recvTimeout(uint8_t* buffer, size_t size, int timeout);
	void send(uint8_t* buffer, size_t size);
	bool closeSocket();

	// When busy, onAccept rejects new connections to prevent
	// clobbering the active connection mid-send (e.g. browser
	// opening a parallel connection for /favicon.ico).
	void setBusy(bool busy) { m_busy = busy; }

	// Calls up from the driver to notify of a change
	void onAccept(int connectedSocket);
	void onClose();
	void onRecv(uint8_t* buffer, size_t recvSize, size_t remaining);
	void onSendDone();
	static bool checkSend();
#if !EFI_BOOTLOADER
	static bool checkRecv();
#endif

	bool hasConnectedSocket() const;

	static ServerSocket* findListener(int sock);
	static ServerSocket* findConnected(int sock);

private:
	bool trySendImpl();
#if !EFI_BOOTLOADER
	bool tryRecvImpl();
#endif

	int m_listenerSocket = -1;
	int m_connectedSocket = -1;
	volatile bool m_busy = false;

	// TX helper data
	const uint8_t* m_sendBuffer;
	size_t m_sendSize;
	bool m_sendRequest = false;
	chibios_rt::BinarySemaphore m_sendDoneSemaphore{/* taken =*/true};

	// RX data
	uint8_t m_recvBuf[512];

#if !EFI_BOOTLOADER
	uint8_t m_recvQueueBuffer[2048]; // Generous 2KB queue for flow control
#else
	uint8_t m_recvQueueBuffer[512];
#endif
	input_queue_t m_recvQueue;

#if !EFI_BOOTLOADER
	size_t m_remainingRecv = 0;
	bool m_recvActive = false;
#endif

	// Linked list of all server sockets
	static ServerSocket* s_serverList;
	ServerSocket* m_nextServer = nullptr;
	const char* m_name;
};
