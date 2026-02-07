#!/bin/bash

PROJECT_BOARD=$1
PROJECT_CPU=$2

# fail on error
set -e

SCRIPT_NAME="common_make.sh"
echo "Entering $SCRIPT_NAME with board $1 and CPU $2"
BOARD_DIR=$(pwd)
echo "Board dir is $BOARD_DIR"

# Back out to the firmware root, relative to this script's location, as it may be called
# from outside the firmware tree.
FW_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )/../..
FW_DIR=$(readlink -f $FW_DIR)
echo "FW dir is $FW_DIR"
cd $FW_DIR

mkdir -p .dep # ChibiOS build's DEPDIR
mkdir -p build # ChibiOS build's BUILDDIR
echo "Calling make for the main firmware..."
make make -j$(( $(nproc) * 3 / 2 )) -r PROJECT_BOARD=$PROJECT_BOARD PROJECT_CPU=$PROJECT_CPU BOARD_DIR=$BOARD_DIR
[ -e build/fome.hex ] || { echo "FAILED to compile by $SCRIPT_NAME with $PROJECT_BOARD $DEBUG_LEVEL_OPT and $EXTRA_PARAMS"; exit 1; }
if [ "${USE_OPENBLT-no}" = "yes" ]; then
  # TODO: why is this rm necessary?
  rm -f pch/pch.h.gch/*
  rm engine_modules_generated*
  rm modules_list_generated*
  echo "Calling make for the bootloader..."
  cd bootloader; make make -j$(( $(nproc) * 3 / 2 )) PROJECT_BOARD=$PROJECT_BOARD PROJECT_CPU=$PROJECT_CPU BOARD_DIR=$BOARD_DIR; cd ..
  [ -e bootloader/blbuild/fome_bl.hex ] || { echo "FAILED to compile OpenBLT by $SCRIPT_NAME with $PROJECT_BOARD"; exit 1; }
fi

if uname | grep "NT"; then
  export HEX2DFU=../misc/encedo_hex2dfu/hex2dfu.exe
else
  export HEX2DFU=../misc/encedo_hex2dfu/hex2dfu.bin
fi
chmod u+x $HEX2DFU

mkdir -p deliver
rm -rf deliver/*
mkdir -p deliver/temp

# Add a checksum to the main firmware image
$FW_DIR/config/boards/write_firmware_checksum.sh build/fome.elf deliver/temp/main_firmware

if [ "$USE_OPENBLT" = "yes" ]; then
  # this image is suitable for update through bootloader only
  # srec is the only format used by OpenBLT host tools
  cp deliver/temp/main_firmware.srec deliver/fome_update.srec
else
  # standalone image (for use with no bootloader)
  cp deliver/temp/main_firmware.bin deliver/fome.bin
fi

# bootloader and combined image
if [ "$USE_OPENBLT" = "yes" ]; then
  # Add a checksum to the bootloader image
  $FW_DIR/config/boards/write_firmware_checksum.sh bootloader/blbuild/fome_bl.elf deliver/temp/bootloader

  cp deliver/temp/bootloader.srec deliver/fome_bl.srec

  echo "$SCRIPT_NAME: invoking hex2dfu for OpenBLT"

  echo "$SCRIPT_NAME: invoking hex2dfu for combined OpenBLT+FOME image"
  $HEX2DFU -i deliver/temp/bootloader.hex -i deliver/temp/main_firmware.hex -b deliver/fome.bin
fi

echo "$SCRIPT_NAME: deliver folder content:"
ls -lh deliver
