#!/usr/bin/env bash

# Resolves the ECU's current /dev/serial/by-id path (which may differ between
# firmware-mode and bootloader-mode interface suffixes — we always match by
# chip serial), exports it as HARDWARE_CI_SERIAL_DEVICE, then runs the
# requested HW CI suite.
#
# Usage: run_hw_ci.sh <serial-substring> <test-suite>

HW_SN=$1
HW_SUITE=$2

set -euo pipefail

HARDWARE_CI_SERIAL_DEVICE=$(.github/workflows/hw-ci/resolve_ecu_port.sh "$HW_SN" 30)
export HARDWARE_CI_SERIAL_DEVICE
echo "HW CI will use serial port: $HARDWARE_CI_SERIAL_DEVICE"

java -cp java_console/autotest/build/libs/autotest-all.jar "$HW_SUITE"
