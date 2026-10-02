#include "pch.h"

#if EFI_WIFI && EFI_FILE_LOGGING && !defined(EFI_BOOTLOADER)

#include "http_file_server.h"
#include "wifi_socket.h"
#include "socket/include/socket.h"
#include "ff.h"
#include "thread_priority.h"
#include "sd_file_log.h"
#include "dma_buffers.h"
#include <rusefi/crc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HTTP_SERVER_PRIO (NORMALPRIO - 2)

// Structure of PKZip format headers
#pragma pack(push, 1)

struct ZipLocalHeader {
	uint32_t signature = 0x04034b50;
	uint16_t version_needed = 20; // 2.0
	uint16_t flags = 0x0008;	  // Bit 3: Data descriptor used
	uint16_t method = 0;		  // 0 = Store (no compression)
	uint16_t mod_time = 0;
	uint16_t mod_date = 0;
	uint32_t crc32 = 0;
	uint32_t comp_size = 0;
	uint32_t uncomp_size = 0;
	uint16_t name_length = 0;
	uint16_t extra_length = 0;
};

struct ZipDataDescriptor {
	uint32_t signature = 0x08074b50;
	uint32_t crc32 = 0;
	uint32_t comp_size = 0;
	uint32_t uncomp_size = 0;
};

struct ZipCentralHeader {
	uint32_t signature = 0x02014b50;
	uint16_t version_made_by = 20;
	uint16_t version_needed = 20;
	uint16_t flags = 0x0008;
	uint16_t method = 0;
	uint16_t mod_time = 0;
	uint16_t mod_date = 0;
	uint32_t crc32 = 0;
	uint32_t comp_size = 0;
	uint32_t uncomp_size = 0;
	uint16_t name_length = 0;
	uint16_t extra_length = 0;
	uint16_t comment_length = 0;
	uint16_t disk_num_start = 0;
	uint16_t internal_attrs = 0;
	uint32_t external_attrs = 0;
	uint32_t local_header_offset = 0;
};

struct ZipEndOfCentralDir {
	uint32_t signature = 0x06054b50;
	uint16_t disk_num = 0;
	uint16_t cd_disk_num = 0;
	uint16_t disk_entries = 0;
	uint16_t total_entries = 0;
	uint32_t cd_size = 0;
	uint32_t cd_offset = 0;
	uint16_t comment_length = 0;
};

#pragma pack(pop)

struct ZipRecord {
	char name[48];
	uint32_t size;
	uint32_t crc;
	uint32_t offset;
	uint16_t time;
	uint16_t date;
};

#define MAX_ZIP_FILES 128
static ZipRecord zipRecords[MAX_ZIP_FILES];

static NO_CACHE ServerSocket httpServer;
static THD_WORKING_AREA(httpServerStack, 5120);

class BufferedSender {
public:
	BufferedSender(ServerSocket& server)
		: m_server(server)
		, m_pos(0) {}
	~BufferedSender() {
		flush();
	}

	bool send(const void* data, size_t size) {
		const uint8_t* ptr = static_cast<const uint8_t*>(data);
		while (size > 0 && m_server.hasConnectedSocket()) {
			size_t space = sizeof(m_buf) - m_pos;
			if (space == 0) {
				if (!flush()) {
					return false;
				}
				space = sizeof(m_buf);
			}
			size_t chunk = size < space ? size : space;
			memcpy(m_buf + m_pos, ptr, chunk);
			m_pos += chunk;
			ptr += chunk;
			size -= chunk;
		}
		return m_server.hasConnectedSocket();
	}

	bool flush() {
		if (m_pos > 0 && m_server.hasConnectedSocket()) {
			m_server.send(m_buf, m_pos);
			m_pos = 0;
		}
		return m_server.hasConnectedSocket();
	}

	bool hasConnectedSocket() const {
		return m_server.hasConnectedSocket();
	}

private:
	ServerSocket& m_server;
	uint8_t m_buf[SOCKET_BUFFER_MAX_LENGTH];
	size_t m_pos;
};

