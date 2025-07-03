include $(RUSEFI_LIB)/util/util.mk
RUSEFI_LIB_CPP := $(filter-out $(RUSEFI_LIB)/util/src/crc.cpp \,$(RUSEFI_LIB_CPP))
RUSEFI_LIB_INC += ./util/crc/include

RUSEFI_LIB_CPP += ./util/crc/src/crc.cpp

ifeq ($(SHORT_BOARD_NAME),small-can-board)
HALSRC_CONTRIB += $(filter-out ${CHIBIOS_CONTRIB}/os/hal/src/hal_crc.c, $(HALSRC_CONTRIB))
endif