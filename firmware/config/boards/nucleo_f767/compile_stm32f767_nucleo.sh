#!/bin/bash

# STM32F767 version of the firmware for Nucleo-F767ZI board

SCRIPT_NAME="compile_nucleo_f767.sh"
echo "Entering $SCRIPT_NAME"

# Nucleo boards use MCO signal from St-Link and NOT oscillator - these need STM32_HSE_BYPASS

export USE_FATFS=no

export EXTRA_PARAMS="-DDUMMY -DSTM32F767xx \
 -DEFI_INJECTOR_PIN3=Gpio::Unassigned \
 -DSTM32_HSE_BYPASS=TRUE"
export DEBUG_LEVEL_OPT="-O2"

bash ../common_make.sh nucleo_f767 ARCH_STM32F7
