#!/bin/bash

set -euo pipefail

echo "This script reads fome_config.txt and produces firmware persistent configuration headers"
echo "The storage section of fome.ini is updated as well"

rm -f gen_config.log
rm -f gen_config_board.log

# see also .github/workflows/build-firmware.yaml where we compile all versions of firmware
for BOARD in \
   "config/boards/hellen/alphax-2chan alphax-2chan" \
   "config/boards/hellen/alphax-4chan alphax-4chan" \
   "config/boards/hellen/alphax-8chan alphax-8chan" \
   "config/boards/hellen/harley81 harley81" \
   "config/boards/hellen/hellen128 hellen128" \
   "config/boards/hellen/hellen121vag hellen121vag" \
   "config/boards/hellen/hellen121nissan hellen121nissan" \
   "config/boards/hellen/hellen-honda-k hellen-honda-k" \
   "config/boards/hellen/hellen154hyundai hellen154hyundai" \
   "config/boards/hellen/hellen88bmw hellen88bmw" \
   "config/boards/hellen/hellen72 hellen72" \
   "config/boards/hellen/hellen81 hellen81" \
   "config/boards/hellen/hellen-nb1 hellen-nb1" \
   "config/boards/hellen/hellen-gm-e67 hellen-gm-e67" \
   "config/boards/hellen/hellen64_miataNA6_94 hellenNA6" \
   "config/boards/hellen/hellenNA8_96 hellenNA8_96" \
   "config/boards/hellen/small-can-board small-can-board" \
   "config/boards/microrusefi mre_f7" \
   "config/boards/microrusefi mre_f4" \
   "config/boards/core8 core8" \
   "config/boards/core48 core48" \
   "config/boards/frankenso frankenso_na6" \
   "config/boards/prometheus prometheus_469" \
   "config/boards/prometheus prometheus_405" \
   "config/boards/proteus proteus_f7" \
   "config/boards/proteus proteus_f4" \
   "config/boards/proteus proteus_h7" \
   "config/boards/f407-discovery f407-discovery" \
   "config/boards/f429-discovery f429-discovery" \
   "config/boards/atlas atlas"\
   "config/boards/tdg-pdm8 tdg-pdm8"\
   ; do
 BOARD_NAME=$(echo "$BOARD" | cut -d " " -f 1)
 BOARD_SHORT_NAME=$(echo "$BOARD" | cut -d " " -f 2)
 INI=$(echo "$BOARD" | cut -d " " -f 3)
 ./gen_config_board.sh $BOARD_NAME $BOARD_SHORT_NAME $INI
 [ $? -eq 0 ] || { echo "ERROR generating board $BOARD_NAME $BOARD_SHORT_NAME $INI"; exit 1; }
done

exit 0