static bool sendChunk(BufferedSender& sender, const void* data, size_t size) {
	if (!sender.hasConnectedSocket() || size == 0) {
		return false;
	}
	sender.send(data, size);
	return sender.hasConnectedSocket();
}

static bool sendString(BufferedSender& sender, const char* str) {
	return sendChunk(sender, str, strlen(str));
}

static void sendHttpError(BufferedSender& sender, int code, const char* message) {
	char buffer[256];
	snprintf(
			buffer,
			sizeof(buffer),
			"HTTP/1.1 %d %s\r\n"
			"Content-Type: text/html\r\n"
			"Connection: close\r\n\r\n"
			"<html><body><h1>%d %s</h1></body></html>\r\n",
			code,
			message,
			code,
			message);
	sendString(sender, buffer);
}

static void urlDecode(char* dst, const char* src, size_t maxLen) {
	size_t i = 0;
	while (*src && i + 1 < maxLen) {
		if (*src == '%' && src[1] && src[2]) {
			char hex[3] = {src[1], src[2], 0};
			*dst++ = static_cast<char>(strtol(hex, nullptr, 16));
			src += 3;
		} else if (*src == '+') {
			*dst++ = ' ';
			src++;
		} else {
			*dst++ = *src++;
		}
		i++;
	}
	*dst = '\0';
}

static void formatSize(char* buf, size_t bufSize, uint32_t size) {
	if (size < 1024) {
		snprintf(buf, bufSize, "%u B", (unsigned)size);
	} else if (size < 1024 * 1024) {
		snprintf(buf, bufSize, "%u.%u KB", (unsigned)(size / 1024), (unsigned)((size % 1024) * 10 / 1024));
	} else {
		uint32_t mb = size / (1024 * 1024);
		uint32_t rem = (size % (1024 * 1024)) * 10 / (1024 * 1024);
		snprintf(buf, bufSize, "%u.%u MB", (unsigned)mb, (unsigned)rem);
	}
}

static void handleFileDownload(BufferedSender& sender, const char* path, const char* filename, uint32_t fileSize) {
	FIL* file = dma_buffers::httpFileFd();
	uint8_t* ioBuf = dma_buffers::httpFileIoBuffer();
	memset(file, 0, sizeof(FIL));
	FRESULT res = f_open(file, path, FA_READ);
	if (res != FR_OK) {
		sendHttpError(sender, 404, "File Not Found");
		return;
	}

	const char* mime = "application/octet-stream";
	if (strstr(filename, ".txt") || strstr(filename, ".ini")) {
		mime = "text/plain";
	}

	char header[384];
	snprintf(
			header,
			sizeof(header),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: %s\r\n"
			"Content-Length: %lu\r\n"
			"Content-Disposition: attachment; filename=\"%s\"\r\n"
			"Connection: close\r\n\r\n",
			mime,
			(unsigned long)fileSize,
			filename);
	sendString(sender, header);

	while (sender.hasConnectedSocket()) {
		UINT bytesRead = 0;
		if (f_read(file, ioBuf, dma_buffers::HTTP_FILE_IO_BUFFER_SIZE, &bytesRead) != FR_OK || bytesRead == 0) {
			break;
		}
		if (!sendChunk(sender, ioBuf, bytesRead)) {
			break;
		}
	}

	f_close(file);
}

static bool strEqualCi(const char* s1, const char* s2) {
	if (!s1 || !s2) {
		return s1 == s2;
	}
	while (*s1 && *s2) {
		char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + ('a' - 'A')) : *s1;
		char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + ('a' - 'A')) : *s2;
		if (c1 != c2) {
			return false;
		}
		s1++;
		s2++;
	}
	return *s1 == *s2;
}

