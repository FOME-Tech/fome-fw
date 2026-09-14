# TunerStudio protection for board configuration

`TunerStudio::handleWriteChunkCommand()` calls `setBoardConfigOverrides()` after
each write. Editing a field assigned by an override therefore changes the bytes
back before TunerStudio checks their CRC. Keep the firmware override and the
board's `prepend.txt` in sync so the UI does not offer an impossible change.

Declare each fixed setting using its **generated TunerStudio name**:

```
#define ts_readonly_idle_stepperDirectionPin 1
#define ts_readonly_vbattAdcChannel 1
```

The generator emits `readOnly` in `[ConstantsExtensions]` and disables every
`field` referencing that setting, including full pinout and feature dialogs.
TunerStudio uses the ECU value when loading a calibration. Unknown constant names
fail generation, including names that were renamed or removed from the layout.
These declarations do not change configuration offsets, the wire protocol, or
the ECU's protection against writes from other clients.

For an override that applies only to certain hardware revisions, use a quoted
TunerStudio expression that is true when the setting is fixed:

```
! AlphaX 2chan rev.D (107) does not reserve SPI1 for SD.
#define ts_readonly_spi1mosiPin "hellenBoardId != 107"
```

TunerStudio's `readOnly` directive has no runtime condition. For these settings
the generator emits `controllerPriority` to preserve the connected ECU's value
on tune import, and combines the condition with each field's existing enable
expression. Its visibility expression is preserved. The field remains editable
on revisions where it is not overridden. Import preserves the ECU value on
**all** revisions; configure a free pin through its dialog after importing.
Offline revision-dependent controls cannot identify hardware until ECU live data
is available. Always connect before configuring those controls.

See the [EFI Analytics definition-file specification, section 6](https://www.efianalytics.com/TunerStudio/docs/EFI%20Analytics%20ECU%20Definition%20files.pdf).

## Audit of configuration writes

The audit followed all 23 `setBoardConfigOverrides()` implementations and their
helpers, including `hellen_meta.h` and `hellen_common.cpp`, and checked the
write/burn, hardware initialization and configuration-change paths.

| Area | Findings and handling |
| --- | --- |
| Core8 / Core48 | Fixed stepper and ETB wiring, CAN, battery/ADC scaling and temperature pullups are protected. Core48 also fixes SD/EGT SPI wiring, EGT chip selects and barometer I2C. Its serial pins are not exposed as TS constants. Core48 SD chip select is only a default, so it stays editable. |
| Proteus | Fixed SD/SPI3, CAN, barometer I2C and analog scaling/pullups are protected. Injector, ignition and ETB wiring are defaults, so they remain editable. |
| Atlas | Fixed Wi-Fi SPI4, CAN, barometer I2C, analog scaling/pullups, SPI-SD disable and EGT bus are protected. The SPI3 cleanup only runs when importing SPI-SD configuration; SPI3 itself is not unconditionally locked. |
| microRusEFI | Fixed TLE8888/SPI1, SPI2 wiring/enable, SPI3 wiring, ETB, CAN and analog scaling/pullups are protected. SPI3 enable and SD chip select/bus are defaults and remain editable. |
| Hellen / AlphaX | Fixed SD, analog scaling/pullups and board-specific CAN, trigger/cam or ETB assignments are protected. AlphaX 2chan, Hellen72, Hellen128 and Hellen154 use revision-dependent conditions. Hellen81's inactive SPI3 preprocessor branch is excluded. |
| Small CAN board / TDG PDM8 | Fixed CAN and analog scaling are protected. The small CAN board also fixes SD, Lua outputs 1–4 and auxiliary ADC inputs 1–8. |
| MC33816 driver | Initialization forcibly selected SPI3 despite the editable bus setting. It now uses the selected bus; existing MC33816 presets already select SPI3. |
| VVT initialization | Initialization raised the saved minimum RPM to cranking RPM. The runtime enable condition now checks both thresholds without rewriting the tune, including after live edits. |
| Other writes | Preset/default loading, explicit console/Lua tuning and calibration commands intentionally change configuration. Burn-time vehicle-string padding normalization remains intentional. Board-change callbacks drive hardware pullups without rewriting configuration. |

Verification includes generator tests for fixed and conditional controls, existing
enable/visibility expressions, repeated fields, optional fields and invalid names;
generation of every INI in `gen_config.sh`; and VVT tests for unchanged calibration
and both RPM boundaries after live edits. Actual ECU/TunerStudio burn behavior
still requires a connected hardware check with the regenerated matching INI.
