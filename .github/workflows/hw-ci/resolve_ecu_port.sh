#!/usr/bin/env bash

# Resolves an ECU chip's USB serial-number substring to a /dev/serial/by-id path.
#
# Multiple HW CI runners may share a host, so we never scan all ports. We match
# the chip's iSerial (derived from the STM32 UID) — same SN across firmware and
# OpenBLT bootloader, even though the trailing interface suffix may differ
# (-if00 vs -if01) between modes.
#
# Usage: resolve_ecu_port.sh <serial-substring> [timeout-seconds]
#
# On success prints the resolved path to stdout. On timeout prints diagnostics
# to stderr and exits 1.

set -euo pipefail

SN=${1:?"serial-substring required"}
TIMEOUT=${2:-30}

deadline=$(( $(date +%s) + TIMEOUT ))

while true; do
    match=$(ls /dev/serial/by-id/ 2>/dev/null | grep -F "$SN" | head -n1 || true)
    if [ -n "$match" ]; then
        echo "/dev/serial/by-id/$match"
        exit 0
    fi

    if [ "$(date +%s)" -ge "$deadline" ]; then
        echo "ERROR: no /dev/serial/by-id entry matched '$SN' within ${TIMEOUT}s" >&2
        echo "Visible entries:" >&2
        ls /dev/serial/by-id/ >&2 2>&1 || echo "(directory not present)" >&2
        exit 1
    fi

    sleep 1
done
