# List of all the board related files.

BOARDCPPSRC = $(BOARD_DIR)/board_configuration.cpp

DDEFS += -DLED_CRITICAL_ERROR_BRAIN_PIN=Gpio::B14
DDEFS += -DSTM32_ADC_USE_ADC3=TRUE

# Enable ethernet
LWIP = no
#include $(PROJECT_DIR)/controllers/modules/ethernet_console/ethernet_console.mk

SHORT_BOARD_NAME = stm32h743_nucleo

DDEFS += -DFIRMWARE_ID=\"nucleo_h743\"
DDEFS += -DDEFAULT_ENGINE_TYPE=MINIMAL_PINS
