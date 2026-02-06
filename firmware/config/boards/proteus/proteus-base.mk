# List of all the board related files.
BOARDCPPSRC =  $(BOARD_DIR)/board_configuration.cpp

ifeq ($(PROJECT_CPU),ARCH_STM32F4)
  IS_STM32F429 = yes
endif

# see also openblt/board.mk STATUS_LED
DDEFS += -DLED_CRITICAL_ERROR_BRAIN_PIN=Gpio::E3
DDEFS += $(VAR_DEF_ENGINE_TYPE)

DDEFS += -DEFI_MAIN_RELAY_CONTROL=TRUE

# Turn off stuff proteus doesn't have/need
DDEFS += -DEFI_MAX_31855=TRUE -DBOARD_TLE8888_COUNT=0

# Any Proteus-based adapter boards with discrete-VR decoder are controlled via a 5v ignition output
DDEFS += -DVR_SUPPLY_VOLTAGE=5

DDEFS += -DSTM32_ADC_USE_ADC3=TRUE
DDEFS += -DEFI_SOFTWARE_KNOCK=TRUE

# This stuff doesn't work on H7 yet
ifneq ($(PROJECT_CPU),ARCH_STM32H7)
	DDEFS += -DTRIGGER_SCOPE
endif

# We are running on Proteus hardware!
DDEFS += -DHW_PROTEUS=1 -DHW_POLYGONUS_PRESETS=1