static bool isFileActiveLog(const char* dirPath, const char* fileName) {
	const char* active = getActiveSdLogFileName();
	if (!active || active[0] == '\0') {
		return false;
	}

	// 1. Direct filename comparison
	if (fileName && strEqualCi(fileName, active)) {
		return true;
	}

	// 2. Active log name might have directory prefix (e.g. "2026/10/01/fome_001.mlg")
	const char* activeBase = strrchr(active, '/');
	if (activeBase) {
		activeBase++;
	} else {
		activeBase = active;
	}

	if (fileName && strEqualCi(fileName, activeBase)) {
		return true;
	}

	// 3. Full path comparison
	if (dirPath) {
		char fullBuf[128];
		if (strcmp(dirPath, "/") == 0) {
			snprintf(fullBuf, sizeof(fullBuf), "%s", fileName ? fileName : "");
		} else {
			const char* dp = (dirPath[0] == '/') ? dirPath + 1 : dirPath;
			snprintf(fullBuf, sizeof(fullBuf), "%s/%s", dp, fileName ? fileName : "");
		}

		const char* act = (active[0] == '/') ? active + 1 : active;
		if (strEqualCi(fullBuf, act)) {
			return true;
		}
	}

	return false;
}

// Check if a filename appears in a comma-separated filter list.
// If filter is null, all files are included ("download all" mode).
static bool isNameSelected(const char* name, const char* filter) {
	if (!filter) {
		return true;
	}
	size_t nameLen = strlen(name);
	const char* p = filter;
	while (*p) {
		const char* comma = strchr(p, ',');
		size_t segLen = comma ? (size_t)(comma - p) : strlen(p);
		if (segLen == nameLen && strncmp(p, name, segLen) == 0) {
			return true;
		}
		p += segLen;
		if (*p == ',') {
			p++;
		}
	}
	return false;
}

