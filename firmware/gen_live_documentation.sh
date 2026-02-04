#!/bin/bash

rm gen_live_documentation.log

STAMP_FILE=${1:-}

java -DSystemOut.name=logs/gen_live_documentation \
 -cp ../java_tools/ConfigDefinition.jar \
 com.rusefi.ldmp.LiveDataProcessor integration/LiveData.yaml \
 -makefileDep .dep/fome_live_data.d \
 ${STAMP_FILE:+-stampFile "$STAMP_FILE"}
[ $? -eq 0 ] || { echo "ERROR generating"; exit 1; }

java -DSystemOut.name=logs/gen_java_enum -cp ../java_tools/enum2string.jar com.rusefi.ToJavaEnum -enumInputFile generated/live_data_ids.h -outputPath ../java_console/io/src/main/java/com/rusefi/enums
[ $? -eq 0 ] || { echo "ERROR generating live data ids"; exit 1; }

# live_data_ids.h is generated above, so order of operations is important here!
./gen_enum_to_string.sh
