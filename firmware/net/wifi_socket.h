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
	void startListening(const sockaddr_in& addr, int backlog = 1);
	size_t recvTimeout(uint8_t* buffer, size_t size, int timeout);
	void send(const void* buffer, size_t size);
	bool closeSocket();

	// Calls up from the driver to notify of a change
	void onAccept(int connectedSocket);
	void onClose();
	void onRecv(uint8_t* buffer, size_t recvSize, size_t remaining);
	void onSendDone();
	static bool checkSend();

	bool hasConnectedSocket() const;
	// Block until a client connects, or timeout ms elapses. Returns true if connected.
	bool waitForConnection(int timeoutMs);

	int getBacklog() const {
		return m_backlog;
	}

	static ServerSocket* findListener(int sock);
	static ServerSocket* findConnected(int sock);

	static void closeAllConnected();
	static bool hasAnyConnectedSocket();

	static void checkClose();
	void tryCloseImpl();

private:
	bool trySendImpl();

	int m_listenerSocket = -1;
	int m_connectedSocket = -1;
	int m_pendingCloseSocket = -1;
	systime_t m_connectedTime = 0;
	sockaddr_in m_listenAddr{};
	int m_backlog = 1;

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
