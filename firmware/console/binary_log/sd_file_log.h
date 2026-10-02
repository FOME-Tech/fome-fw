#pragma once

void initSdCardLogger();

#if EFI_FILE_LOGGING
bool isSdCardLogging();
const char* getActiveSdLogFileName();
#else
inline bool isSdCardLogging() {
	return false;
}
inline const char* getActiveSdLogFileName() {
	return nullptr;
}
#endif
