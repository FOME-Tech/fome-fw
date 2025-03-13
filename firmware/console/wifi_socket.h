#pragma once

void initWifi();
void waitForWifiInit();

struct sockaddr_in;

class ServerSocket {
public:
	ServerSocket();

	void startListening(const sockaddr_in& addr);

	void onAccept(int connectedSocket);

	void onClose();

	void onRecv(uint8_t* buffer, size_t recvSize, size_t remaining);

	bool hasConnectedSocket() const;

	static bool checkSend();

	void send(uint8_t* buffer, size_t size);

	void onSendDone();

	input_queue_t& recvQueue();

	static ServerSocket* findListener(int sock);
	static ServerSocket* findConnected(int sock);

private:
	bool trySendImpl();

	int m_listenerSocket = -1;
	int m_connectedSocket = -1;

	// TX helper data
	const uint8_t* m_sendBuffer;
	size_t m_sendSize;
	bool m_sendRequest = false;
	chibios_rt::BinarySemaphore m_sendDoneSemaphore{/* taken =*/ true};

	// RX data
	uint8_t m_recvBuf[512];

	uint8_t m_recvQueueBuffer[512];
	input_queue_t m_recvQueue;

	// Linked list of all server sockets
	static ServerSocket* s_serverList;
	ServerSocket* m_nextServer = nullptr;
};
