# Included by the main firmware Makefile after the ChibiOS object list exists.
# The ECU bootloader, simulator, and native tests do not bundle this image.
ifeq ($(G0_EXTENSION),yes)
ifeq ($(G0_FIRMWARE_UPDATE),yes)
G0_FIRMWARE_DIR := $(PROJECT_DIR)/ext/g0_firmware
G0_FIRMWARE_BUILD := $(abspath $(BUILDDIR)/g0_firmware)
G0_FIRMWARE_IMAGE := $(G0_FIRMWARE_BUILD)/g0-extension.bin
G0_FIRMWARE_HEADER := $(GENERATED_DIR)/g0_firmware_image.h
G0_IMAGE_GENERATOR := $(PROJECT_DIR)/hw_layer/g0_extension/make_image.py
G0_COMPILER_PREFIX := $(abspath $(TRGT))
PYTHON ?= python3

# Let the nested make inspect its own dependencies on every invocation. Keep its
# objects outside the submodule and use the same pinned ARM compiler as fome-fw.
.PHONY: g0-firmware
g0-firmware:
	git -C "$(PROJECT_DIR)/.." submodule update --init --recursive -- firmware/ext/g0_firmware
	+$(MAKE) -C "$(G0_FIRMWARE_DIR)" all \
		TRGT="$(G0_COMPILER_PREFIX)" \
		BUILDDIR="$(G0_FIRMWARE_BUILD)" DEPDIR="$(G0_FIRMWARE_BUILD)/.dep" \
		USE_OPT="-Os -ggdb -fomit-frame-pointer -falign-functions=16 --specs=nosys.specs"

$(G0_FIRMWARE_HEADER): g0-firmware $(G0_IMAGE_GENERATOR)
	$(PYTHON) "$(G0_IMAGE_GENERATOR)" \
		--input "$(G0_FIRMWARE_IMAGE)" --output "$@" \
		--version-header "$(G0_FIRMWARE_BUILD)/g0_firmware_version.h"

# Missing headers after clean and fresh parallel builds take the same path.
# make_image.py preserves the mtime when the image has not changed, so the normal
# compiler dependency on this header only rebuilds the updater for a new image.
$(OBJS) $(PCHOBJ): | $(G0_FIRMWARE_HEADER)
endif
endif
