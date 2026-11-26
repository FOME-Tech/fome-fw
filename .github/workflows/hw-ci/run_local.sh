#!/usr/bin/env bash

# Run HW CI against an ECU on your desk: the same steps as hardware-ci.yaml (build, wipe and flash
# the bootloader over SWD, update the firmware via OpenBLT, run the suite), with switches to skip
# steps while iterating.
#
# Usage: .github/workflows/hw-ci/run_local.sh <board> [options] [-- TestClass[#method] ...]
#
# Boards: atlas, proteus_f4, f407-discovery
#
# Options:
#   --no-build      don't rebuild the firmware, flash what's in firmware/deliver
#   --no-flash      don't flash, test whatever the ECU is running
#   --swd           flash the full image (bootloader + firmware) over SWD and skip the OpenBLT
#                   update step. Quicker, and works if the OpenBLT path is broken
#   --ecu-serial SN chip USB serial; by default the one FOME ECU in /dev/serial/by-id
#   --suite CLASS   run this suite instead of the board's own
#   -- TESTS...     run only these test classes/methods via com.rusefi.HwCiRun, e.g.
#                   -- MiscTest  or  -- MiscTest#testSetDate
#
# Examples:
#   .github/workflows/hw-ci/run_local.sh atlas                       # full run, like CI
#   .github/workflows/hw-ci/run_local.sh atlas --no-build --swd      # reflash last build
#   .github/workflows/hw-ci/run_local.sh atlas --no-build --no-flash -- MiscTest#burn
#
# Uses any ST-Link it finds, unlike CI which pins the probe's serial.
#
# WSL: the ECU re-enumerates on every reset and on the jump to/from OpenBLT, so it has to be
# attached with 'usbipd attach --auto-attach --busid <id> --wsl' on the Windows side, or it drops
# out of WSL at the first reboot. Auto-attach occasionally sticks ("Attach Request ... failed - Device
# not found"); pressing the ECU's reset button makes it re-enumerate and get picked up.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"

BOARD=${1:-}
shift || true

case "$BOARD" in
	atlas)
		FOLDER=config/boards/atlas
		TARGET=atlas
		SUITE=com.rusefi.HwCiAtlas
		OPENOCD_TARGET=target/stm32h7x.cfg
		# Bank 1 holds bootloader + firmware; bank 2 (the tune) is left alone
		ERASE_SECTORS="0 0 last"
		;;
	proteus_f4)
		FOLDER=config/boards/proteus
		TARGET=proteus_f4
		SUITE=com.rusefi.HwCiF4Proteus
		OPENOCD_TARGET=target/stm32f4x.cfg
		ERASE_SECTORS="0 0 11"
		;;
	f407-discovery)
		FOLDER=config/boards/f407-discovery
		TARGET=f407-discovery
		SUITE=com.rusefi.HwCiF4Discovery
		OPENOCD_TARGET=target/stm32f4x.cfg
		ERASE_SECTORS="0 0 11"
		;;
	*)
		sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'
		exit 1
		;;
esac

DO_BUILD=yes
DO_FLASH=yes
USE_SWD=no
ECU_SERIAL=
TESTS=()

while [ $# -gt 0 ]; do
	case "$1" in
		--no-build) DO_BUILD=no ;;
		--no-flash) DO_FLASH=no ;;
		--swd) USE_SWD=yes ;;
		--ecu-serial) ECU_SERIAL=$2; shift ;;
		--suite) SUITE=$2; shift ;;
		--) shift; TESTS=("$@"); break ;;
		*) echo "Unknown option $1"; exit 1 ;;
	esac
	shift
done

# Waits for the ECU's port; on WSL a missing ECU is nearly always usbipd not re-attaching it.
# A reset makes it re-enumerate, which gets auto-attach going again, so try one over SWD first.
wait_for_ecu() {
	if .github/workflows/hw-ci/resolve_ecu_port.sh "$ECU_SERIAL" "${1:-30}" > /dev/null; then
		return
	fi
	if grep -qi microsoft /proc/version; then
		echo "ECU didn't come back; resetting it over SWD to re-trigger usbipd auto-attach" >&2
		swd_reset > /dev/null 2>&1 || echo "SWD reset failed" >&2
		if .github/workflows/hw-ci/resolve_ecu_port.sh "$ECU_SERIAL" 30 > /dev/null; then
			return
		fi
		echo "WSL: if the ECU isn't in lsusb either, usbipd dropped it when it re-enumerated." >&2
		echo "On Windows: 'usbipd list', then 'usbipd attach --wsl --auto-attach --busid <id>'." >&2
		echo "If auto-attach is already running but stuck on 'Device not found', press the ECU's" >&2
		echo "reset button: it re-enumerates and auto-attach picks it up." >&2
		echo "Then carry on with: $0 $BOARD --no-build --no-flash" >&2
	fi
	exit 1
}

