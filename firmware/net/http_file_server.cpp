/**
 * @file http_file_server.cpp
 * @brief Minimal HTTP/1.0 file server for SD card access over WiFi.
 *
 * Provides read-only access to the SD card filesystem via a web browser.
 *   - GET / or GET /subdir/   → HTML directory listing
 *   - GET /file.mlg           → file download (binary stream)
 *
 * Uses the existing ServerSocket / ATWINC1500 infrastructure and FatFS.
 * Runs in its own ChibiOS thread alongside the TunerStudio WiFi console.
 */

#include "pch.h"

#if EFI_WIFI && !EFI_BOOTLOADER

#include "http_file_server.h"
#include "wifi_socket.h"
#include "thread_controller.h"

#include "socket/include/socket.h"
#include "ff.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint16_t HTTP_PORT = 80;

// Maximum length for the request path extracted from the HTTP request line.
static constexpr size_t MAX_PATH_LEN = 128;

// Size of the buffer used for reading file data from SD and sending over WiFi.
// Must not exceed SOCKET_BUFFER_MAX_LENGTH (1400) imposed by the ATWINC1500.
static constexpr size_t FILE_BUF_SIZE = SOCKET_BUFFER_MAX_LENGTH;

// ---------------------------------------------------------------------------
// HTTP response helpers
// ---------------------------------------------------------------------------

// Send a string (without the NUL terminator) on the connected socket.
static void httpSend(ServerSocket& sock, const char* str) {
	sock.send(reinterpret_cast<uint8_t*>(const_cast<char*>(str)), strlen(str));
}

// Send a fixed-size buffer.
static void httpSendBuf(ServerSocket& sock, uint8_t* buf, size_t len) {
	// The ATWINC1500 limits each send() to SOCKET_BUFFER_MAX_LENGTH bytes.
	while (len > 0) {
		size_t chunk = (len > SOCKET_BUFFER_MAX_LENGTH) ? SOCKET_BUFFER_MAX_LENGTH : len;
		sock.send(buf, chunk);
		buf += chunk;
		len -= chunk;
	}
}

// Format a file size into a human-readable string (e.g. "1.2 MB").
static void formatSize(char* out, size_t outLen, uint32_t bytes) {
	if (bytes < 1024) {
		chsnprintf(out, outLen, "%u B", (unsigned)bytes);
	} else if (bytes < 1024u * 1024u) {
		unsigned kb10 = (unsigned)((bytes * 10u) / 1024u);
		chsnprintf(out, outLen, "%u.%u KB", kb10 / 10, kb10 % 10);
	} else {
		unsigned mb10 = (unsigned)((bytes * 10u) / (1024u * 1024u));
		chsnprintf(out, outLen, "%u.%u MB", mb10 / 10, mb10 % 10);
	}
}

// ---------------------------------------------------------------------------
// HTTP request reading
// ---------------------------------------------------------------------------

/**
 * Read the HTTP request line and extract the method and path.
 *
 * We only care about "GET /path HTTP/1.x".
 * After reading the request line we consume all remaining headers until
 * the blank line (\r\n\r\n), so the socket is ready for the response.
 *
 * @return true if a valid GET request was parsed.
 */
static bool readRequest(ServerSocket& sock, char* pathOut, size_t pathOutLen) {
	// Read enough data to contain the full request headers.
	// HTTP requests from browsers are typically well under 1 KB.
	uint8_t buf[512];
	size_t total = 0;

	// Keep reading until we see "\r\n\r\n" (end of headers) or fill the buffer.
	while (total < sizeof(buf) - 1) {
		size_t got = sock.recvTimeout(buf + total, sizeof(buf) - 1 - total, TIME_MS2I(2000));
		if (got == 0) {
			break; // timeout or connection closed
		}
		total += got;
		buf[total] = '\0';

		// Check for end of HTTP headers
		if (strstr(reinterpret_cast<char*>(buf), "\r\n\r\n")) {
			break;
		}
	}

	if (total == 0) {
		return false;
	}
	buf[total] = '\0';

	// Parse "METHOD /path HTTP/1.x\r\n"
	char* line = reinterpret_cast<char*>(buf);

	// Must start with "GET "
	if (strncmp(line, "GET ", 4) != 0) {
		return false;
	}

	char* pathStart = line + 4;
	char* pathEnd = strchr(pathStart, ' ');
	if (!pathEnd) {
		return false;
	}

	size_t pathLen = pathEnd - pathStart;
	if (pathLen >= pathOutLen) {
		pathLen = pathOutLen - 1;
	}
	memcpy(pathOut, pathStart, pathLen);
	pathOut[pathLen] = '\0';

	return true;
}

