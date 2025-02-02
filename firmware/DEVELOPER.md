This directory contains the source code for the FOME firmware.

Ideally, end users should use pre-built firmware. You should not need
to modify or build firmware from source.

[The latest release is available here.](https://github.com/fome-tech/fome-fw/releases/latest)

# Environment

Building FOME requires a Unix-like OS. Currently supported are 64-bit Linux
(Intel/AMD) and macOS (64-bit Intel and ARM) platforms.

Linux and MacOS systems should have the software development tools,
primarily 'make', pre-installed or readily installed. To build on
Windows, a Unix-compatible environment (WSL) must be installed first.

Two options are available for building FOME: either via a [GitHub Codespace](https://github.com/codespaces),
or locally on your PC.

# Environment Option 1: GitHub Codespace

A codespace is a development environment that runs in the cloud. That means
that it requires no local setup on your machine in order to build FOME.

1. Go here: https://github.com/codespaces/new
1. Select the repository `FOME-Tech/fome-fw`, or your fork (`<username>/fome-fw`).
1. The defaults for the other options are OK, press "Create codespace"
1. The container is created, and a VsCode editor started running in the browser.
1. A terminal at the bottom will appear and show `Running postCreateCommand...`, wait for this to finish for everything to be set up properly. This might take a little while, so go finish that engine swap you've been meaning to wrap up.

# Environment Option 2: Local

### Editor/IDE

The supported IDE is [Visual Studio Code](https://code.visualstudio.com/).
Others will work, but if you need help from the maintainers, you might not
get much if you're using something else.

### Linux

1. Make sure you have the latest version of git installed: `sudo apt install git`
1. Clone the repo. `git clone https://github.com/<github username>/fome-fw.git`
1. Install additional dependencies: `cd fome-fw/firmware && ./setup_linux_environment.sh`
1. Open vscode: `code fome-fw/`

### macOS

macOS setup is largely the same as Linux, except the setup script will not work.
On recent versions of macOS, simply attempting to run `make` from the terminal
will prompt installation of Xcode command line developer tools, which is the only
prerequisite. Then, do steps 2 and 4 from the Linux setup instructions above.

### Windows

While technically possible to build on Windows, the preferred method is to first install
the [Windows Subsystem for Linux (WSL)](https://learn.microsoft.com/en-us/windows/wsl/install), then follow the [instructions for Linux](#linux)
above.

## Note

Note that the developers are volunteers, with varied motivations.
These motivations often include using leading-edge language and build
system concepts, requiring recent versions of tools.  Should you
encounter build problems, review the latest version of this document.
If you're still having trouble, reach out on Discord in the #firmware channel.

# Building

TL;DR:

```bash
cd config/boards/proteus
./compile_proteus_f7.sh
```

Each ECU+MCU combination has its own compilation scripts. In general, these scripts are located in `firmware/config/boards/<board>/compile_<board>_<chip>.sh`. For example, `firmware/config/boards/proteus/compile_proteus_f7.sh` compiles a firmware image for a Proteus (or Polygonus) ECU fitted with an STM32F7 microcontroller.

## Outputs

When a compile script is run, it will generate any generated files (TunerStudio ini, generated structs, etc), compile the firmware, compile the bootloader if configured for that board, then assemble any firmware images. These outputs are placed in `firmware/deliver/`.

For example, running `compile_proteus_f7.sh` will yield:

```
$ ls -lh deliver
657K	fome.bin
 18K	fome_bl.bin
1.9M	fome_update.srec
```

|File|Purpose|
|--|--|
|`fome.bin`|Combined firmware image, both bootloader and main image pre-assembled. Flash this one if you have a blank ECU (or one you want to wipe fully).|
|`fome_bl.bin`|Just the bootloader.|
|`fome_update.srec`|Update image that only contains the firmware, to be flashed via the bootloader.|

If using a GitHub Codespace, these files can be downloaded to your local PC by
navigating to them in the file browser at the left, right click, and pick "Download".

# Unit Tests

[See unit tests readme](../unit_tests/readme.md).
