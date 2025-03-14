#include "pch.h"
#include "wifi_socket.h"
#include "thread_controller.h"

#include "socket/include/socket.h"

#include "ff.h"

#include <charconv>
#include <string_view>

static NO_CACHE ServerSocket httpSocket;

static struct {
	char buffer[1400];
	FIL sdFile;
	FILINFO sdFileInfo;
} mem SDMMC_MEMORY(4096);

enum class HttpMethod {
	Invalid,
	Get,
	Post,
};

HttpMethod determineMethod(std::string_view firstWord) {
	if (firstWord == "GET") {
		return HttpMethod::Get;
	} else if (firstWord == "POST") {
		return HttpMethod::Post;
	}

	return HttpMethod::Invalid;
}

void sendStr(std::string_view str) {
	httpSocket.send((uint8_t*)str.data(), str.length());
}

static void sendStatusLine(int code, const char* phrase) {
	// char scratch[64];
	// int len = chsnprintf(scratch, sizeof(scratch), "HTTP/1.1 %d %s\r\n", code, phrase);
	// httpSocket.send((uint8_t*)scratch, len);
	sendStr("HTTP/1.1 200 OK\r\n");
}

static void writeContentLength(size_t len) {
	char scratch[10];
	auto res = std::to_chars(scratch, scratch + 10, len);
	sendStr("Content-Length: ");
	httpSocket.send((uint8_t*)scratch, res.ptr - scratch);
	sendStr("\r\n");
}

#include "index.html.h"

static void respondIndexPage() {
	sendStatusLine(200, "OK");

	sendStr(
		"Content-Type: text/html\r\n"
		"Cache-Control: no-store\r\n"
	);

	writeContentLength(sizeof(index_html));

	// End of headers
	sendStr("\r\n");

	// Send content
	httpSocket.send((uint8_t*)index_html, sizeof(index_html));

	httpSocket.onClose();
}

static void respondSdCardFile(std::string_view path) {
	char filename[32];

	for (size_t i = 0; i < path.length(); i++) {
		filename[i] = path[i];
	}

	filename[path.length()] = 0;

	FRESULT err = f_stat(filename, &mem.sdFileInfo);

	if (err == FR_NO_FILE) {
		sendStatusLine(404, "Not found");
	} else if (err != FR_OK) {
		sendStatusLine(500, "File stat error");
	}

	size_t remaining = mem.sdFileInfo.fsize;

	err = f_open(&mem.sdFile, filename, FA_READ);

	if (err == FR_NO_FILE) {
		sendStatusLine(404, "Not found");
	} else if (err != FR_OK) {
		sendStatusLine(500, "File stat error");
	} else {
		sendStatusLine(200, "OK");

		sendStr(
			"Content-Type: application/octet-stream\r\n"
			"Content-Disposition: attachment\r\n"
			"Cache-Control: no-store\r\n"
		);

		writeContentLength(remaining);

		sendStr("\r\n");

		while (remaining) {
			size_t chunkSize = std::min(remaining, sizeof(mem.buffer));

			size_t bytesRead;
			err = f_read(&mem.sdFile, mem.buffer, chunkSize, &bytesRead);

			if (FR_OK != err || chunkSize != bytesRead) {
				// Unexpected read, abort
				break;
			}

			httpSocket.send((uint8_t*)mem.buffer, bytesRead);
			remaining = remaining - chunkSize;
		}
	}

	httpSocket.onClose();
	f_close(&mem.sdFile);
}

