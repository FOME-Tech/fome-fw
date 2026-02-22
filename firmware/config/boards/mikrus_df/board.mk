# Mikrus DF board - STM32H743VIT

PROJECT_CPU = ARCH_STM32H7

# List of all the board related files.
BOARDCPPSRC = $(BOARD_DIR)/board_configuration.cpp

# Error LED is red PE3
DDEFS += -DLED_CRITICAL_ERROR_BRAIN_PIN=Gpio::E3

DDEFS += -DFIRMWARE_ID=\"mikrus_df\"
DDEFS += -DDEFAULT_ENGINE_TYPE=MINIMAL_PINS

# ADC3 is needed for AN5 (PC2) and AN6 (PC3) which are ADC3-only on STM32H743.
# No knock sensor on this board - software knock is not enabled.
DDEFS += -DSTM32_ADC_USE_ADC3=TRUE

# SDMMC1 for SD card.
# IMPORTANT: only 1-bit wired (CMD=PD2, CLK=PC12, DAT0=PC8).
# To use 4-bit mode a future hardware revision must also wire PC9/PC10/PC11.
DDEFS += -DEFI_SDC_DEVICE=SDCD1
DDEFS += -DEFI_SDC_MODE=SDC_MODE_1BIT
DDEFS += -DBOARD_OTG_NOVBUSSENS=TRUE
# 32kHz crystal on PC14/PC15 (no bypass - crystal, not external oscillator signal)

# WiFi ATWINC1500 on SPI1
USE_WIFI = yes

# Enable the SD card bootloader
SD_BOOTLOADER = yes

DDEFS += -DHW_MIKRUS_DF=1

SHORT_BOARD_NAME = mikrus_df
