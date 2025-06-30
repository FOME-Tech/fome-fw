#include "pch.h"

#if EFI_WIFI

#include "wifi_socket.h"

#include "socket/include/socket.h"

#include "tunerstudio.h"

class WifiChannel final : public TsChannelBase {
public:
	WifiChannel(ServerSocket& server)
		: TsChannelBase("WiFi")
		, m_server(server)
	{
	}

	bool isReady() const override {
		return m_server.hasConnectedSocket();
	}

	void write(const uint8_t* buffer, size_t size, bool /*isEndOfPacket*/) final override {
		while (size) {
			size_t chunkSize = writeChunk(buffer, size);

			buffer += chunkSize;
			size -= chunkSize;
		}
	}

	void flush() final override {
		if (m_writeSize == 0) {
			// spurious flush, ignore
			return;
		}

		m_server.send(m_writeBuffer, m_writeSize);

		m_writeSize = 0;
	}

	size_t readTimeout(uint8_t* buffer, size_t size, int timeout) override {
		return m_server.recvTimeout(buffer, size, timeout);
	}

private:
	ServerSocket& m_server;

	size_t writeChunk(const uint8_t* buffer, size_t size) {
		// Maximum we can fit in the buffer before a flush
		size_t available = SOCKET_BUFFER_MAX_LENGTH - m_writeSize;

		// Size we will write to the buffer in this chunk
		size_t chunkSize = std::min(size, available);

		// Perform the write!
		memcpy(&m_writeBuffer[m_writeSize], buffer, chunkSize);
		m_writeSize += chunkSize;

		// This write filled the buffer, flush it
		if (m_writeSize == SOCKET_BUFFER_MAX_LENGTH) {
			flush();
		}

		return chunkSize;
	}

	uint8_t m_writeBuffer[SOCKET_BUFFER_MAX_LENGTH];
	size_t m_writeSize = 0;
};

static NO_CACHE ServerSocket tsServer;
static NO_CACHE WifiChannel wifiChannel(tsServer);

static void startTsListening() {
	// Start listening on the socket
	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = _htons(29000);
	address.sin_addr.s_addr = 0;

	tsServer.startListening(address);
}

struct WifiConsoleThread : public TunerstudioThread {
	WifiConsoleThread() : TunerstudioThread("WiFi Console") { }

	TsChannelBase* setupChannel() override {
		waitForWifiInit();

		startTsListening();

		return &wifiChannel;
	}
};

static NO_CACHE WifiConsoleThread wifiThread;

void startWifiConsole() {
	initWifi();

	wifiThread.start();
}

#endif // EFI_WIFI
