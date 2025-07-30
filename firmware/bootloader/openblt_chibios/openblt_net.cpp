#include "pch.h"

#include "wifi_socket.h"
#include "socket/include/socket.h"

extern "C" {
	#include "boot.h"
	#include "net.h"
}

static ServerSocket server;

static bool didInit = false;

void NetDeferredInit() {
	if (didInit) {
		return;
	}

	didInit = true;

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
	memcpy(outBuffer + 1, data, len);
	outBuffer[0] = len;
	server.send(outBuffer, len + 1);
}

blt_bool NetReceivePacket(blt_int8u *data, blt_int8u *len) {
	uint8_t lengthByte;

	auto lengthByteLen = server.recvTimeout(&lengthByte, 1, TIME_MS2I(1000));

	if (lengthByteLen == 0 || lengthByte == 0) {
		return BLT_FALSE;
	}

	*len = server.recvTimeout(data, lengthByte, TIME_MS2I(10));

	if (*len != lengthByte) {
		return BLT_FALSE;
	}

	return BLT_TRUE;
}

static const wifi_string_t ssid = "FOME Bootloader";
static const wifi_string_t password = "";

const wifi_string_t& getWifiSsid() {
	return ssid;
}

const wifi_string_t& getWifiPassword() {
	return password;
}

void DoWifiDisconnect() {
	if (!didInit) {
		return;
	}

	if (server.closeSocket()) {
		// The socket was open, let the message get out before we reset WiFi
		chThdSleepMilliseconds(500);

		// Stop WiFi so it comes up cleanly in the main firmware
		stopWifi();
	}
}
