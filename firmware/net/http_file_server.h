#pragma once

/**
 * @file http_file_server.h
 * @brief Minimal HTTP/1.0 file server for browsing and downloading SD card files over WiFi.
 *
 * Serves directory listings as HTML pages and streams file downloads.
 * Listens on port 80 on the ATWINC1500 WiFi AP (e.g. http://192.168.10.1/).
 */

void startHttpFileServer();
