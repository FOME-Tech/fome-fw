# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FOME (Free Open Motorsports ECU) is an open-source engine control unit firmware for STM32 microcontrollers. It's a fork of rusEFI focused on user experience and stability.

## Build Commands

Default to building with 12 threads unless otherwise specified (-j12 etc).

NEVER attempt to build one file manually, always do it via the board build scripts.

### Just build the thing

Don't probe the environment to decide whether a build is possible - run the build and read the
error. Everything the build needs is vendored in the repo, so absence from `PATH` proves nothing:

- The ARM toolchain lives in `firmware/ext/build-tools` and is selected by `firmware/use_arm_gcc.mk`
  based on `uname`. `which arm-none-eabi-gcc` will fail on a perfectly working checkout. The
  makefile even auto-inits the submodule if the compiler is missing.
- Toolchains, submodules and generated files are all fetched or built on demand.

### When a build fails weirdly, `make clean` first

Stale objects and stale generated files cause failures that look like something much worse: missing
headers, "No rule to make target" for paths that plainly exist, link errors against code you didn't
touch. Before concluding that the environment is broken, the submodules are missing, or a Makefile
needs fixing, run `make clean` in that directory and build again. This is cheap and it is very often
the whole answer.

Only after a clean build still fails is it worth investigating the build system itself.

### Building Firmware

A bare `make` in `firmware/` builds the default board, which is the quickest way to confirm firmware
still compiles for ARM.

Each board+chip combination also has its own compile script in `firmware/config/boards/<board>/`:

```bash
# Example: Build for Proteus F7
cd firmware/config/boards/proteus
./compile_proteus_f7.sh
```

Outputs are placed in `firmware/deliver/`:
- `fome.bin` - Complete image (bootloader + firmware) for blank ECUs
- `fome_update.srec` - Update image for bootloader flashing

### Unit Tests

```bash
cd unit_tests
make -j12
./build/fome_test

# Run a specific test
./build/fome_test --gtest_filter=TestName
```

Unit tests use Google Test and run on amd64/aarch64, not on the ECU.

### Code Generation

Code generation occurs as part of the normal firmware and unit test builds, via `firmware/fome_generated.mk`.

Build failures that look like generated files are missing are a common case of the stale-build
problem above - `make clean` first. In general, there should be no reason to manually run generated
code scripts.

The exception is working *on* the generator itself, where regenerating directly is a much faster
feedback loop than a full build:

```bash
cd firmware
./gen_config_board.sh config/boards/atlas atlas
```

Diff `firmware/generated/` and `firmware/tunerstudio/generated/*.ini` against a saved copy to see
exactly what a generator change did to the config layout and the ini.

### Code Formatting

`.clang-format` at the repository root: tabs (width 4), K&R braces, left-aligned pointers. VS Code
formats C/C++ on save via `ms-vscode.cpptools`.

```bash
./format.sh check   # show a diff of what would change
./format.sh         # apply in place
```

Directories opt out with their own `.clang-format` carrying `DisableFormat: true` - third-party code
(`firmware/ext/`, `unit_tests/googletest/`) but also some first-party trees, notably
`firmware/config/engines/` and various board/port `cfg` directories. Check for one before assuming a
file should be reformatted.

**`format.sh` only runs on Linux.** It shells out to `firmware/ext/clang-format`, which is an x86-64
ELF binary, and uses GNU-only `xargs -d`. On macOS both fail, so formatting is CI-enforced only -
match the surrounding style by hand and let CI confirm.

## Architecture

### Directory Structure

- `firmware/config/boards/` - Hardware configuration and defaults for different ECU hardware
- `firmware/config/engines/` - Hardware-agnostic configuration for engines (orthogonal to what ECU you run)
- `firmware/controllers/` - Core control logic
  - `algo/` - Fuel, ignition, and air calculations
  - `actuators/` - Control for engine-asynchronous outputs like electonic throttle, idle, AC, boost, VVT, etc.
  - `engine_cycle/` - Control for engine-synchronous outputs like injection, ignition
  - `sensors/` - Input processing (ADC, thermistors, pressure)
  - `trigger/` - Crank/cam position decoding and sync
  - `can/` - CAN bus communication
  - `lua/` - Runtime scripting
- `firmware/hw_layer/` - Hardware abstraction layer
- `firmware/ext/` - Third-party code, almost all of it git submodules (ChibiOS, lua, uzlib, openblt,
  the ARM toolchain in `build-tools`, and `libfirmware`). **Changes here do not belong to this repo** -
  they need a PR upstream and a submodule bump. New shared utility code goes in `firmware/util/`.
