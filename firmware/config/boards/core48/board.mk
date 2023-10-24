# List of all the board related files.
BOARDCPPSRC =  $(PROJECT_DIR)/config/boards/core48/board_configuration.cpp

BOARDINC = $(PROJECT_DIR)/config/boards/core48

# Override DEFAULT_ENGINE_TYPE
DDEFS += -DSHORT_BOARD_NAME=core48
DDEFS += -DFIRMWARE_ID=\"core48\"
DDEFS += -DDEFAULT_ENGINE_TYPE=engine_type_e::MINIMAL_PINS
DDEFS += -DEFI_SOFTWARE_KNOCK=TRUE -DSTM32_ADC_USE_ADC3=TRUE
