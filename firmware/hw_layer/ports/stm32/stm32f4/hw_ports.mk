include $(PROJECT_DIR)/hw_layer/ports/stm32/stm32_common.mk

HW_LAYER_EMS += $(HW_STM32_PORT_DIR)/stm32f4/flash/stm32f4xx_hal_flash.c \
				$(HW_STM32_PORT_DIR)/stm32f4/flash/stm32f4xx_hal_flash_ex.c

ALLINC += $(HW_STM32_PORT_DIR)/stm32f4/flash

HW_LAYER_EMS_CPP += $(PROJECT_DIR)/hw_layer/ports/stm32/stm32f4/mpu_util.cpp \
					$(PROJECT_DIR)/hw_layer/ports/stm32/stm32_adc_v2.cpp \
					$(HW_STM32_PORT_DIR)/flash_int_f4_f7.cpp \

MCU_BOOTLOADER_FLASH = openblt_flash_f4_f7.cpp

MCU = cortex-m4
USE_FPU = hard
LDSCRIPT = $(PROJECT_DIR)/hw_layer/ports/stm32/stm32f4/STM32F4.ld
CONFDIR = $(PROJECT_DIR)/hw_layer/ports/stm32/stm32f4/cfg

# STM32F42x has extra memory, so change some flags so we can use it.
ifeq ($(IS_STM32F429),yes)
	USE_OPT += -Wl,--defsym=STM32F4_HAS_SRAM3=1
	DDEFS += -DSTM32F429xx
	DDEFS += -DEFI_IS_F42x
else
	DDEFS += -DSTM32F407xx
endif
