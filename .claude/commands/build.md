---
description: Build FOME firmware for a target board
argument-hint: <board/target>
allowed-tools: [Bash, Glob]
---

# Build Firmware

Build the FOME firmware for the specified board/target.

The user invoked this command with: $ARGUMENTS

## Available compile scripts

!`find firmware/config/boards -name "compile_*.sh" | sort | sed 's|firmware/config/boards/||; s|/compile_|  ->  compile_|'`

## Instructions

1. If no argument was provided, list the available targets above and ask the user to pick one.

2. If an argument was provided, find the matching compile script from the list above. Match loosely (e.g. "proteus_f7" matches `proteus/compile_proteus_f7.sh`, "atlas" matches `atlas/compile_atlas.sh`). If multiple scripts match, list them and ask the user to clarify.

3. Once you have the script, run it from the repo root using:
   ```
   cd firmware/config/boards/<board> && bash compile_<target>.sh
   ```
   Use `-j12` is already the default per project convention — do not add extra flags unless asked.

4. Report whether the build succeeded or failed. On failure, summarize the first error from the compiler output. If the build fails due to a PCH issue, run `make clean` then retry once. 
