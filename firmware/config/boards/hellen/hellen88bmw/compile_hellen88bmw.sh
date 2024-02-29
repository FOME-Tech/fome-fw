#!/bin/bash

cd ..

export EXTRA_PARAMS="-DDUMMY \
-DEFI_SOFTWARE_KNOCK=TRUE -DSTM32_ADC_USE_ADC3=TRUE \
"

bash ../common_make.sh ARCH_STM32F4
