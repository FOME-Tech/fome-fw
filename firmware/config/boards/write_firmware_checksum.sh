#!/bin/bash

# Read an elf image, then write .hex/.srec files with checksum added

set -e

# Input: An elf file to checksum
INPUT_FILE=$1

# Output: Srec file name
OUTPUT_FILE=$2

# Extract the firmware's base address from the elf - it may be different depending on exact CPU
firmwareBaseAddress="$(objdump -h -j .vectors $INPUT_FILE | awk '/.vectors/ {print $5 }')"
echo "$INPUT_FILE Base address is 0x$firmwareBaseAddress"

# Offset the checksum's location at 0x1C past the base
checksumAddress="$(printf "%X\n" $((0x$firmwareBaseAddress+0x1c)))"
echo "$INPUT_FILE Checksum address is 0x$checksumAddress"

# Make a temporary hex that hex2dfu can read
objcopy -O ihex $INPUT_FILE temp.hex

# Add the checksum
$HEX2DFU -i temp.hex -c $checksumAddress -b $OUTPUT_FILE.bin
rm temp.hex

# re-make hex, srec with the checksum in place
objcopy -I binary -O ihex --change-addresses=0x$firmwareBaseAddress $OUTPUT_FILE.bin $OUTPUT_FILE.hex
objcopy -I binary -O srec --change-addresses=0x$firmwareBaseAddress $OUTPUT_FILE.bin $OUTPUT_FILE.srec
