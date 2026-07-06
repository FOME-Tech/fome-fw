#!/bin/bash

export EXTRA_PARAMS="-DEFI_LUA=FALSE"
export DEBUG_LEVEL_OPT="-O0 -ggdb -g"

# export USE_OPENBLT=yes

bash ../common_make.sh microrusefi ARCH_STM32F4
