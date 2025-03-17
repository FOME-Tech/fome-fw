# Atlas is STM32H743
PROJECT_CPU = ARCH_STM32H7

# List of all the board related files.
BOARDCPPSRC =  $(BOARD_DIR)/board_configuration.cpp
DDEFS += -DEFI_MAIN_RELAY_CONTROL=TRUE

DDEFS += -DLED_CRITICAL_ERROR_BRAIN_PIN=Gpio::F0
DDEFS += -DFIRMWARE_ID=\"atlas\"

# This stuff doesn't work on H7 yet
# This board has trigger scope hardware!
# DDEFS += -DTRIGGER_SCOPE
DDEFS += -DEFI_SOFTWARE_KNOCK=TRUE -DSTM32_ADC_USE_ADC3=TRUE

DDEFS += -DEFI_SDC_DEVICE=SDCD1

# Atlas's LSE runs in bypass mode (doesn't use OSC IN pin)
DDEFS += -DSTM32_LSE_BYPASS

# We are running on Atlas hardware!
DDEFS += -DHW_ATLAS=1

# Atlas has WiFi
USE_WIFI = yes

# Enable the SD card bootloader
SD_BOOTLOADER = yes

SHORT_BOARD_NAME = atlas
