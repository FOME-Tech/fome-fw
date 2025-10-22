#include "pch.h"

#include "lwipthread.h"

#include "lwip/sockets.h"

#include "thread_controller.h"
#include "tunerstudio.h"

static int listenerSocket = -1;
static int connectionSocket = -1;

static void do_connection() {
	if (connectionSocket != -1) {
		auto localCopy = connectionSocket;
		connectionSocket = -1;

		lwip_close(localCopy);
	}

	sockaddr_in remote;
	socklen_t size = sizeof(remote);
	connectionSocket = lwip_accept(listenerSocket, (sockaddr*)&remote, &size);
}

class EthernetChannel final : public TsChannelBase {
public:
	EthernetChannel()
		: TsChannelBase("Ethernet")
	{
	}

	bool isReady() const override {
		return connectionSocket != -1;
	}

	void write(const uint8_t* buffer, size_t size, bool /*isEndOfPacket*/) override {
		while (size) {
			size_t remain = TCP_MSS - m_size;
			size_t chunkSize = std::min(size, remain);

			memcpy(m_buffer + m_size, buffer, chunkSize);

			m_size += chunkSize;
			buffer += chunkSize;
			size -= chunkSize;

			if (m_size == TCP_MSS) {
				flush();
			}
		}
	}

	void flush() {
		if (!m_size) {
			// spurious flush
			return;
		}

		lwip_send(connectionSocket, m_buffer, m_size, 0);
		m_size = 0;
	}

	size_t readTimeout(uint8_t* buffer, size_t size, int timeout) override {
		// Convert back to ms
		timeout = TIME_I2MS(timeout);
		int res = lwip_setsockopt(connectionSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		if (res == -1) {
			// something went wrong with the socket, close it and try again
			lwip_close(connectionSocket);
			do_connection();
			return 0;
		}

		auto result = lwip_recv(connectionSocket, buffer, size, /*flags =*/ 0);
		if (result > 0) {
			// success, bytes were returned
			return result;
		} else if (result == 0) {
			// 0 result means the socket is dead
			do_connection();
			return 0;
		} else {
			// negative result means timeout
			return 0;
		}
	}

private:
	uint8_t m_buffer[TCP_MSS];
	size_t m_size = 0;
};

static EthernetChannel ethChannel;

struct EthernetThread : public TunerstudioThread {
	EthernetThread() : TunerstudioThread("Ethernet Console") { }

	TsChannelBase* setupChannel() override {
		lwipInit(nullptr);

		sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_port = htons(29000);
		address.sin_addr.s_addr = INADDR_ANY;

		listenerSocket = lwip_socket(AF_INET, SOCK_STREAM, 0);
		lwip_bind(listenerSocket, (sockaddr*)&address, sizeof(address));
		lwip_listen(listenerSocket, 1);

		do_connection();

		return &ethChannel;
	}
};

static EthernetThread ethernetConsole;

void EthernetConsoleModule::initNoConfiguration() {
	ethernetConsole.start();
}
