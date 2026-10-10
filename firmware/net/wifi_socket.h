#pragma once

#include "socket/include/socket.h"

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
	void startListening(const sockaddr_in& addr, int backlog = 1);
	size_t recvTimeout(uint8_t* buffer, size_t size, int timeout);
	void send(const void* buffer, size_t size);
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
	// Block until a client connects, or timeout ms elapses. Returns true if connected.
	bool waitForConnection(int timeoutMs);

	int getBacklog() const {
		return m_backlog;
	}

	static ServerSocket* findListener(int sock);
	static ServerSocket* findConnected(int sock);

	static bool hasAnyConnectedSocket();

private:
	// WiFi thread only: these call in to the driver
	bool trySendImpl();
	void handleRequestsImpl();
	void closeSocketImpl();

	int m_listenerSocket = -1;
	int m_connectedSocket = -1;
	systime_t m_connectedTime = 0;

	// Requests from other threads for the WiFi thread to service
	bool m_listenRequest = false;
	uint16_t m_listenPort = 0;
	uint32_t m_listenAddress = 0;
	int m_backlog = 1;
	volatile bool m_closeRequest = false;

	// TX buffer in NO_CACHE memory (as ServerSocket instances are marked NO_CACHE)
	// so DMA can always safely read from it without cache or bus fault issues.
	uint8_t m_txBuf[SOCKET_BUFFER_MAX_LENGTH];
	const uint8_t* m_sendBuffer = nullptr;
	size_t m_sendSize = 0;
	bool m_sendRequest = false;
	chibios_rt::BinarySemaphore m_sendDoneSemaphore{/* taken =*/true};

	// RX data
	uint8_t m_recvBuf[512];

	uint8_t m_recvQueueBuffer[512];
	input_queue_t m_recvQueue;

	chibios_rt::BinarySemaphore m_acceptSemaphore{/* taken =*/true};

	// Linked list of all server sockets
	static ServerSocket* s_serverList;
	ServerSocket* m_nextServer = nullptr;
};