// ---------------------------------------------------------------------------
// Response generators
// ---------------------------------------------------------------------------

static void sendNotFound(ServerSocket& sock) {
	httpSend(sock,
		"HTTP/1.0 404 Not Found\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html\r\n"
		"\r\n"
		"<html><body><h1>404 Not Found</h1></body></html>\r\n");
}

static void sendMethodNotAllowed(ServerSocket& sock) {
	httpSend(sock,
		"HTTP/1.0 405 Method Not Allowed\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html\r\n"
		"\r\n"
		"<html><body><h1>405 Method Not Allowed</h1></body></html>\r\n");
}

/**
 * Serve an HTML directory listing for the given FatFS path.
 *
 * @param sock   connected ServerSocket
 * @param path   FatFS directory path, e.g. "/" or "/logs"
 * @param urlPath  the URL path the client requested, for building links
 */
static void serveDirectoryListing(ServerSocket& sock, const char* path, const char* urlPath) {
	DIR dir;
	FILINFO fno;

	if (f_opendir(&dir, path) != FR_OK) {
		sendNotFound(sock);
		return;
	}

	// Send HTTP headers — we don't know Content-Length for a directory listing
	// so we rely on Connection: close to signal end of body.
	httpSend(sock,
		"HTTP/1.0 200 OK\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"\r\n");

	// HTML head
	httpSend(sock,
		"<!DOCTYPE html><html><head>"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>FOME SD Card</title>"
		"<style>"
		"body{font-family:system-ui,sans-serif;margin:2em;background:#1a1a2e;color:#e0e0e0}"
		"h1{color:#0ff}a{color:#4fc3f7;text-decoration:none}"
		"a:hover{text-decoration:underline}"
		"table{border-collapse:collapse;width:100%;max-width:700px}"
		"th,td{text-align:left;padding:6px 14px;border-bottom:1px solid #333}"
		"th{color:#888;font-size:0.85em;text-transform:uppercase}"
		"tr:hover{background:#222244}"
		".sz{text-align:right;font-variant-numeric:tabular-nums;color:#aaa}"
		"</style></head><body>");

	// Title
	char line[256];
	chsnprintf(line, sizeof(line), "<h1>&#128193; %s</h1>", urlPath);
	httpSend(sock, line);

	// "Up" link if not at root
	if (strlen(urlPath) > 1) {
		// Compute parent path
		char parent[MAX_PATH_LEN];
		strncpy(parent, urlPath, sizeof(parent));
		parent[sizeof(parent) - 1] = '\0';
		size_t plen = strlen(parent);
		// Remove trailing slash
		if (plen > 1 && parent[plen - 1] == '/') {
			parent[plen - 1] = '\0';
			plen--;
		}
		// Find last slash
		char* lastSlash = strrchr(parent, '/');
		if (lastSlash && lastSlash != parent) {
			*(lastSlash + 1) = '\0';
		} else {
			strcpy(parent, "/");
		}
		chsnprintf(line, sizeof(line), "<p><a href=\"%s\">&#11168; Parent directory</a></p>", parent);
		httpSend(sock, line);
	}

	httpSend(sock, "<table><tr><th>Name</th><th class=\"sz\">Size</th></tr>");

	// Enumerate directory entries
	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
		char sizeStr[24];

		if (fno.fattrib & AM_DIR) {
			// Directory entry — link with trailing slash
			chsnprintf(line, sizeof(line),
				"<tr><td>&#128193; <a href=\"%s%s/\">%s/</a></td><td class=\"sz\">-</td></tr>",
				urlPath, fno.fname, fno.fname);
		} else {
			formatSize(sizeStr, sizeof(sizeStr), (uint32_t)fno.fsize);
			chsnprintf(line, sizeof(line),
				"<tr><td>&#128196; <a href=\"%s%s\">%s</a></td><td class=\"sz\">%s</td></tr>",
				urlPath, fno.fname, fno.fname, sizeStr);
		}
		httpSend(sock, line);
	}

	f_closedir(&dir);

	httpSend(sock, "</table><hr><p style=\"color:#666;font-size:0.8em\">FOME ECU &middot; HTTP File Server</p></body></html>");
}

