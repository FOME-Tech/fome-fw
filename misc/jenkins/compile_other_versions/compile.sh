#!/bin/bash

# fail on error!
set -euo pipefail

# for example 'config/boards/proteus'
BOARD_DIR="$1"

# for example 'proteus_f4'
export BUNDLE_NAME="$2"

SCRIPT_NAME="compile.sh"
echo "Entering $SCRIPT_NAME with folder $BOARD_DIR and bundle name $BUNDLE_NAME"

[ -n $BOARD_DIR ] || { echo "BOARD_DIR parameter expected"; exit 1; }
[ -n $BUNDLE_NAME ] || { echo "BUNDLE_NAME parameter expected"; exit 1; }

COMPILE_SCRIPT="compile_$BUNDLE_NAME.sh"

cd firmware
rm -rf .dep # ChibiOS build's DEPDIR
rm -rf build # ChibiOS build's BUILDDIR
rm -rf pch/pch.h.gch.sh # what is this?
cd $BOARD_DIR

echo "Invoking $COMPILE_SCRIPT"
bash $COMPILE_SCRIPT
