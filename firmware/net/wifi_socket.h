#pragma once

void initWifi();
void waitForWifiInit();
void stopWifi();

const wifi_string_t& getWifiSsid();
const wifi_string_t& getWifiPassword();

struct sockaddr_in;

class ServerSocket {
public:
	ServerSocket();

	// User functions: listen, recv, send, close
	// These may be called from any thread. The WiFi driver is not thread safe, so anything here that
	// needs the driver is handed off to the WiFi thread rather than done on the caller's thread.
	void startListening(const sockaddr_in& addr);
	size_t recvTimeout(uint8_t* buffer, size_t size, int timeout);
	void send(uint8_t* buffer, size_t size);
	bool closeSocket();

	// Calls up from the driver to notify of a change (WiFi thread only)
	void onAccept(int connectedSocket);
	void onClose();
	void onLinkDown();
	static void onLinkDownAll();
	void onRecv(uint8_t* buffer, size_t recvSize, size_t remaining);
	void onSendDone();
	static bool checkSend();
	static void handleRequests();

	bool hasConnectedSocket() const;

	static ServerSocket* findListener(int sock);
	static ServerSocket* findConnected(int sock);

private:
	// WiFi thread only: these call in to the driver
	bool trySendImpl();
	void handleRequestsImpl();
	void closeSocketImpl();

	int m_listenerSocket = -1;
	int m_connectedSocket = -1;

	// Requests from other threads for the WiFi thread to service
	bool m_listenRequest = false;
	uint16_t m_listenPort = 0;
	uint32_t m_listenAddress = 0;
	volatile bool m_closeRequest = false;

	// TX helper data
	const uint8_t* m_sendBuffer;
	size_t m_sendSize;
	bool m_sendRequest = false;
	chibios_rt::BinarySemaphore m_sendDoneSemaphore{/* taken =*/true};

	// RX data
	uint8_t m_recvBuf[512];

	uint8_t m_recvQueueBuffer[512];
	input_queue_t m_recvQueue;

	// Linked list of all server sockets
	static ServerSocket* s_serverList;
	ServerSocket* m_nextServer = nullptr;
};