/**
 * Serve a file download for the given FatFS path.
 */
static void serveFile(ServerSocket& sock, const char* path) {
	FIL fil;

	if (f_open(&fil, path, FA_READ) != FR_OK) {
		sendNotFound(sock);
		return;
	}

	// Get file size
	uint32_t fileSize = (uint32_t)f_size(&fil);

	// Send HTTP headers
	char header[192];
	chsnprintf(header, sizeof(header),
		"HTTP/1.0 200 OK\r\n"
		"Connection: close\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %u\r\n"
		"\r\n",
		(unsigned)fileSize);
	httpSend(sock, header);

	// Stream file data
	uint8_t buf[FILE_BUF_SIZE];
	UINT bytesRead;
	while (fileSize > 0) {
		FRESULT res = f_read(&fil, buf, sizeof(buf), &bytesRead);
		if (res != FR_OK || bytesRead == 0) {
			break;
		}
		httpSendBuf(sock, buf, bytesRead);
		fileSize -= bytesRead;
	}

	f_close(&fil);
}

// ---------------------------------------------------------------------------
// Request dispatcher
// ---------------------------------------------------------------------------

/**
 * Handle a single HTTP request on the connected socket.
 */
static void handleRequest(ServerSocket& sock) {
	char urlPath[MAX_PATH_LEN];

	if (!readRequest(sock, urlPath, sizeof(urlPath))) {
		// Bad or non-GET request
		sendMethodNotAllowed(sock);
		return;
	}

	// URL-decode is intentionally skipped — file names on the SD card are
	// ASCII and the browser will send them unencoded in most cases.

	// Determine the FatFS path.
	// The SD card is mounted at "/", and FatFS paths don't use a leading
	// slash on some configs, but with FF_FS_RPATH=0 absolute paths work.
	// We'll use the URL path directly since it starts with "/".
	const char* fsPath = urlPath;

	// Security: reject paths containing ".." to prevent directory traversal.
	if (strstr(fsPath, "..")) {
		sendNotFound(sock);
		return;
	}

	// Check if path points to a directory or a file.
	// A trailing '/' is a strong hint it's a directory.
	size_t pathLen = strlen(fsPath);
	bool trailingSlash = (pathLen > 0 && fsPath[pathLen - 1] == '/');

	if (trailingSlash) {
		// Definitely a directory request
		serveDirectoryListing(sock, fsPath, urlPath);
	} else {
		// Try to stat the path to determine if it's a file or directory
		FILINFO fno;
		if (f_stat(fsPath, &fno) == FR_OK) {
			if (fno.fattrib & AM_DIR) {
				// It's a directory — redirect to add trailing slash
				char redirect[256];
				chsnprintf(redirect, sizeof(redirect),
					"HTTP/1.0 301 Moved Permanently\r\n"
					"Location: %s/\r\n"
					"Connection: close\r\n"
					"\r\n",
					urlPath);
				httpSend(sock, redirect);
			} else {
				serveFile(sock, fsPath);
			}
		} else {
			sendNotFound(sock);
		}
	}
}

// ---------------------------------------------------------------------------
// Server thread
// ---------------------------------------------------------------------------

static NO_CACHE ServerSocket httpServer;

class HttpFileServerThread : public ThreadController<4096> {
public:
	HttpFileServerThread()
		: ThreadController("HTTP Files", WIFI_THREAD_PRIORITY - 1) {}

	void ThreadTask() override {
		// Wait for WiFi to be fully initialized
		waitForWifiInit();

		// Bind and listen on port 80
		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = _htons(HTTP_PORT);
		addr.sin_addr.s_addr = 0;
		httpServer.startListening(addr);

		efiPrintf("HTTP file server listening on port %d", HTTP_PORT);

		while (true) {
			if (!httpServer.hasConnectedSocket()) {
				// No client connected — wait a bit and check again
				chThdSleepMilliseconds(50);
				continue;
			}

			// We have a connected client — handle one request
			handleRequest(httpServer);

			// Close the connection (HTTP/1.0 — one request per connection)
			httpServer.closeSocket();
		}
	}
};

static NO_CACHE HttpFileServerThread httpThread;

void startHttpFileServer() {
	httpThread.startThread();
}

#endif // EFI_WIFI && !EFI_BOOTLOADER
