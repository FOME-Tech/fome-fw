include $(PROJECT_DIR)/hw_layer/ports/stm32/stm32_common.mk

HW_LAYER_EMS += $(HW_STM32_PORT_DIR)/stm32f7/flash/stm32f7xx_hal_flash.c \
				$(HW_STM32_PORT_DIR)/stm32f7/flash/stm32f7xx_hal_flash_ex.c

ALLINC += $(HW_STM32_PORT_DIR)/stm32f7/flash

HW_LAYER_EMS_CPP += $(PROJECT_DIR)/hw_layer/ports/stm32/stm32f7/mpu_util.cpp \
					$(PROJECT_DIR)/hw_layer/ports/stm32/stm32_adc_v2.cpp \
					$(HW_STM32_PORT_DIR)/flash_int_f4_f7.cpp \

MCU_BOOTLOADER_FLASH = openblt_flash_f4_f7.cpp

# This MCU has a cache, align functions to a cache line for maximum cache efficiency
USE_OPT += -falign-functions=16

DDEFS += -DSTM32F767xx
MCU = cortex-m7
USE_FPU = hard
USE_FPU_OPT = -mfloat-abi=$(USE_FPU) -mfpu=fpv5-d16
LDSCRIPT = $(PROJECT_DIR)/hw_layer/ports/stm32/stm32f7/STM32F7.ld
CONFDIR = $(PROJECT_DIR)/hw_layer/ports/stm32/stm32f7/cfg