static void streamDirectoryAsZip(BufferedSender& sender, const char* dirPath, const char* nameFilter) {
	DIR dir;
	FRESULT res = f_opendir(&dir, dirPath);
	if (res != FR_OK) {
		sendHttpError(sender, 404, "Directory Not Found");
		return;
	}

	// Create a friendly zip filename from path, e.g. /2026/09/29 -> logs_2026_09_29.zip
	char zipName[64];
	const char* p = dirPath;
	while (*p == '/') {
		p++;
	}
	if (*p == 0) {
		strcpy(zipName, "sd_root_logs.zip");
	} else {
		snprintf(zipName, sizeof(zipName), "logs_%s.zip", p);
		for (char* cp = zipName; *cp; cp++) {
			if (*cp == '/') {
				*cp = '_';
			}
		}
	}

	char header[256];
	snprintf(
			header,
			sizeof(header),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: application/zip\r\n"
			"Content-Disposition: attachment; filename=\"%s\"\r\n"
			"Connection: close\r\n\r\n",
			zipName);
	sendString(sender, header);

	uint32_t streamOffset = 0;
	uint16_t fileCount = 0;
	FILINFO fno;

	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0 && fileCount < MAX_ZIP_FILES) {
		if (fno.fname[0] == '.' || (fno.fattrib & AM_DIR)) {
			continue;
		}

		if (isFileActiveLog(dirPath, fno.fname)) {
			continue;
		}

		if (!isNameSelected(fno.fname, nameFilter)) {
			continue;
		}

		char fullPath[128];
		if (strcmp(dirPath, "/") == 0) {
			snprintf(fullPath, sizeof(fullPath), "/%s", fno.fname);
		} else {
			snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, fno.fname);
		}

		FIL* file = dma_buffers::httpFileFd();
		uint8_t* ioBuf = dma_buffers::httpFileIoBuffer();
		memset(file, 0, sizeof(FIL));
		if (f_open(file, fullPath, FA_READ) != FR_OK) {
			continue;
		}

		uint16_t nameLen = strlen(fno.fname);

		// Record metadata for writing the Central Directory at the end
		strncpy(zipRecords[fileCount].name, fno.fname, sizeof(zipRecords[fileCount].name) - 1);
		zipRecords[fileCount].name[sizeof(zipRecords[fileCount].name) - 1] = '\0';
		zipRecords[fileCount].size = fno.fsize;
		zipRecords[fileCount].offset = streamOffset;
		zipRecords[fileCount].time = fno.ftime;
		zipRecords[fileCount].date = fno.fdate;

		// 1. Send Local File Header
		ZipLocalHeader lh;
		lh.name_length = nameLen;
		lh.mod_time = fno.ftime;
		lh.mod_date = fno.fdate;

		sendChunk(sender, &lh, sizeof(lh));
		sendChunk(sender, fno.fname, nameLen);
		streamOffset += sizeof(lh) + nameLen;

		// 2. Stream file content and compute CRC32
		uint32_t crc = 0;
		UINT bytesRead = 0;
		while (sender.hasConnectedSocket()) {
			if (f_read(file, ioBuf, dma_buffers::HTTP_FILE_IO_BUFFER_SIZE, &bytesRead) != FR_OK || bytesRead == 0) {
				break;
			}
			crc = crc32inc(ioBuf, crc, bytesRead);
			sendChunk(sender, ioBuf, bytesRead);
			streamOffset += bytesRead;
		}
		f_close(file);
		zipRecords[fileCount].crc = crc;

		// 3. Send Data Descriptor
		ZipDataDescriptor dd;
		dd.crc32 = crc;
		dd.comp_size = fno.fsize;
		dd.uncomp_size = fno.fsize;

		sendChunk(sender, &dd, sizeof(dd));
		streamOffset += sizeof(dd);

		fileCount++;
	}

	f_closedir(&dir);

	// 4. Send Central Directory
	uint32_t cdStart = streamOffset;
	for (uint16_t i = 0; i < fileCount && sender.hasConnectedSocket(); i++) {
		uint16_t nameLen = strlen(zipRecords[i].name);
		ZipCentralHeader cdHeader;
		cdHeader.mod_time = zipRecords[i].time;
		cdHeader.mod_date = zipRecords[i].date;
		cdHeader.crc32 = zipRecords[i].crc;
		cdHeader.comp_size = zipRecords[i].size;
		cdHeader.uncomp_size = zipRecords[i].size;
		cdHeader.name_length = nameLen;
		cdHeader.local_header_offset = zipRecords[i].offset;

		sendChunk(sender, &cdHeader, sizeof(cdHeader));
		sendChunk(sender, zipRecords[i].name, nameLen);
		streamOffset += sizeof(cdHeader) + nameLen;
	}

	// 5. Send End of Central Directory
	ZipEndOfCentralDir eocd;
	eocd.disk_entries = fileCount;
	eocd.total_entries = fileCount;
	eocd.cd_size = streamOffset - cdStart;
	eocd.cd_offset = cdStart;

	sendChunk(sender, &eocd, sizeof(eocd));
}

