ALLCPPSRC += \
	$(PROJECT_DIR)/net/wifi_socket.cpp \
	# $(PROJECT_DIR)/net/wifi_firmware_updater.cpp \

ifeq ($(findstring EFI_BOOTLOADER,$(DDEFS)),)
ALLCPPSRC += $(PROJECT_DIR)/net/http_file_server.cpp
endif

ALLINC += \
	$(PROJECT_DIR)/net \
