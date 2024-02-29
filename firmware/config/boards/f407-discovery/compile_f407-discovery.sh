#!/bin/bash

export USE_OPENBLT=yes
export EXTRA_PARAMS="-DDUMMY -DSHORT_BOARD_NAME=f407-discovery"

bash ../common_make.sh ARCH_STM32F4