static void handleDirectoryListing(BufferedSender& sender, const char* path) {
	DIR dir;
	FRESULT res = f_opendir(&dir, path);
	if (res != FR_OK) {
		sendHttpError(sender, 404, "Directory Not Found");
		return;
	}

	sendString(
			sender,
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html\r\n"
			"Connection: close\r\n\r\n"
			"<!DOCTYPE html><html><head><meta charset='utf-8'>"
			"<meta name='viewport' content='width=device-width,initial-scale=1'>"
			"<title>FOME SD Logs</title><style>"
			"body{font-family:system-ui,-apple-system,sans-serif;background:#18181b;color:#f4f4f5;margin:0;padding:"
			"20px;line-height:1.5}"
			".card{max-width:850px;margin:0 auto;background:#27272a;border-radius:10px;padding:24px;box-shadow:0 4px "
			"6px rgba(0,0,0,0.3)}"
			"h1{font-size:1.5rem;margin:0 0 16px;color:#38bdf8;border-bottom:1px solid #3f3f46;padding-bottom:12px}"
			"a{color:#60a5fa;text-decoration:none}"
			"a:hover{text-decoration:underline}"
			".btn{display:inline-block;padding:8px "
			"16px;background:#2563eb;color:#fff;border-radius:6px;font-weight:600;margin:4px 4px 4px 0;"
			"transition:0.2s;cursor:pointer;border:none;font-size:0.9rem}"
			".btn:hover{background:#1d4ed8;text-decoration:none}"
			".btn:disabled{background:#3f3f46;color:#71717a;cursor:default}"
			"table{width:100%;border-collapse:collapse;margin-top:12px}"
			"th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #3f3f46}"
			"th{background:#3f3f46;color:#d4d4d8}"
			"tr:hover{background:#323238}"
			".dir{color:#facc15;font-weight:600}"
			".size{color:#a1a1aa;text-align:right}"
			"input[type=checkbox]{width:16px;height:16px;accent-color:#2563eb;cursor:pointer}"
			"</style></head><body><div class='card'>");

	char titleBuf[128];
	snprintf(titleBuf, sizeof(titleBuf), "<h1>\xf0\x9f\x93\x81 Index of %s</h1>", path);
	sendString(sender, titleBuf);

	// Action buttons
	sendString(
			sender,
			"<div>"
			"<button class='btn' onclick='dlAll()'>\xf0\x9f\x93\xa6 Download All as .ZIP</button>"
			"<button class='btn' id='dlsel' onclick='dlSel()' disabled>"
			"\xf0\x9f\x93\xa5 Download Selected as .ZIP</button>"
			"<button class='btn' onclick='togAll()' style='background:#3f3f46'>"
			"\xe2\x98\x91 Toggle All</button>"
			"</div>");

	sendString(
			sender, "<table><tr><th style='width:30px'></th><th>Name</th><th style='text-align:right'>Size</th></tr>");

	// Parent directory link if not root
	if (strcmp(path, "/") != 0 && strcmp(path, "") != 0) {
		sendString(
				sender,
				"<tr><td></td><td><a href='../'>\xe2\xac\x85\xef\xb8\x8f [Parent Directory]</a></td>"
				"<td class='size'>-</td></tr>");
	}

	FILINFO fno;
	char rowBuf[320];
	char sizeBuf[32];

	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
		if (fno.fname[0] == '.') {
			continue;
		}

		if (fno.fattrib & AM_DIR) {
			snprintf(
					rowBuf,
					sizeof(rowBuf),
					"<tr><td><input type='checkbox' class='sel' value='%s'></td>"
					"<td><a class='dir' href='%s/'>\xf0\x9f\x93\x81 %s/</a></td>"
					"<td class='size'>-</td></tr>",
					fno.fname,
					fno.fname,
					fno.fname);
		} else {
			formatSize(sizeBuf, sizeof(sizeBuf), fno.fsize);
			if (isFileActiveLog(path, fno.fname)) {
				snprintf(
						rowBuf,
						sizeof(rowBuf),
						"<tr style='color:#a1a1aa'><td></td>"
						"<td>\xe2\x8f\xba\xef\xb8\x8f %s <span "
						"style='color:#38bdf8;font-size:0.85em;font-weight:600'>[Recording - In "
						"Progress]</span></td><td "
						"class='size'>%s</td></tr>",
						fno.fname,
						sizeBuf);
			} else {
				snprintf(
						rowBuf,
						sizeof(rowBuf),
						"<tr><td><input type='checkbox' class='sel' value='%s'></td>"
						"<td><a href='%s'>\xf0\x9f\x93\x84 %s</a></td>"
						"<td class='size'>%s</td></tr>",
						fno.fname,
						fno.fname,
						fno.fname,
						sizeBuf);
			}
		}
		sendString(sender, rowBuf);
	}

	f_closedir(&dir);

	// JavaScript for checkbox handling and download actions
	sendString(
			sender,
			"</table></div>"
			"<script>"
			"function gc(){return document.querySelectorAll('.sel')}"
			"function upd(){var c=gc(),n=0;c.forEach(function(x){if(x.checked)n++});"
			"document.getElementById('dlsel').disabled=n==0}"
			"gc().forEach(function(x){x.onchange=upd});"
			"function dlAll(){location.href='?zip=all'}"
			"function dlSel(){var c=gc(),s=[];"
			"c.forEach(function(x){if(x.checked)s.push(x.value)});"
			"if(s.length)location.href='?zip=items&names='+s.join(',')}"
			"function togAll(){var c=gc(),a=true;"
			"c.forEach(function(x){if(!x.checked)a=false});"
			"c.forEach(function(x){x.checked=!a});upd()}"
			"setInterval(function(){fetch('/ping').catch(function(){})},15000);"
			"</script></body></html>\r\n");
}

