#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BRIDGE_DIR="$SCRIPT_DIR/fome-can-bridge"
OUTPUT_DIR="$SCRIPT_DIR/current_bridge_output"

echo "Building FOME CAN Bridge..."
java -version

cd "$BRIDGE_DIR"
./gradlew shadowJar

[ -d "$OUTPUT_DIR" ] || mkdir -p "$OUTPUT_DIR"

# Identify the JAR (handles version changes)
JAR_PATH=$(find build/libs/ -name "fome-can-bridge-*.jar" | head -n 1)

if [ -f "$JAR_PATH" ]; then
    cp "$JAR_PATH" "$OUTPUT_DIR/fome-can-bridge.jar"
    echo "Build successful!"
    echo "Artifact: $OUTPUT_DIR/fome-can-bridge.jar"
    ls -lh "$OUTPUT_DIR/fome-can-bridge.jar"
else
    echo "ERROR: Bridge JAR not found!"
    exit 1
fi
