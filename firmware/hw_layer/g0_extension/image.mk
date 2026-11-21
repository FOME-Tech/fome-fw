# Included by the main firmware Makefile after the ChibiOS object list exists.
# The ECU bootloader, simulator, and native tests do not bundle this image.
ifeq ($(G0_EXTENSION),yes)
G0_FIRMWARE_DIR := $(PROJECT_DIR)/ext/g0_firmware
G0_PROTOCOL_HEADER := $(G0_FIRMWARE_DIR)/for_fome/g0_spi_protocol.h

# Runtime-only builds still use the companion's wire contract.
$(G0_PROTOCOL_HEADER):
	git -C "$(PROJECT_DIR)/.." submodule update --init --recursive -- firmware/ext/g0_firmware

$(OBJS) $(PCHOBJ): | $(G0_PROTOCOL_HEADER)

endif