static void handleClient(ServerSocket& server) {
	BufferedSender sender(server);
	char reqBuf[256];
	size_t n = 0;
	while (n < sizeof(reqBuf) - 1) {
		uint8_t b;
		if (server.recvTimeout(&b, 1, (n == 0) ? TIME_MS2I(3000) : TIME_MS2I(100)) != 1) {
			break;
		}
		reqBuf[n++] = static_cast<char>(b);
		if (b == '\n') {
			break;
		}
	}
	if (n == 0) {
		return;
	}
	reqBuf[n] = '\0';

	if (!isSdCardLogging()) {
		sendHttpError(sender, 503, "SD Card Logging Inactive");
		return;
	}

	// Extract GET path
	const char* get = strstr(reqBuf, "GET ");
	if (!get) {
		sendHttpError(sender, 400, "Bad Request");
		return;
	}

	const char* pathStart = get + 4;
	const char* pathEnd = strpbrk(pathStart, " \r\n");
	if (!pathEnd) {
		sendHttpError(sender, 400, "Bad Request");
		return;
	}

	char rawPath[128];
	size_t pathLen = pathEnd - pathStart;
	if (pathLen >= sizeof(rawPath)) {
		pathLen = sizeof(rawPath) - 1;
	}
	memcpy(rawPath, pathStart, pathLen);
	rawPath[pathLen] = '\0';

	// Check for query parameters: ?zip=all or ?zip=items&names=file1,file2
	bool isZipAll = false;
	bool isZipItems = false;
	char* nameFilter = nullptr;
	char* query = strchr(rawPath, '?');
	if (query) {
		*query = '\0';
		query++;
		if (strstr(query, "zip=all") != nullptr || strstr(query, "zip=1") != nullptr) {
			isZipAll = true;
		} else if (strstr(query, "zip=items") != nullptr) {
			isZipItems = true;
			char* np = strstr(query, "names=");
			if (np) {
				nameFilter = np + 6;
			}
		}
	}
	bool isZip = isZipAll || isZipItems;

	// URL decode
	char cleanPath[128];
	urlDecode(cleanPath, rawPath, sizeof(cleanPath));

	// Respond to keepalive ping from web UI
	if (strcmp(cleanPath, "/ping") == 0) {
		static const char pingResp[] = "HTTP/1.1 200 OK\r\n"
									   "Content-Type: text/plain\r\n"
									   "Content-Length: 2\r\n"
									   "Connection: close\r\n\r\n"
									   "OK";
		sendString(sender, pingResp);
		return;
	}

	// Fast 404 for browser favicon
	if (strcmp(cleanPath, "/favicon.ico") == 0) {
		sendHttpError(sender, 404, "Not Found");
		return;
	}

	// Prevent directory traversal
	if (strstr(cleanPath, "..") != nullptr) {
		sendHttpError(sender, 403, "Forbidden");
		return;
	}

	// Default to root if empty
	if (cleanPath[0] == '\0') {
		strcpy(cleanPath, "/");
	}

	// Check if path is root
	if (strcmp(cleanPath, "/") == 0) {
		if (isZip) {
			streamDirectoryAsZip(sender, "/", isZipItems ? nameFilter : nullptr);
		} else {
			handleDirectoryListing(sender, "/");
		}
		return;
	}

	// Remove trailing slash for f_stat compatibility
	size_t len = strlen(cleanPath);
	char statPath[128];
	strcpy(statPath, cleanPath);
	if (len > 1 && statPath[len - 1] == '/') {
		statPath[len - 1] = '\0';
	}

	FILINFO fno;
	FRESULT res = f_stat(statPath, &fno);
	if (res != FR_OK) {
		sendHttpError(
				sender, 404, (res == FR_NOT_READY || res == FR_NO_FILESYSTEM) ? "SD Card Not Ready" : "Not Found");
		return;
	}

	if (fno.fattrib & AM_DIR) {
		if (isZip) {
			streamDirectoryAsZip(sender, statPath, isZipItems ? nameFilter : nullptr);
		} else {
			// Ensure directory URLs end with '/' for relative links to resolve properly
			if (cleanPath[len - 1] != '/') {
				char redirBuf[192];
				snprintf(
						redirBuf,
						sizeof(redirBuf),
						"HTTP/1.1 301 Moved Permanently\r\n"
						"Location: %s/\r\n"
						"Connection: close\r\n\r\n",
						cleanPath);
				sendString(sender, redirBuf);
				return;
			}
			handleDirectoryListing(sender, statPath);
		}
	} else {
		// Single file download
		const char* filename = strrchr(statPath, '/');
		if (filename) {
			filename++;
		} else {
			filename = statPath;
		}

		if (isFileActiveLog(statPath, filename)) {
			sendHttpError(sender, 403, "Forbidden - File is currently being recorded");
			return;
		}

		handleFileDownload(sender, statPath, filename, fno.fsize);
	}
}

