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

void NetTransmitPacket(blt_int8u *data, blt_int8u len) {
	server.send(data, len);
}

blt_bool NetReceivePacket(blt_int8u *data, blt_int8u *len) {
	return len == server.recvTimeout(data, len, TIME_MS2I(1000));
}
