G0 expansion support is split into six modules: protocol/link, SPI transport,
ADC, digital inputs, outputs/PWM, and firmware bundling/updater. Each part
includes its tests and only depends on the preceding parts. The updater branch
also contains the factory-provisioning follow-up.

The link uses the shared protocol-9 header from dynfer/g0-extension: 36-byte frames,
CRC16/CCITT-FALSE in bytes 34..35, and a response delayed by one exchange. It
validates CRC, command, payload length, status, channel and protocol version before
publishing data. Reconnect discards the old pipeline. Module and output scheduling
rotate so changing outputs cannot starve sensor polls.

Only the 10 ms background SPI worker transfers data. `LockedModule` protects
state callbacks and the adapters lock readers/writers; SPI never runs while the
kernel is locked. On H7, DMA buffers live in noncached SRAM.

| Atlas connection | Pins / interface |
| --- | --- |
| SPI5 | PF7 SCK, PF8 MISO, PF9 MOSI, PF6 CS |
| G0 control | PB14 RESET, PB15 BOOT |
| ADC | `EFI_ADC_20`..`EFI_ADC_31`: G0 channels 0..9, 17, 18 |
| Switch inputs | `G0_DIGITAL_1`..`G0_DIGITAL_4` |
| Lowside outputs | `G0_OUT_1`..`G0_OUT_4`, static or local 1..500 Hz PWM |

ADC values are volts at the G0 pin; board divider calibration still applies.
Unavailable or 250 ms old samples return NaN and invalidate subscribed sensors;
filters are preloaded on recovery. Digital reads return false when unavailable.
Digital mode changes are acknowledged before polling. Frequency snapshots are
available internally; registering frequency sensors in fome-fw is separate work.
SENT is unsupported.

Outputs start off. Callers queue setpoints, and only a matching acknowledgement
clears them; changes while an ACK is in flight stay pending. Setpoints refresh
every second and after reconnect. `SimplePwm` supports duty, frequency and
inversion, including Lua frequency changes. Deinitializing a pin turns it off.
The companion preserves PWM cycles during duty updates and disables outputs after
500 ms without a checksum-valid frame. These outputs are for asynchronous
auxiliary loads: they cannot follow crank/cam timing through SPI.

Build Atlas with its normal script:

```sh
cd firmware/config/boards/atlas
bash ./compile_atlas.sh
```

Atlas enables the expansion and updater by default. The build initializes the
pinned `firmware/ext/g0_firmware` submodule, builds it with fome-fw's ARM compiler,
and generates `g0_firmware_image.h` containing the complete immutable image and
build identity. Clean builds recreate it; unchanged inputs preserve timestamps.
No prepared binary/header is needed. Update the submodule pin for a new release.

Before registering remote I/O, the updater first probes an existing G071 ROM
session without pulsing NRST. This preserves factory-blank entry on ROM versions
that clear the EMPTY flag during startup. RESET and BOOT output latches are
preloaded high before enabling their GPIO drivers. Atlas also keeps PB14/NRST
pulled up during both ECU bootloader and main-firmware GPIO initialization. A running application follows
the normal reset/version-check path; a matching image skips programming.

Before erasing main flash, the loader confirms device ID 0x0460, reads the full
128-byte option block twice, and validates its option/complement pairs. It
requires RDP level 0, nBOOT1=1, BOOT_LOCK=0 and an NRST input mode. If nBOOT_SEL is
set, it clears only that bit and updates its inverse copy, preserving all other
bytes. An already enabled BOOT0 pin causes no option write. The loader reconnects
after the write and verifies the complete option block; a missing final ACK is
accepted only when the readback proves the intended configuration was stored.

It then programs padded 256-byte blocks and reads every block back. BOOT0 is
lowered before the ROM Go command starts the application, avoiding dependence on
the latched EMPTY flag. The application protocol and image identity must match
before runtime I/O is registered. Invalid vectors or images beyond G071 flash
bounds are rejected before changing hardware. Firmware identity is generated
from companion build inputs independently of the wire protocol version.

This first-boot path needs a live ROM session from power-on. A G0 that already
lost that session while BOOT0 selection was disabled may need a full power cycle;
pulsing NRST cannot recreate the empty-check condition on affected ROMs. An
already programmed chip with BOOT0 disabled still needs a cooperating application
or debug access to enter ROM.

The option layout and boot sequence follow
[RM0444](https://www.stmcu.jp/wp/wp-content/uploads/2018/12/RM0444_Rev3.pdf),
[AN2606 section 48.3.1](https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf#page=264)
and [AN4286](https://www.st.com/resource/en/application_note/an4286-spi-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf).

`G0_FIRMWARE_UPDATE=no` builds runtime I/O without an image or updater.
`G0_EXTENSION=no` disables the expansion. These flags also skip the companion
build dependency. Other boards need their own `initG0Extension` wiring.

Run the standalone tests:

```sh
cmake -S firmware/hw_layer/g0_extension/tests -B /tmp/g0-tests -DG0_SANITIZE=ON
cmake --build /tmp/g0-tests -j12
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir /tmp/g0-tests --output-on-failure
```

Physical SPI timing/recovery, input latency, PWM resolution, BOOT option bytes,
reset wiring and interrupted programming still need bench verification. Nothing
in the host tests establishes those hardware properties.
