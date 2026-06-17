/**
 * @file http_file_server.cpp
 * @brief HTTP/1.0 file server for SD card access over WiFi.
 *
 * Provides read-only browsing + file download + file upload via a web browser.
 * Runs in its own ChibiOS thread alongside the TunerStudio WiFi console.
 */

#include "pch.h"

#if EFI_WIFI && !EFI_BOOTLOADER

#include <cstring>
#include "ff.h"
#include "dma_buffers.h"
#include "wifi_socket.h"
#include "thread_controller.h"
#include "mmc_card.h"
#include "socket/include/socket.h"

void waitForTsListening();

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint16_t HTTP_PORT = 80;
static constexpr size_t MAX_PATH_LEN = 128;
// 16 × 512-byte SD sectors: aligns to FatFS sector boundaries so the SDMMC
// DMA can fill the buffer directly without an intermediate copy.
static constexpr size_t FILE_BUF_SIZE = 8192;

// ---------------------------------------------------------------------------
// Static buffers — kept off the stack.  Only one HTTP request at a time.
// ---------------------------------------------------------------------------

struct HttpStorage {
	FIL file;
	uint8_t reqBuf[2048]; // Generous size to handle massive browser headers
	uint8_t fileBuf[FILE_BUF_SIZE];
	uint8_t httpOut[SOCKET_BUFFER_MAX_LENGTH];
};

#if defined(STM32H7XX)
#define s_fil     (dma_buffers::http()->file)
#define s_reqBuf  (dma_buffers::http()->reqBuf)
#define s_fileBuf (dma_buffers::http()->fileBuf)
#define s_httpOut (dma_buffers::http()->httpOut)
#else
static HttpStorage s_storage;
#define s_fil     (s_storage.file)
#define s_reqBuf  (s_storage.reqBuf)
#define s_fileBuf (s_storage.fileBuf)
#define s_httpOut (s_storage.httpOut)
#endif

static char    s_urlPath[MAX_PATH_LEN]; // parsed URL path
static char    s_lineBuf[1024];         // scratch for HTML line generation
static size_t  s_httpOutPos = 0;

// POST body preamble: bytes that were read as part of header parsing
// but actually belong to the request body.
static size_t s_bodyPreambleOffset = 0;
static size_t s_bodyPreambleLen = 0;

// ---------------------------------------------------------------------------
// Buffered HTTP output
// ---------------------------------------------------------------------------

static void httpFlush(ServerSocket& sock) {
	if (s_httpOutPos > 0) {
		sock.send(s_httpOut, s_httpOutPos);
		s_httpOutPos = 0;
	}
}

// Write a string to the output buffer; auto-flushes when full.
static void httpWrite(ServerSocket& sock, const char* str) {
	size_t len = strlen(str);
	while (len > 0) {
		size_t space = sizeof(s_httpOut) - s_httpOutPos;
		size_t chunk = (len < space) ? len : space;
		memcpy(s_httpOut + s_httpOutPos, str, chunk);
		s_httpOutPos += chunk;
		str += chunk;
		len -= chunk;
		if (s_httpOutPos >= sizeof(s_httpOut)) {
			httpFlush(sock);
		}
	}
}

