# Combine the related files for a specific platform and MCU.

# Target ECU board design
BOARDCPPSRC = $(BOARD_DIR)/board_configuration.cpp

BOARDINC  = $(BOARD_DIR)

# see also openblt/board.mk STATUS_LED
DDEFS += -DLED_CRITICAL_ERROR_BRAIN_PIN=Gpio::E3

# maybe a way to disable SPI2 privately
#DDEFS += -DSTM32_SPI_USE_SPI2=FALSE

DDEFS += -DFIRMWARE_ID=\"microRusEFI\"
DDEFS += -DEFI_SOFTWARE_KNOCK=TRUE -DSTM32_ADC_USE_ADC3=TRUE
DDEFS += $(VAR_DEF_ENGINE_TYPE)

# We are running on microRusEFI hardware!
DDEFS += -DHW_MICRO_RUSEFI=1

ifeq ($(PROJECT_CPU),ARCH_STM32F7)
SHORT_BOARD_NAME = mre_f7
else ifeq ($(PROJECT_CPU),ARCH_STM32F4)
SHORT_BOARD_NAME = mre_f4
else ifewq ($(PROJECT_CPU),simulator)
SHORT_BOARD_NAME = mre_simulator
else
$(error Unsupported PROJECT_CPU for microRusEFI: [$(PROJECT_CPU)])
endif
