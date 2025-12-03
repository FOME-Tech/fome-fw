# `firmware/config/boards/` Directory

FOME supports a targetted arrangement of hardware, all based upon STM32 ARM cores (STM32F4, F7, H7).  These arrangements receive individualized binaries particular to their configuration, all compiled from the same source code.  The `firmware/config/boards/` directory is all about aligning that compilation process.

By definition, `BOARD_NAME` is a folder in `firmware/config/boards/`.

One `BOARD_NAME` could be producing a number of artifacts via `compile_$BUNDLE_NAME.sh` scripts.

*Work in progress: `SHORT_BOARDNAME` becomes `BUNDLE_NAME`.*
*Work in progress: `BOARD_SHORT_NAME` information (vs `BUNDLE_NAME` concept)*

## Adding a new board/hardware configuration
First, create the directory in `boards/` with the name of the new board/hardware configuration.

1) update [gen_config.sh](https://github.com/FOME-Tech/fome-fw/blob/master/firmware/gen_config.sh): add board entry to `for BOARD in ...` iteration - this would produce new `signature*.h` file and new `fome_*.ini` file

2) update [build-firmware.yaml](https://github.com/FOME-Tech/fome-fw/blob/master/.github/workflows/build-firmware.yaml) to get new firmware bundle on https://github.com/FOME-Tech/fome-fw/

3) add connector pinout mapping [yaml](https://en.wikipedia.org/wiki/YAML) file see examples of yaml files in `firmware/config/boards/*/connectors/` directory.

See also:

- https://wiki.fome.tech/Intro-Start-Here/Which-Hardware-For-Me/
- https://wiki.fome.tech/category/hardware/
