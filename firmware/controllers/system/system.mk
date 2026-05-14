
SYSTEMSRC_CPP =	\
	$(PROJECT_DIR)/controllers/system/efi_output.cpp \
	$(PROJECT_DIR)/controllers/system/injection_gpio.cpp \
	$(PROJECT_DIR)/controllers/system/efi_gpio.cpp \
	$(PROJECT_DIR)/controllers/system/dc_motor.cpp \
	$(PROJECT_DIR)/controllers/system/timer/scheduler.cpp \
	$(PROJECT_DIR)/controllers/system/timer/trigger_scheduler.cpp

ifeq ($(SHORT_BOARD_NAME),atlas)
SYSTEMSRC_CPP += \
	$(PROJECT_DIR)/controllers/system/g0_extension_firmware.cpp \
	$(PROJECT_DIR)/controllers/system/g0_extension_firmware_protocol.cpp \
	$(PROJECT_DIR)/controllers/system/g0_extension_firmware_bootloader.cpp
endif
