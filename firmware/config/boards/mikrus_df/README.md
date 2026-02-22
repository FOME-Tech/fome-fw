# Mikrus DF - Build & Debug Guide

**MCU:** STM32H743VIT
**Based on:** Atlas / Proteus boards

---

## Prerequisites

Install build dependencies (Linux):
```bash
cd firmware
./setup_linux_environment.sh
```

---

## Build Firmware

```bash
cd firmware/config/boards/mikrus_df
./compile_mikrus_df.sh
```

Outputs in `firmware/deliver/`:
- `fome.bin` — full image (bootloader + firmware) for a blank ECU
- `fome_update.srec` — update image for flashing via the bootloader

The build also generates:
- `firmware/deliver/fome.ini` — TunerStudio ini file for this board

---

## Build the INI File Only

The ini file is generated automatically during a full firmware build. If you need
to regenerate it without recompiling the firmware, you can run the code-gen step
from the firmware root:

```bash
cd firmware
make gen_config
```

The board-specific ini is placed in `firmware/tunerstudio/generated/fome_<board>.ini`.

---

## Build the Java Console (FOME Console)

```bash
cd java_tools
./gradlew :ui:shadowJar
```

The runnable jar is placed in `java_tools/ui/build/libs/`.

Run it with:
```bash
java -jar java_tools/ui/build/libs/ui-all.jar
```

---

## Debugging in VS Code

Two named debug configurations are provided in [.vscode/launch.json](../../../../.vscode/launch.json):

| Configuration | Use for |
|---|---|
| **Debug Mikrus DF** | Debugging firmware (`firmware/build/fome.elf`) |
| **Debug Mikrus DF Bootloader** | Debugging the OpenBLT bootloader |

### Requirements

- **Cortex-Debug** VS Code extension (`marus25.cortex-debug`)
- **ST-Link** probe connected to the SWD header on the board

### Steps

1. Build firmware first (see above).
2. Open the **Run and Debug** panel in VS Code (`Ctrl+Shift+D`).
3. Select **Debug Mikrus DF** from the dropdown.
4. Press **F5** to flash and start debugging.

OpenOCD is launched automatically using `stlink.cfg` + `stm32h7x.cfg`.

---

## Pin Assignments (Summary)

| Function | Pin |
|---|---|
| CAN RX/TX | PD0 / PD1 |
| 12V Sense | PA2 |
| Analog IN1–IN6 | PC4, PC5, PC0, PC1, PC2, PC3 |
| Analog IN7–IN8 | PA0, PA1 |
| Injectors IN1–IN8 | PD7, PB4–PB9, PA8 |
| Ignition IGN1–IGN2 | PD4, PD3 |
| Hall1 / Hall2 / VR1 | PC6, PE11, PE7 |
| ETB PWM/DIR/DIS | PD12, PD10, PD11 |
| SD Card (SDMMC1 1-bit) | DAT0=PC8, CLK=PC12, CMD=PD2 |
| WiFi SPI1 (ATWINC1500) | SCK=PA5, MISO=PA6, MOSI=PA7, CS=PA4 |
| WiFi RST / IRQ | PB0 / PB1 |
| LEDs R/G/G/O | PE3, PE4, PE5, PE6 |
| USB D- / D+ | PA11 / PA12 |
