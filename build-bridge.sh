#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BRIDGE_DIR="$SCRIPT_DIR/fome-can-bridge"
OUTPUT_DIR="$SCRIPT_DIR/current_bridge_output"

echo "Building FOME CAN Bridge..."
java -version

cd "$BRIDGE_DIR"
./gradlew shadowJar

[ -d "$OUTPUT_DIR" ] || mkdir -p "$OUTPUT_DIR"

JAR_PATH="$OUTPUT_DIR/fome-can-bridge.jar"

if [ -f "$JAR_PATH" ]; then
    echo "Build successful!"
    echo "Artifact: $JAR_PATH"
    ls -lh "$JAR_PATH"
else
    echo "ERROR: Bridge JAR not found at $JAR_PATH!"
    exit 1
fi
