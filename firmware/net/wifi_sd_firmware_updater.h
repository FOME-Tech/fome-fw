#pragma once

void tryUpdateWifiFirmwareFromSd();
void tryDumpWifiFirmwareToSd();

// Signal that SD-based WiFi firmware operations are complete.
// Must be called even if no update was performed, to unblock WiFi init.
void signalWifiSdUpdateComplete();

// Block until SD-based WiFi firmware operations are complete (or skipped).
void waitForWifiSdUpdateComplete();
