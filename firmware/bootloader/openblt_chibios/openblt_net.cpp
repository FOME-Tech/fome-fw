#include "pch.h"

#include "wifi_socket.h"
#include "socket/include/socket.h"

extern "C" {
	#include "boot.h"
	#include "net.h"
}

static ServerSocket server;

void NetDeferredInit() {
	initWifi();
	waitForWifiInit();

	// Start listening on the socket
	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = _htons(29000);
	address.sin_addr.s_addr = 0;

	server.startListening(address);
}

uint8_t header[4] = {0xde, 0xad, 0xbe, 0xef};

uint8_t outBuffer[512];

void NetTransmitPacket(blt_int8u *data, blt_int8u len) {
	memcpy(outBuffer + 4, data, len);
	memcpy(outBuffer, header, 4);
	server.send(outBuffer, len + 4);
}

blt_bool NetReceivePacket(blt_int8u *data, blt_int8u *len) {
	*len = server.recvTimeout(data, BOOT_COM_RX_MAX_DATA + 4, TIME_MS2I(100));

	if (*len >= 4) {
		*len -= 4;
		// memcpy(header, data, 4);
		memcpy(data, data + 4, *len);
	} else {
		*len = 0;
	}

	return *len > 0 ? BLT_TRUE : BLT_FALSE;
}

static const wifi_string_t ssid = "FOME Bootloader";
static const wifi_string_t password = "";

const wifi_string_t& getWifiSsid() {
	return ssid;
}

const wifi_string_t& getWifiPassword() {
	return password;
}