// Convenience: format into s_lineBuf then write.
#define httpWriteFmt(sock, fmt, ...) do { \
	chsnprintf(s_lineBuf, sizeof(s_lineBuf), fmt, ##__VA_ARGS__); \
	httpWrite(sock, s_lineBuf); \
} while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int hex2int(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static void urlDecodeInPlace(char* str) {
	char* pstr = str;
	char* pbuf = str;
	while (*pstr) {
		if (*pstr == '%') {
			if (pstr[1] && pstr[2]) {
				int h = hex2int(pstr[1]);
				int l = hex2int(pstr[2]);
				if (h != -1 && l != -1) {
					*pbuf++ = (char)((h << 4) | l);
					pstr += 3;
					continue;
				}
			}
		} else if (*pstr == '+') {
			*pbuf++ = ' ';
			pstr++;
			continue;
		}
		*pbuf++ = *pstr++;
	}
	*pbuf = '\0';
}

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

static void formatDate(char* out, size_t outLen, WORD fdate, WORD ftime) {
	int year  = ((fdate >> 9) & 0x7F) + 1980;
	int month = (fdate >> 5) & 0x0F;
	int day   = fdate & 0x1F;
	int hour  = (ftime >> 11) & 0x1F;
	int min   = (ftime >> 5) & 0x3F;
	chsnprintf(out, outLen, "%04d-%02d-%02d %02d:%02d",
		year, month, day, hour, min);
}

// ---------------------------------------------------------------------------
// HTTP request parsing
// ---------------------------------------------------------------------------

enum HttpMethod { HTTP_GET, HTTP_POST, HTTP_OTHER };

static HttpMethod s_method;
static uint32_t   s_contentLength;

/**
 * Read and parse an HTTP request.  Sets s_method, s_urlPath,
 * s_contentLength, and the body preamble offset/length.
 *
 * @return true if a valid request was parsed.
 */
static bool readRequest(ServerSocket& sock) {
	size_t total = 0;
	s_contentLength = 0;
	s_bodyPreambleOffset = 0;
	s_bodyPreambleLen = 0;
	s_method = HTTP_OTHER;

	// Read until we see \r\n\r\n or fill the buffer.
	while (total < sizeof(s_reqBuf) - 1) {
		size_t got = sock.recvTimeout(s_reqBuf + total,
			sizeof(s_reqBuf) - 1 - total, TIME_MS2I(3000));
		if (got == 0) break;
		total += got;
		s_reqBuf[total] = '\0';

		if (strstr(reinterpret_cast<char*>(s_reqBuf), "\r\n\r\n")) {
			break;
		}
	}

	if (total == 0) return false;
	s_reqBuf[total] = '\0';

	char* line = reinterpret_cast<char*>(s_reqBuf);

	// Parse method
	if (strncmp(line, "GET ", 4) == 0) {
		s_method = HTTP_GET;
		line += 4;
	} else if (strncmp(line, "POST ", 5) == 0) {
		s_method = HTTP_POST;
		line += 5;
	} else {
		return false;
	}

	// Parse path
	char* pathEnd = strchr(line, ' ');
	if (!pathEnd) return false;
	size_t pathLen = pathEnd - line;
	if (pathLen >= sizeof(s_urlPath)) pathLen = sizeof(s_urlPath) - 1;
	memcpy(s_urlPath, line, pathLen);
	s_urlPath[pathLen] = '\0';

	// Parse Content-Length header
	// Check common capitalizations since strcasestr is unavailable
	const char* clHeader = strstr(reinterpret_cast<char*>(s_reqBuf),
		"Content-Length:");
	if (!clHeader) {
		clHeader = strstr(reinterpret_cast<char*>(s_reqBuf),
			"content-length:");
	}
	if (!clHeader) {
		clHeader = strstr(reinterpret_cast<char*>(s_reqBuf),
			"Content-length:");
	}
	if (clHeader) {
		clHeader += 15; // skip "content-length:"
		while (*clHeader == ' ') clHeader++;
		s_contentLength = (uint32_t)atoi(clHeader);
	}

	// Find the end of headers to identify any body preamble
	char* headerEnd = strstr(reinterpret_cast<char*>(s_reqBuf), "\r\n\r\n");
	if (headerEnd) {
		size_t headerSize = (headerEnd - reinterpret_cast<char*>(s_reqBuf)) + 4;
		if (total > headerSize) {
			s_bodyPreambleOffset = headerSize;
			s_bodyPreambleLen = total - headerSize;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Response generators
// ---------------------------------------------------------------------------

static void sendNotFound(ServerSocket& sock) {
	httpWrite(sock,
		"HTTP/1.0 404 Not Found\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html\r\n\r\n"
		"<html><body><h1>404 Not Found</h1></body></html>\r\n");
	httpFlush(sock);
}

static void sendBadRequest(ServerSocket& sock) {
	httpWrite(sock,
		"HTTP/1.0 400 Bad Request\r\n"
		"Connection: close\r\n\r\n");
	httpFlush(sock);
}

// ---- CSS (kept compact) ----

static const char CSS[] =
	"<style>"
	"*{box-sizing:border-box}"
	"body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:20px;"
	"background:#0f0f1a;color:#d4d4d4}"
	"h1{color:#5eead4;margin:0 0 16px;font-size:1.4em}"
	".bar{display:flex;gap:10px;align-items:center;margin:0 0 14px;flex-wrap:wrap}"
	".btn{background:#1e293b;color:#5eead4;border:1px solid #334155;padding:6px 14px;"
	"border-radius:6px;cursor:pointer;font-size:.85em}"
	".btn:hover{background:#334155}"
	"table{border-collapse:collapse;width:100%}"
	"th,td{text-align:left;padding:8px 12px;border-bottom:1px solid #1e293b}"
	"th{color:#64748b;font-size:.78em;text-transform:uppercase;cursor:pointer;"
	"user-select:none}"
	"th:hover{color:#94a3b8}"
	"tr:hover{background:#1a1a2e}"
	".r{text-align:right;color:#94a3b8;font-variant-numeric:tabular-nums}"
	".d{color:#64748b;font-size:.85em}"
	"a{color:#38bdf8;text-decoration:none}a:hover{text-decoration:underline}"
	".dir{color:#5eead4}"
	"#prog{display:none;margin:8px 0;color:#5eead4;font-size:.85em}"
	".del{color:#ef4444;border-color:#451a1a;padding:4px 8px;margin-left:4px}"
	".del:hover{background:#451a1a}"
	"</style>";

// ---- JavaScript for sorting + upload ----

static const char JS[] =
	"<script>"
	"var sc='n',sd=1;"
	"function srt(c){"
	"if(sc===c)sd=-sd;else{sc=c;sd=1;}"
	"var t=document.getElementById('ft'),"
	"r=Array.from(t.querySelectorAll('tr.f'));"
	"r.sort(function(a,b){"
	"var x=a.dataset[c],y=b.dataset[c];"
	"if(c==='s')return(Number(x)-Number(y))*sd;"
	"return x.localeCompare(y)*sd;"
	"});"
	"r.forEach(function(e){t.appendChild(e);});"
	"}"
	"function upl(){"
	"var f=document.getElementById('uf').files[0];"
	"if(!f)return;"
	"var p=document.getElementById('prog');"
	"p.style.display='block';p.textContent='Uploading '+f.name+'...';"
	"var x=new XMLHttpRequest();"
	"x.open('POST','/upload?dir='+encodeURIComponent(document.getElementById('cd').value)"
	"+'&name='+encodeURIComponent(f.name));"
	"x.upload.onprogress=function(e){if(e.lengthComputable)"
	"p.textContent='Uploading '+f.name+': '+Math.round((e.loaded/e.total)*100)+'%';};"
	"x.onload=function(){p.textContent=x.status===200?'Done!':'Error: '+x.statusText;"
	"setTimeout(function(){location.reload();},800);};"
	"x.onerror=function(){p.textContent='Upload failed (network error)';};"
	"x.send(f);"
	"}"
	"function del(p){"
	"if(!confirm('Delete '+p+'?'))return;"
	"var x=new XMLHttpRequest();"
	"x.open('GET','/delete?path='+encodeURIComponent(p));"
	"x.onload=function(){location.reload();};"
	"x.send();"
	"}"
	"function mkd(){"
	"var n=prompt('New Folder Name:');"
	"if(!n)return;"
	"var x=new XMLHttpRequest();"
	"x.open('GET','/mkdir?dir='+encodeURIComponent(document.getElementById('cd').value)+'&name='+encodeURIComponent(n));"
	"x.onload=function(){location.reload();};"
	"x.send();"
	"}"
	"</script>";

/**
 * Serve a styled HTML directory listing with sort buttons and upload form.
 */
static void serveDirectoryListing(ServerSocket& sock, const char* path,
								  const char* urlPath) {
	DIR dir;
	FILINFO fno;

	char normalizedPath[MAX_PATH_LEN];
	strncpy(normalizedPath, path, sizeof(normalizedPath));
	normalizedPath[sizeof(normalizedPath) - 1] = '\0';
	size_t nlen = strlen(normalizedPath);
	if (nlen > 1 && normalizedPath[nlen - 1] == '/') {
		normalizedPath[nlen - 1] = '\0';
	}

	if (f_opendir(&dir, (nlen > 1) ? normalizedPath : "/") != FR_OK) {
		sendNotFound(sock);
		return;
	}

	// --- HTTP headers ---
	httpWrite(sock,
		"HTTP/1.0 200 OK\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html; charset=utf-8\r\n\r\n");

	// --- HTML head ---
	httpWrite(sock, "<!DOCTYPE html><html><head>"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>FOME SD Card</title>");
	httpWrite(sock, CSS);
	httpWrite(sock, "</head><body>");

	// --- Title ---
	httpWriteFmt(sock, "<h1>FOME SD Card [v3] &mdash; %s</h1>", urlPath);

	// --- Toolbar: parent link + upload ---
	httpWrite(sock, "<div class=\"bar\">");

	if (strlen(urlPath) > 1) {
		char parent[MAX_PATH_LEN];
		strncpy(parent, urlPath, sizeof(parent));
		parent[sizeof(parent) - 1] = '\0';
		size_t plen = strlen(parent);
		if (plen > 1 && parent[plen - 1] == '/') parent[--plen] = '\0';
		char* ls = strrchr(parent, '/');
		if (ls && ls != parent) *(ls + 1) = '\0';
		else strcpy(parent, "/");
		httpWriteFmt(sock,
			"<a class=\"btn\" href=\"%s\">&larr; Up</a>", parent);
	}

	// Upload controls + hidden field with current directory
	httpWriteFmt(sock,
		"<input type=\"hidden\" id=\"cd\" value=\"%s\">", urlPath);
	httpWrite(sock,
		"<input type=\"file\" id=\"uf\" style=\"font-size:.85em\">"
		"<button class=\"btn\" onclick=\"upl()\">Upload</button>"
		"<button class=\"btn\" onclick=\"mkd()\">+ Folder</button>"
		"<button class=\"btn\" onclick=\"location.reload()\">Refresh</button>"
		"</div>"
		"<div id=\"prog\"></div>");

	// --- Table header with sort links ---
	httpWrite(sock,
		"<table><thead><tr>"
		"<th onclick=\"srt('n')\">Name &#x25B4;&#x25BE;</th>"
		"<th onclick=\"srt('d')\" style=\"min-width:120px\">Date &#x25B4;&#x25BE;</th>"
		"<th onclick=\"srt('s')\" class=\"r\">Size &#x25B4;&#x25BE;</th>"
		"<th style=\"width:60px\">Action</th>"
		"</tr></thead><tbody id=\"ft\">");

	// --- Directory entries ---
	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
		const char* activeLog = getActiveLogFileName();
		if (activeLog && strcmp(fno.fname, activeLog) == 0) {
			continue;
		}

		char sizeStr[24];
		char dateStr[20];

		formatDate(dateStr, sizeof(dateStr), fno.fdate, fno.ftime);

		if (fno.fattrib & AM_DIR) {
			httpWriteFmt(sock,
				"<tr class=\"f\" data-n=\"%s\" data-d=\"%s\" data-s=\"-1\">"
				"<td><a class=\"dir\" href=\"%s%s/\">%s/</a></td>"
				"<td class=\"d\">%s</td><td class=\"r\">&mdash;</td>"
				"<td><button class=\"btn del\" onclick=\"del('%s%s/')\">&times;</button></td></tr>",
				fno.fname, dateStr,
				(urlPath[1] == '\0') ? "" : urlPath, fno.fname, fno.fname,
				dateStr,
				(urlPath[1] == '\0') ? "" : urlPath, fno.fname);
		} else {
			formatSize(sizeStr, sizeof(sizeStr), (uint32_t)fno.fsize);
			httpWriteFmt(sock,
				"<tr class=\"f\" data-n=\"%s\" data-d=\"%s\" data-s=\"%u\">"
				"<td><a href=\"%s%s\">%s</a></td>"
				"<td class=\"d\">%s</td><td class=\"r\">%s</td>"
				"<td><button class=\"btn del\" onclick=\"del('%s%s')\">&times;</button></td></tr>",
				fno.fname, dateStr, (unsigned)fno.fsize,
				(urlPath[1] == '\0') ? "" : urlPath, fno.fname, fno.fname,
				dateStr, sizeStr,
				(urlPath[1] == '\0') ? "" : urlPath, fno.fname);
		}
	}

	f_closedir(&dir);

	httpWrite(sock,
		"</tbody></table>"
		"<hr style=\"border-color:#1e293b\">"
		"<p style=\"color:#475569;font-size:.75em\">FOME ECU &middot; HTTP File Server</p>");
	httpWrite(sock, JS);
	httpWrite(sock, "</body></html>");
	httpFlush(sock);
}

/**
 * Serve a file download.
 */
static void serveFile(ServerSocket& sock, const char* path) {
	const char* activeLog = getActiveLogFileName();
	if (activeLog && strstr(path, activeLog)) {
		sendNotFound(sock);
		return;
	}

	memset(&s_fil, 0, sizeof(s_fil));
	if (f_open(&s_fil, path, FA_READ) != FR_OK) {
		sendNotFound(sock);
		return;
	}

	uint32_t fileSize = (uint32_t)f_size(&s_fil);

	// Extract filename from path for Content-Disposition header
	const char* basename = strrchr(path, '/');
	basename = basename ? basename + 1 : path;

	httpWriteFmt(sock,
		"HTTP/1.0 200 OK\r\n"
		"Connection: close\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Disposition: attachment; filename=\"%s\"\r\n"
		"Content-Length: %u\r\n\r\n",
		basename, (unsigned)fileSize);
	httpFlush(sock);

	// Stream file data directly (bypass output buffer for efficiency)
	UINT bytesRead;
	while (fileSize > 0) {
		FRESULT res = f_read(&s_fil, s_fileBuf, sizeof(s_fileBuf), &bytesRead);
		if (res != FR_OK || bytesRead == 0) break;
		// Send in SOCKET_BUFFER_MAX_LENGTH chunks
		size_t sent = 0;
		while (sent < bytesRead) {
			size_t chunk = bytesRead - sent;
			if (chunk > SOCKET_BUFFER_MAX_LENGTH) chunk = SOCKET_BUFFER_MAX_LENGTH;
			sock.send(s_fileBuf + sent, chunk);
			sent += chunk;
		}
		fileSize -= bytesRead;
	}

	f_close(&s_fil);
}

/**
 * Handle a POST file upload.
 * URL format: POST /upload?dir=/&name=filename.ext
 */
static void handleUpload(ServerSocket& sock) {
	// Parse query parameters from s_urlPath: "/upload?dir=/subdir/&name=file.txt"
	char* queryStr = strchr(s_urlPath, '?');
	if (!queryStr || s_contentLength == 0) {
		efiPrintf("HTTP upload: bad request (queryStr=%s, contentLen=%u)",
			queryStr ? "found" : "MISSING", (unsigned)s_contentLength);
		sendBadRequest(sock);
		return;
	}
	queryStr++; // skip '?'

	// Find dir= and name= parameters
	char dirPath[MAX_PATH_LEN] = "/";
	char fileName[64] = "";

	// Simple query string parsing (no URL decoding — filenames are ASCII)
	char* p = queryStr;
	while (p && *p) {
		if (strncmp(p, "dir=", 4) == 0) {
			p += 4;
			char* end = strchr(p, '&');
			size_t len = end ? (size_t)(end - p) : strlen(p);
			if (len >= sizeof(dirPath)) len = sizeof(dirPath) - 1;
			memcpy(dirPath, p, len);
			dirPath[len] = '\0';
			p = end ? end + 1 : nullptr;
		} else if (strncmp(p, "name=", 5) == 0) {
			p += 5;
			char* end = strchr(p, '&');
			size_t len = end ? (size_t)(end - p) : strlen(p);
			if (len >= sizeof(fileName)) len = sizeof(fileName) - 1;
			memcpy(fileName, p, len);
			fileName[len] = '\0';
			p = end ? end + 1 : nullptr;
		} else {
			char* end = strchr(p, '&');
			p = end ? end + 1 : nullptr;
		}
	}

	urlDecodeInPlace(dirPath);
	urlDecodeInPlace(fileName);

	// Reject directory traversal in dirPath
	if (strstr(dirPath, "..")) {
		sendBadRequest(sock);
		return;
	}

	if (fileName[0] == '\0') {
		sendBadRequest(sock);
		return;
	}

	// Reject dangerous filenames
	if (strstr(fileName, "..") || strchr(fileName, '/') || strchr(fileName, '\\')) {
		sendBadRequest(sock);
		return;
	}

	// Build full path: dirPath + fileName
	char fullPath[MAX_PATH_LEN + 64];
	size_t dlen = strlen(dirPath);
	if (dlen > 0 && dirPath[dlen - 1] != '/') {
		chsnprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, fileName);
	} else {
		chsnprintf(fullPath, sizeof(fullPath), "%s%s", dirPath, fileName);
	}

	efiPrintf("HTTP: upload %s (%u bytes)", fullPath, (unsigned)s_contentLength);

	// Open file for writing
	memset(&s_fil, 0, sizeof(s_fil));
	FRESULT fres = f_open(&s_fil, fullPath, FA_CREATE_ALWAYS | FA_WRITE);
	if (fres != FR_OK) {
		efiPrintf("HTTP upload: f_open failed (%d)", (int)fres);
		httpWrite(sock,
			"HTTP/1.0 500 Internal Server Error\r\n"
			"Connection: close\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Failed to create file\r\n");
		httpFlush(sock);
		return;
	}
	efiPrintf("HTTP upload: file opened OK, receiving %u bytes (preamble=%u)",
		(unsigned)s_contentLength, (unsigned)s_bodyPreambleLen);

	uint32_t remaining = s_contentLength;
	uint32_t bytesSinceSync = 0;
	bool writeError = false;

	// First write any body bytes already read during header parsing
	if (s_bodyPreambleLen > 0 && remaining > 0) {
		size_t toWrite = (s_bodyPreambleLen < remaining)
			? s_bodyPreambleLen : remaining;
		UINT written;
		if (f_write(&s_fil, s_reqBuf + s_bodyPreambleOffset,
					toWrite, &written) != FR_OK) {
			writeError = true;
		}
		bytesSinceSync += written;
		remaining -= toWrite;
	}

	// Total timeout calculation: base 10s + 1s per 2.5KB (approx 20kbps minimum speed)
	systime_t startTime = chVTGetSystemTime();
	systime_t timeoutTicks = TIME_MS2I(10000 + (s_contentLength * 10 / 25));

	// Read remaining body from the socket and write to file
	while (remaining > 0 && !writeError) {
		size_t toRead = remaining;
		if (toRead > sizeof(s_fileBuf)) toRead = sizeof(s_fileBuf);

		systime_t elapsed = chVTTimeElapsedSinceX(startTime);
		if (elapsed > timeoutTicks) {
			writeError = true;
			break;
		}

		systime_t remainingTicks = timeoutTicks - elapsed;
		systime_t pktTimeout = TIME_MS2I(10000);
		if (remainingTicks < pktTimeout) pktTimeout = remainingTicks;

		size_t got = sock.recvTimeout(s_fileBuf, toRead, pktTimeout);
		if (got == 0) {
			efiPrintf("HTTP upload: recv timeout, remaining=%u", (unsigned)remaining);
			writeError = true;
			break;
		}

		UINT written;
		FRESULT wres = f_write(&s_fil, s_fileBuf, got, &written);
		if (wres != FR_OK) {
			efiPrintf("HTTP upload: f_write failed (%d)", (int)wres);
			writeError = true;
			break;
		}
		
		bytesSinceSync += written;
		if (bytesSinceSync >= 1024 * 1024) {
			f_sync(&s_fil);
			bytesSinceSync = 0;
		}

		remaining -= got;
	}

	f_sync(&s_fil);
	f_close(&s_fil);

	if (writeError || remaining > 0) {
		efiPrintf("HTTP upload: FAILED (writeErr=%d, remaining=%u)",
			(int)writeError, (unsigned)remaining);
		// Clean up partial file
		f_unlink(fullPath);
		httpWrite(sock,
			"HTTP/1.0 500 Internal Server Error\r\n"
			"Connection: close\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"Write failed or incomplete upload\r\n");
	} else {
		httpWrite(sock,
			"HTTP/1.0 200 OK\r\n"
			"Connection: close\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"OK\r\n");
	}
	httpFlush(sock);
}

// ---------------------------------------------------------------------------
// Request dispatcher
// ---------------------------------------------------------------------------

/**
 * Handle a request to delete a file or directory.
 * URL format: GET /delete?path=/path/to/item
 */
static void handleDelete(ServerSocket& sock) {
	char* path = strstr(s_urlPath, "path=");
	if (!path) {
		sendBadRequest(sock);
		return;
	}
	path += 5; // skip "path="
	urlDecodeInPlace(path);

	// Strip trailing slash — directories are passed as "/dir/" from the UI
	// but f_unlink rejects paths with a trailing slash (FR_INVALID_NAME).
	size_t pathLen = strlen(path);
	if (pathLen > 1 && path[pathLen - 1] == '/') {
		path[pathLen - 1] = '\0';
	}

	efiPrintf("HTTP: deleting %s", path);

	// Security: check active log (cannot delete)
	const char* activeLog = getActiveLogFileName();
	if (activeLog && strstr(path, activeLog)) {
		sendBadRequest(sock); // forbidden
		return;
	}

	FRESULT res = f_unlink(path);
	if (res == FR_OK) {
		httpWrite(sock, "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\nOK");
	} else {
		efiPrintf("HTTP delete failed: %d", (int)res);
		httpWrite(sock, "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\nFailed");
	}
	httpFlush(sock);
}

/**
 * Handle a request to create a directory.
 * URL format: GET /mkdir?dir=/path/&name=foldername
 */
static void handleMkdir(ServerSocket& sock) {
	char* queryStr = strchr(s_urlPath, '?');
	if (!queryStr) {
		sendBadRequest(sock);
		return;
	}
	queryStr++;

	char dirPath[MAX_PATH_LEN] = "/";
	char name[64] = "";

	char* p = queryStr;
	while (p && *p) {
		if (strncmp(p, "dir=", 4) == 0) {
			p += 4;
			char* end = strchr(p, '&');
			size_t len = end ? (size_t)(end - p) : strlen(p);
			if (len >= sizeof(dirPath)) len = sizeof(dirPath) - 1;
			memcpy(dirPath, p, len);
			dirPath[len] = '\0';
			p = end ? end + 1 : nullptr;
		} else if (strncmp(p, "name=", 5) == 0) {
			p += 5;
			char* end = strchr(p, '&');
			size_t len = end ? (size_t)(end - p) : strlen(p);
			if (len >= sizeof(name)) len = sizeof(name) - 1;
			memcpy(name, p, len);
			name[len] = '\0';
			p = end ? end + 1 : nullptr;
		} else {
			char* end = strchr(p, '&');
			p = end ? end + 1 : nullptr;
		}
	}

	urlDecodeInPlace(dirPath);
	urlDecodeInPlace(name);

	char fullPath[MAX_PATH_LEN + 64];
	size_t dlen = strlen(dirPath);
	if (dlen > 0 && dirPath[dlen - 1] != '/') {
		chsnprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, name);
	} else {
		chsnprintf(fullPath, sizeof(fullPath), "%s%s", dirPath, name);
	}

	efiPrintf("HTTP: mkdir %s", fullPath);

	FRESULT res = f_mkdir(fullPath);
	if (res == FR_OK) {
		httpWrite(sock, "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\nOK");
	} else {
		efiPrintf("HTTP mkdir failed: %d", (int)res);
		httpWrite(sock, "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\nFailed");
	}
	httpFlush(sock);
}

static void sendUsbStoragePage(ServerSocket& sock) {
	httpWrite(sock,
		"HTTP/1.0 503 Service Unavailable\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html\r\n\r\n"
		"<html><head>"
		"<meta http-equiv='refresh' content='5'>"
		"<style>"
		"body{font-family:system-ui,sans-serif;background:#0f0f1a;color:#d4d4d4;"
		"display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
		".box{text-align:center;padding:40px;border:1px solid #334155;border-radius:12px}"
		"h1{color:#5eead4}p{color:#94a3b8}"
		"</style></head><body>"
		"<div class='box'>"
		"<h1>SD Card in USB Storage Mode</h1>"
		"<p>The SD card is currently attached to the USB host.</p>"
		"<p>Disconnect the USB cable to use the WiFi file browser.</p>"
		"<p><small>This page refreshes automatically every 5 seconds.</small></p>"
		"</div></body></html>\r\n");
	httpFlush(sock);
}

static void handleRequest(ServerSocket& sock) {
	s_httpOutPos = 0;

	if (!readRequest(sock)) {
		sendBadRequest(sock);
		return;
	}

	// If SD card is in USB MSD mode, FatFS is not available — show informative page
	if (!isSdCardMounted()) {
		sendUsbStoragePage(sock);
		return;
	}

	// --- POST /upload: dispatch BEFORE stripping query string ---
	if (s_method == HTTP_POST) {
		efiPrintf("HTTP: POST %s", s_urlPath);
		if (strncmp(s_urlPath, "/upload", 7) == 0) {
			handleUpload(sock);
		} else {
			sendNotFound(sock);
		}
		return;
	}

	// --- GET requests: match against command endpoints (path may have query string) ---
	if (strncmp(s_urlPath, "/delete", 7) == 0) {
		handleDelete(sock);
		return;
	}
	if (strncmp(s_urlPath, "/mkdir", 6) == 0) {
		handleMkdir(sock);
		return;
	}

	// Strip query string (unused for GET requests)
	char* queryStr = strchr(s_urlPath, '?');
	if (queryStr) {
		*queryStr = '\0';
	}
	urlDecodeInPlace(s_urlPath);

	efiPrintf("HTTP: GET %s", s_urlPath);

	// Security: reject directory traversal
	if (strstr(s_urlPath, "..")) {
		sendNotFound(sock);
		return;
	}

	if (s_method != HTTP_GET) {
		sendBadRequest(sock);
		return;
	}

	const char* fsPath = s_urlPath;
	size_t pathLen = strlen(fsPath);
	bool trailingSlash = (pathLen > 0 && fsPath[pathLen - 1] == '/');

	if (trailingSlash) {
		serveDirectoryListing(sock, fsPath, s_urlPath);
	} else {
		FILINFO fno;
		if (f_stat(fsPath, &fno) == FR_OK) {
			if (fno.fattrib & AM_DIR) {
				httpWriteFmt(sock,
					"HTTP/1.0 301 Moved Permanently\r\n"
					"Location: %s/\r\n"
					"Connection: close\r\n\r\n",
					s_urlPath);
				httpFlush(sock);
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

static NO_CACHE ServerSocket httpServer("HTTP");

class HttpFileServerThread : public ThreadController<8192> {
public:
	HttpFileServerThread()
		: ThreadController("HTTP Files", WIFI_THREAD_PRIORITY - 1) {}

	void ThreadTask() override {
		waitForWifiInit();
		waitForTsListening();  // Serialize: TS socket must finish binding first

		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = _htons(HTTP_PORT);
		addr.sin_addr.s_addr = 0;
		httpServer.startListening(addr);

		efiPrintf("HTTP file server v2 listening on port %d", HTTP_PORT);

		while (true) {
			if (!httpServer.hasConnectedSocket()) {
				// Short poll interval so we notice new connections quickly.
				// Keeping this small also reduces the window during which a
				// second browser connection could arrive and be rejected while
				// the first one sits pending.
				chThdSleepMilliseconds(5);
				continue;
			}

			// Set busy immediately — no sleep before this — so that any
			// connection arriving between now and handleRequest is rejected
			// rather than silently replacing the one we are about to serve.
			httpServer.setBusy(true);
			handleRequest(httpServer);
			httpServer.setBusy(false);
			httpServer.closeSocket();
			chThdSleepMilliseconds(5);
		}
	}
};

static NO_CACHE HttpFileServerThread httpThread;

void startHttpFileServer() {
	httpThread.startThread();
}

#endif // EFI_WIFI && !EFI_BOOTLOADER
