#!/bin/bash

set -e

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
FW_DIR=$(readlink -f "$SCRIPT_DIR/../../..")
REPO_DIR=$(readlink -f "$FW_DIR/..")
G0_DIR="$FW_DIR/ext/g0_firmware"

export USE_OPENBLT=yes

if [ -e "$G0_DIR/.git" ]; then
  git -C "$G0_DIR" submodule update --init --recursive
else
  git -C "$REPO_DIR" submodule update --init --recursive firmware/ext/g0_firmware
fi
git -C "$REPO_DIR" submodule update --init --depth=1 firmware/ext/build-tools

UNAME_SM=$(uname -sm)
case "$UNAME_SM" in
  "Darwin "*)
    COMPILER_PLATFORM=arm-gnu-toolchain-11.3.rel1-darwin-x86_64-arm-none-eabi
    ;;
  "Linux x86_64")
    COMPILER_PLATFORM=arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-eabi
    ;;
  *)
    echo "Unsupported compiler platform: $UNAME_SM"
    exit 1
    ;;
esac

G0_TRGT="$FW_DIR/ext/build-tools/$COMPILER_PLATFORM/bin/arm-none-eabi-"
[ -x "${G0_TRGT}g++" ] || { echo "Compiler not found at ${G0_TRGT}g++"; exit 1; }

make -C "$G0_DIR" -j20 TRGT="$G0_TRGT" USE_OPT="-O0 -ggdb -fomit-frame-pointer -falign-functions=16 --specs=nosys.specs" for_fome_image

G0_IMAGE_HEADER="$G0_DIR/for_fome/g0_firmware_image.h"
G0_APP_VERSION=$(sed -nE 's/.*APP_VERSION = ([0-9]+)U;.*/\1/p' "$G0_DIR/source/spi_app_protocol.cpp" | head -n1)
[ -n "$G0_APP_VERSION" ] || { echo "Unable to determine G0 app version"; exit 1; }
if ! grep -q 'build_g0_extension_version' "$G0_IMAGE_HEADER"; then
  printf '\nstatic const unsigned int build_g0_extension_version = %sU;\n' "$G0_APP_VERSION" >> "$G0_IMAGE_HEADER"
fi

cd "$SCRIPT_DIR"
bash ../common_make.sh atlas ARCH_STM32H7
