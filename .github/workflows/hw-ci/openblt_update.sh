#!/usr/bin/env bash

# Verify that an OpenBLT firmware update flashes a viable image.
#
# Assumes OpenOCD has just flashed the bootloader+firmware to the target. We
# wait for the firmware to enumerate (matching the chip's USB serial), then
# hand off to HwCiOpenbltUpdate which sends the reboot command, finds the
# bootloader (potentially under a different by-id name with a different -ifNN
# suffix), and pushes deliver/fome_update.srec via XCP.
#
# Usage: openblt_update.sh <serial-substring>

set -euo pipefail

SN=$1

# Wait for any by-id entry that contains the chip serial — confirms the
# firmware is alive on the bus before we attempt to talk to it.
.github/workflows/hw-ci/resolve_ecu_port.sh "$SN" 30 >/dev/null

SREC=firmware/deliver/fome_update.srec
if [ ! -f "$SREC" ]; then
    echo "ERROR: $SREC not found — did the firmware build run?"
    exit 1
fi

echo "Ports in /dev/serial/by-id matching $SN:"
ls /dev/serial/by-id | grep -F "$SN" || echo "(none)"

if ! java -cp java_console/autotest/build/libs/autotest-all.jar \
        com.rusefi.HwCiOpenbltUpdate --skip-reboot "$SN" "$SREC"; then
    echo "OpenBLT update failed. Ports in /dev/serial/by-id after failure:"
    ls /dev/serial/by-id || echo "(directory not present)"
    exit 1
fi

echo "OpenBLT update completed; allowing the new firmware to boot..."
sleep 5
