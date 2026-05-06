#!/usr/bin/env bash

# Verify that an OpenBLT firmware update flashes a viable image.
#
# Assumes OpenOCD has just flashed the bootloader+firmware to the target. We
# wait for the firmware to enumerate, then trigger a reboot into OpenBLT and
# push deliver/fome_update.srec via the bootloader.
#
# Usage: openblt_update.sh <serial-device>

set -euo pipefail

SERIAL_DEVICE=$1

echo "Waiting for firmware to enumerate at $SERIAL_DEVICE"
for _ in $(seq 1 30); do
    if [ -e "$SERIAL_DEVICE" ]; then
        break
    fi
    sleep 1
done

if [ ! -e "$SERIAL_DEVICE" ]; then
    echo "ERROR: serial device $SERIAL_DEVICE never appeared"
    exit 1
fi

SREC=firmware/deliver/fome_update.srec
if [ ! -f "$SREC" ]; then
    echo "ERROR: $SREC not found — did the firmware build run?"
    exit 1
fi

sleep 1

echo "Ports in /dev/serial/by-id:"
ls /dev/serial/by-id

if ! java -cp java_console/autotest/build/libs/autotest-all.jar \
        com.rusefi.HwCiOpenbltUpdate "$SERIAL_DEVICE" "$SREC"; then
    echo "OpenBLT update failed. Ports in /dev/serial/by-id after failure:"
    ls /dev/serial/by-id || echo "(directory not present)"
    exit 1
fi

echo "OpenBLT update completed; allowing the new firmware to boot..."
sleep 5
