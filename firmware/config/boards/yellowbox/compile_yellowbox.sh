#!/bin/bash

export USE_OPENBLT=yes

# Uncomment for debug builds (no optimization, all variables visible in debugger)
# Revert to release before flashing production firmware.
# export DEBUG_LEVEL_OPT="-O0 -ggdb -g"
# export USE_LTO=no

bash ../common_make.sh yellowbox ARCH_STM32H7