static THD_FUNCTION(httpServerThread, arg) {
	(void)arg;
	chRegSetThreadName("HTTP Server");

	// Wait until SD card logging is active before attempting to bind port 80.
	// When the ECU is connected to a PC via USB, the SD card is handed to PC
	// as mass storage and internal logging will not run. The web server must not run then.
	while (!isSdCardLogging()) {
		chThdSleepMilliseconds(500);
	}

	// Allow Wi-Fi stack and TS console to settle before binding port 80
	chThdSleepMilliseconds(500);

	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = _htons(80);
	address.sin_addr.s_addr = 0;
	httpServer.startListening(address, 1);

	efiPrintf("HTTP: SD logging active, server listening on port 80");

	while (true) {
		// Block efficiently until a client connects (up to 1s), then handle it.
		// Using a semaphore rather than polling means we respond in microseconds
		// instead of up to 50ms, which matters for browsers with short connection timeouts.
		if (!httpServer.waitForConnection(1000)) {
			continue;
		}

		if (!isSdCardLogging()) {
			BufferedSender sender(httpServer);
			sendHttpError(sender, 503, "SD Logging Inactive");
			httpServer.closeSocket();
			continue;
		}

		handleClient(httpServer);

		// Give the remote peer a chance to close the TCP connection cleanly.
		// This prevents the ATWINC1500 firmware from leaking sockets or wedging
		// if we forcefully close() a socket that still has data in its TX buffers.
		int drainWait = 0;
		while (httpServer.hasConnectedSocket() && drainWait < 20) {
			chThdSleepMilliseconds(100);
			drainWait++;
		}

		httpServer.closeSocket();
	}
}

void startHttpFileServer() {
	static bool started = false;
	if (started) {
		return;
	}
	started = true;

	chThdCreateStatic(httpServerStack, sizeof(httpServerStack), HTTP_SERVER_PRIO, httpServerThread, nullptr);
}

#endif // EFI_WIFI && EFI_FILE_LOGGING && !defined(EFI_BOOTLOADER)