step() {
	echo
	echo "==== $* ===="
}

# The chip's USB serial (from the STM32 UID) is the same in firmware and OpenBLT, so it's how we
# find the ECU across reboots. Detect it now, while the firmware is (hopefully) running.
if [ -z "$ECU_SERIAL" ]; then
	mapfile -t found < <(ls /dev/serial/by-id/ 2>/dev/null | grep -i 'FOME\|rusEFI\|OpenBLT' | sed -n 's/.*_\([0-9A-F]\{24\}\)-if.*/\1/p' | sort -u)
	if [ ${#found[@]} -ne 1 ]; then
		echo "Found ${#found[@]} ECUs in /dev/serial/by-id, need exactly one, or pass --ecu-serial:"
		ls /dev/serial/by-id/ 2>/dev/null || echo "(/dev/serial/by-id not present)"
		exit 1
	fi
	ECU_SERIAL=${found[0]}
fi
echo "Board $BOARD, ECU serial $ECU_SERIAL, suite ${TESTS[*]:-$SUITE}"

if [ $DO_BUILD = yes ]; then
	step "Build firmware"
	# A unit test build in this tree leaves a host-gcc PCH variant behind, which the ARM build
	# rejects (-Werror=invalid-pch). CI runners never build unit tests, so only we need this.
	rm -rf firmware/pch/pch.h.gch
	# The firmware 'make clean' leaves the bootloader's objects, so switching boards in one tree
	# links one board's OpenBLT with another's (undefined FileTask/NetTransmitPacket). CI runners
	# only ever build their own board.
	rm -rf firmware/bootloader/blbuild
	.github/workflows/hw-ci/build_for_hw_ci.sh "$FOLDER" "$TARGET"
fi

if [ $DO_BUILD = no ]; then
	# The tests' Fields.java comes from whichever board's config was generated last, and a unit test
	# build regenerates it for the default board. Regenerate it for this one (same sources, same
	# signature as the firmware built from them).
	step "Generate config for $TARGET"
	(cd firmware && ./gen_config_board.sh "$FOLDER" "$TARGET" > /dev/null)
fi

step "Build autotest jar"
(cd java_tools && ./gradlew -q :autotest:shadowJar)

openocd_run() {
	openocd -f interface/stlink.cfg \
		-c "transport select hla_swd" \
		-c "set WORKAREASIZE 0x20000" \
		-f "$OPENOCD_TARGET" \
		-c "reset_config none separate" \
		-c init -c targets -c "reset halt" \
		"$@" \
		-c "reset run" -c shutdown
}

swd_reset() {
	openocd -f interface/stlink.cfg \
		-c "transport select hla_swd" \
		-f "$OPENOCD_TARGET" \
		-c "reset_config none separate" \
		-c init -c "reset run" -c shutdown
}

if [ $DO_FLASH = yes ]; then
	if [ $USE_SWD = yes ]; then
		step "Flash full image over SWD"
		openocd_run -c "flash erase_sector $ERASE_SECTORS" -c "flash write_image firmware/deliver/fome.bin 0x08000000" -c "verify_image firmware/deliver/fome.bin 0x08000000"
	else
		# Same as CI: bootloader only, firmware sectors erased, so the chip stays in OpenBLT
		step "Wipe and flash bootloader over SWD"
		openocd_run -c "flash erase_sector $ERASE_SECTORS" -c "flash write_image firmware/bootloader/blbuild/fome_bl.bin 0x08000000"

		step "Update firmware via OpenBLT"
		.github/workflows/hw-ci/openblt_update.sh "$ECU_SERIAL"
	fi
fi

step "Run tests"
wait_for_ecu 30
if [ ${#TESTS[@]} -gt 0 ]; then
	HARDWARE_CI_SERIAL_DEVICE=$(.github/workflows/hw-ci/resolve_ecu_port.sh "$ECU_SERIAL" 30)
	export HARDWARE_CI_SERIAL_DEVICE
	echo "Using $HARDWARE_CI_SERIAL_DEVICE"
	java -cp java_console/autotest/build/libs/autotest-all.jar com.rusefi.HwCiRun "${TESTS[@]}"
else
	.github/workflows/hw-ci/run_hw_ci.sh "$ECU_SERIAL" "$SUITE"
fi
