---
description: Build and run the FOME firmware simulator
argument-hint: [seconds]
allowed-tools: [Bash]
---

# Simulator

Build and run the FOME firmware simulator.

The user invoked this command with: $ARGUMENTS

## Instructions

1. Build the simulator:
   ```
   cd simulator && make -j12
   ```
   If the build fails due to a PCH issue, run `make clean` then retry once.

2. If the build fails for another reason, report the first compiler error and stop.

3. Run the simulator from the `simulator/` directory:
   - If an argument was provided, use it as the duration in seconds: `./build/fome_simulator $ARGUMENTS`
   - Otherwise default to 10 seconds: `./build/fome_simulator 10`

4. Report whether the simulator ran successfully or crashed, including any notable output.