- `firmware/util/` - Self-contained utilities (no external dependencies)
- `unit_tests/` - Google Test suite
- `simulator/` - Windows/Linux firmware simulator. Uses core ECU code, but runs on PC rather than MCU.

### Key Concepts

- **Event-driven execution**: Trigger events from crank/cam sensors drive the main control loop
- **Angle-based scheduling**: Events scheduled by crank angle, not just time
- **Configuration-driven**: Board and engine parameters externalized; firmware adapts via configuration
- **ChibiOS RTOS**: Real-time operating system foundation

#### Generated configuration layout

- `firmware/integration/fome_config.txt` defines the parameters stored in persistent configuration (both "configuration", ie which pins do what, and the "calibration" or "tune", like the VE table, timing, etc.)
- That file is processed by the java tool at `java_tools/configuration_definition` to generate several outputs. It is critical that these match, so that each part of the system can communicate and agree about the in-memory config format.
  - C/C++ headers in `firmware/generated`
  - Along with `firmware/tunerstudio/tunerstudio.template.ini`, generates the ini file used by TunerStudio to communicate with the ECU. All tuner-adjustable parameters **MUST** appear in this file to be useful.
- `firmware/integration/LiveData.yaml` defines objects processed by the same tool to be transmitted from the ECU about the current state of the world. For example sensors, output values, and intermediate calculations useful for logging.

These are all automatically regenerated as part of running `make`, so no direct script invocation is required. Do not attempt to commit any generated files.

Inside `java_tools/configuration_definition`, the pipeline runs:

`RusefiConfigGrammar.g4` (ANTLR) -> `newparse/ParseState.java` (grammar listener; builds the field
objects in `newparse/parsing/`) -> `newparse/layout/` (assigns offsets and inserts alignment padding)
-> `newparse/outputs/` (one visitor per artifact: `CStructsVisitor`, `TsLayoutVisitor`,
`DatalogVisitor`, `OutputChannelVisitor`, ...). `TsWriter` copies `tunerstudio.template.ini` line by
line, substituting `@@DEFINE@@` references and expanding marker comments like
`CONFIG_DEFINITION_START`.

Two things that are easy to get wrong here:
- Numeric literals in the grammar (`numexpr`) are evaluated into a **shared FIFO** on `ParseState`,
  which the field-options parser drains positionally. A new rule that introduces a numexpr must
  consume its own result as that rule exits, or it will be silently eaten as the next field's `scale`.
- Adding a field to a struct shifts every offset after it. `sizeof(persistentState)` changing is what
  makes an ECU discard its stored tune on update (`flash_main.cpp`), so it's self-protecting, but say
  so in the changelog.

### Compiler Flags

- C99 with GNU extensions for C code
- C++20 for firmware code
- No RTTI, no exceptions (`-fno-rtti -fno-exceptions`)
- LTO enabled by default

### Build Conditionals

Key preprocessor flags that control compilation:
- `EFI_PROD_CODE=1` - Production firmware
- `EFI_UNIT_TEST=1` - Unit test build
- `EFI_SIMULATOR=1` - Simulator build

## Embedded Code Practices

- **Static allocation**: Prefer static allocation over dynamic (`new`/`malloc`). Memory is limited and fragmentation must be avoided.
- **Performance matters**: This is a hard real-time application. Fuel and ignition events must fire at precise crank angles. Avoid unnecessary computation in hot paths. Use lower priority threads for expensive computation.
- **No exceptions**: C++ exceptions are disabled. Use return values or error codes for error handling.
- **No RTTI**: `dynamic_cast` and `typeid` are unavailable.
- **Interrupt safety**: Be mindful of code that runs in interrupt context vs. thread context. Use appropriate synchronization primitives.
- **Stack usage**: Keep stack allocations small. Large arrays should be static or global, not local variables.

## Changelog

When you make a user-facing change (new feature, bug fix, breaking change, removal), update `firmware/CHANGELOG.md` under the `## Unreleased` section. See `/changelog` for details on what counts and how to write entries.

## Development Notes

- Supported IDE: Visual Studio Code
- Requires Unix-like OS (Linux, macOS, or Windows WSL)
- All PRs must pass CI gates (firmware builds for all boards, unit tests)
- Wiki: https://wiki.fome.tech/
- Discord: #firmware channel for help
