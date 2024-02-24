#include "pch.h"

#define EFI_WIFI 1

#if EFI_WIFI

#include "driver/include/m2m_wifi.h"
#include "socket/include/socket.h"

#include "thread_controller.h"
#include "tunerstudio.h"

static int listenerSocket = -1;
static int connectionSocket = -1;

static void do_connection() {
	if (connectionSocket != -1) {
		auto localCopy = connectionSocket;
		connectionSocket = -1;

		close(localCopy);
	}

	connectionSocket = accept(listenerSocket, nullptr, 0);
}


class WifiChannel : public TsChannelBase {
public:
	WifiChannel()
		: TsChannelBase("WiFi")
	{
	}

	bool isReady() const override {
		return false;
	}

	void write(const uint8_t* buffer, size_t size, bool /*isEndOfPacket*/) override {
		// If not the end of a packet, set the MSG_MORE flag to indicate to the transport
		// that we have more to add to the buffer before queuing a flush.
		// auto flags = isEndOfPacket ? 0 : MSG_MORE;
		send(connectionSocket, const_cast<uint8_t*>(buffer), size, 0);
	}

	size_t readTimeout(uint8_t* buffer, size_t size, int timeout) override {
		auto result = recv(connectionSocket, buffer, size, timeout);

		if (result != 0) {
			do_connection();
			return 0;
		}

		return size;
	}
};

static WifiChannel wifiChannel;

struct WifiThread : public TunerstudioThread {
	WifiThread() : TunerstudioThread("WiFi Console") { }

	TsChannelBase* setupChannel() override {
		sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_port = _htons(29000);
		address.sin_addr.s_addr = 0;

		listenerSocket = socket(AF_INET, SOCK_STREAM, 0);
		bind(listenerSocket, (sockaddr*)&address, sizeof(address));
		listen(listenerSocket, 1);

		do_connection();

		return &wifiChannel;
	}
};

static WifiThread wifiThread;

static tstrWifiInitParam param;

static tstrM2MAPConfig apConfig;

void startWifiConsole() {
	m2m_wifi_init(&param);

	strcpy(apConfig.au8SSID, "WINC_SSID");
	apConfig.u8ListenChannel 	= 1;
	apConfig.u8SecType			= M2M_WIFI_SEC_OPEN;
	apConfig.u8SsidHide			= 0;
	
	// IP Address
	apConfig.au8DHCPServerIP[0]	= 192;
	apConfig.au8DHCPServerIP[1]	= 168;
	apConfig.au8DHCPServerIP[2]	= 1;
	apConfig.au8DHCPServerIP[3]	= 1;
	
	// Trigger AP
	m2m_wifi_enable_ap(&apConfig);

	m2m_wifi_handle_events(nullptr);

	wifiThread.start();
}

#endif // EFI_WIFI