static void writeSdCardFile(std::string_view path, std::string_view headers, char* firstChunk, size_t firstChunkSize) {
	size_t remaining;

	do {
		size_t headerEnd = headers.find("\r\n");
		std::string_view header(headers.begin(), headers.begin() + headerEnd);
		headers = std::string_view(headers.begin() + headerEnd + 2, headers.begin() + headers.length() - headerEnd);

		auto keyEnd = header.find(':');

		std::string_view key(header.begin(), header.begin() + keyEnd);

		if (key == "Content-Length") {
			std::from_chars(header.begin() + keyEnd + 2, header.end(), remaining);
			break;
		}
	} while (true);


	char filename[32];

	for (size_t i = 0; i < path.length(); i++) {
		filename[i] = path[i];
	}

	filename[path.length()] = 0;

	FRESULT err = f_open(&mem.sdFile, filename, FA_OPEN_ALWAYS | FA_WRITE);

	if (firstChunkSize) {
		size_t chunkSize = std::min(remaining, firstChunkSize);
		f_write(&mem.sdFile, firstChunk, chunkSize, nullptr);
		remaining = remaining - chunkSize;
	}

	while (remaining) {
		size_t chunkSize = std::min(remaining, sizeof(mem.buffer));

		auto bytesRead = httpSocket.recvTimeout((uint8_t*)mem.buffer, chunkSize, TIME_MS2I(1000));

		if (FR_OK != err || chunkSize != bytesRead) {
			// Unexpected read, abort
			break;
		}

		err = f_write(&mem.sdFile, mem.buffer, chunkSize, nullptr);
		remaining = remaining - chunkSize;
	}

	f_close(&mem.sdFile);

	sendStatusLine(200, "OK");
	httpSocket.onClose();
}

class HttpThread : public ThreadController<4096> {
public:
	HttpThread() : ThreadController("WiFi HTTP", WIFI_THREAD_PRIORITY + 1) {}
	void ThreadTask() override {
		waitForWifiInit();

		// Start listening on the socket
		sockaddr_in address;
		address.sin_family = AF_INET;
		address.sin_port = _htons(80);
		address.sin_addr.s_addr = 0;

		httpSocket.startListening(address);

		while (true) {
			auto len = httpSocket.recvTimeout((uint8_t*)mem.buffer, sizeof(mem.buffer), TIME_MS2I(10));
			if (len == 0) {
				httpSocket.onClose();
				continue;
			}

			std::string_view request(mem.buffer, len);

			auto firstSpace = request.find(' ');
			auto method = determineMethod({request.begin(), request.begin() + firstSpace});
			if (method == HttpMethod::Invalid) {
				httpSocket.onClose();
				continue;
			}

			auto secondSpace = request.find(' ', firstSpace + 1);

			// std::string_view path(request.substr(firstSpace + 1, secondSpace - firstSpace - 1));
			std::string_view path(request.begin() + firstSpace + 1, request.begin() + secondSpace);

			if (method == HttpMethod::Get) {
				if (path == "/") {
					respondIndexPage();
				} else {
					respondSdCardFile(path);
				}
			} else {
				auto headersStart = request.find("\r\n", secondSpace) + 2;
				auto headersEnd = request.find("\r\n\r\n", headersStart + 2);

				std::string_view requestHeaders(request.begin() + headersStart, request.begin() + headersEnd);

				size_t bytesAfterHeaders = len - headersEnd - 4;

				writeSdCardFile("/test.txt", requestHeaders, mem.buffer + headersEnd + 4, bytesAfterHeaders);
			}
		}
	}
};

void initHttp() {
	// STM32H7 SDMMC1 needs the filesystem object to be in AXI
	// SRAM, but excluded from the cache
	#ifdef STM32H7XX
	{
		void* base = &mem;
		static_assert(sizeof(mem) <= 4096);
		uint32_t size = MPU_RASR_SIZE_4K;

		mpuConfigureRegion(MPU_REGION_2,
						base,
						MPU_RASR_ATTR_AP_RW_RW |
						MPU_RASR_ATTR_NON_CACHEABLE |
						MPU_RASR_ATTR_S |
						size |
						MPU_RASR_ENABLE);
		mpuEnable(MPU_CTRL_PRIVDEFENA);

		/* Invalidating data cache to make sure that the MPU settings are taken
		immediately.*/
		SCB_CleanInvalidateDCache();
	}
	#endif

	static HttpThread http;
	http.start();
}
