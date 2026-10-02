# Airmass control: capabilities and differences from upstream

Documentation revision 3 · 2026-10-01

This branch gives Speed Density, Alpha-N, and MAF their own calibrations and
adds a strategy that combines SD and Alpha-N cylinder air masses. The tuner can
calibrate SD and Alpha-N separately with VE Analyze while each has full
authority, then use those same maps together in blended operation. Their cells
and RPM/MAP or RPM/TPS axes remain independent; no table copying or axis
conversion is needed when enabling the blend. The tuner can also select the
load coordinate used by each fuel, ignition, protection, or actuator function.

The comparison uses FOME upstream at
[`178ecaff6516`](https://github.com/FOME-Tech/fome-fw/tree/178ecaff6516607a68e77bdcca017ed5bef195a8),
the upstream base used for this implementation. The behavior described here is
the current implementation in this branch.

## What changes in practice

| Area | Upstream at the reference above | This branch and its practical benefit |
| --- | --- | --- |
| Main maps | SD, Alpha-N, and MAF use one VE map, with a selectable load-axis override. | Three dedicated maps retain their own cells and axes. Each model can be calibrated without repurposing another model's table. |
| SD and Alpha-N together | One airmass strategy is selected at a time. | An 8 x 8 TPS/RPM map blends calculated masses from 0% to 100% Alpha-N. The contribution can vary across the operating range. |
| Load sources | Target lambda and main ignition already have selectors; many other functions inherit a shared load. | More functions have independent selectors and cursors. For example, changing the lambda-table source need not change staging or lambda-protection thresholds. |
| Temperature and pressure | SD uses Tcharge; Alpha-N uses fixed 20 °C or optional IAT at reference pressure. SD's MAP estimate is available as a fallback. | Selectable Tcharge/IAT, explicit MAP-estimate permission, standalone Alpha-N Multiply MAP, and optional Alpha-N BARO compensation make these modeling choices visible and configurable. |
| Idle VE | One overlay follows the selected model and its load override. | In blended operation the overlay belongs to a chosen branch, with an independent load source. Its calibration affects that branch before mass blending. |
| Model failure | Recovery depends on the selected model's existing sensor fallbacks. | The blend can use a healthy remaining branch at full authority. Diagnostics show requested and effective contributions and the fallback in use. |
| Fuel delivery | Fuel scheduling has no shared contract tying new pulses to a completed airmass/fuel publication. | SD, Alpha-N, MAF, and the blend require a complete valid fuel result before admitting new injections. Temporary failures recover automatically, and accepted pulse callbacks remain scheduled to completion. |
| VE Analyze | The analyzer targets the shared VE map. | Tune the dedicated SD map at 100% SD authority and the dedicated Alpha-N map at 100% Alpha-N authority, then blend their calculated masses using those same maps. Analysis requires a qualified session at one uniform endpoint. |

### Where this helps calibration

- **Calibrate each model with VE Analyze, then blend.** Use a uniform 0%
  Alpha-N authority map to tune SD, and a uniform 100% map in a separate session
  to tune Alpha-N. Both calibrated maps stay in place when variable authority
  is enabled. See the [calibration workflow](#calibrate-separately-then-blend)
  for session requirements. MAF retains its own correction map.
- **Choose coordinates per function.** An Alpha-N air model can coexist with
  MAP-based ignition, TPS-based staging, and independently configured lambda
  protection. The chosen sources and their calibration units remain explicit.
- **Explain a fuel result from the log.** Separate branch masses, contribution,
  temperature, pressure provenance, correction factor, and load cursors show
  which inputs and fallbacks produced the result.

For example, if SD calculates 400 mg/cylinder and Alpha-N calculates
500 mg/cylinder, 25% Alpha-N authority produces 425 mg/cylinder before common
corrections. A common +5% VE correction then produces 446.25 mg/cylinder. The
authority map sets the weighting; the firmware does not learn which model is
more accurate from the lambda residual.

### Comparison with rusEFI table blending

This comparison is separate from the FOME upstream baseline above and refers
to rusEFI [`5a8d43a`](https://github.com/rusefi/rusefi/tree/5a8d43a8f72a7ab8a642ef598d2fc2d79d1f5c06),
checked on 2026-10-01. Its [airmass implementation](https://github.com/rusefi/rusefi/blob/5a8d43a8f72a7ab8a642ef598d2fc2d79d1f5c06/firmware/controllers/algo/airmass/airmass.cpp#L26-L64)
can switch or blend between primary and secondary VE tables. Both use the same
load coordinate and supply VE to the selected air model. Its
[standard VE Analyze configuration](https://github.com/rusefi/rusefi/blob/5a8d43a8f72a7ab8a642ef598d2fc2d79d1f5c06/firmware/tunerstudio/tunerstudio.template.ini#L251-L269)
registers the primary VE table only; setting secondary-table authority to 100%
does not redirect VE Analyze to it.

This branch provides separate analysis targets for SD and Alpha-N and blends
the masses calculated from their respective RPM/MAP and RPM/TPS maps. That
supports the calibration workflow below directly. The rusEFI comparison is
limited to the cited standard implementation; custom INIs or scripts may differ.

### Calibration consequences

The expanded tune layout requires the matching firmware and INI. Existing
binary tunes are not migrated, and changing a source does not convert its axes,
cells, or thresholds. See [compatibility with upstream tunes](#compatibility-with-upstream-tunes)
for mappings that retain the previous mass equation.

Both blended maps need calibration wherever either can be promoted to full
authority after a failure. Mixed operation cannot use VE Analyze to tune both
maps at once. Defaults provide usable table structure, but they do not establish
a calibrated engine model or a measured improvement in engine performance.

The sections below describe the controls and their behavior. Quoted names are
TunerStudio labels; names in `code` are settings or log channels. The
[architecture reference](../development/airmass-architecture.md) covers equations,
validation, and scheduling.

Quick reference: [model settings](#airmass-model-settings-and-blending),
[load sources](#independent-load-sources), [VE Analyze](#ve-analyze),
[tune compatibility](#compatibility-with-upstream-tunes),
[fault recovery](#limp-operation-and-automatic-recovery), and
[log channels](#useful-log-channels).

## Choose the fuel strategy

Open **Base Engine > Base engine > Fuel strategy** (`fuelAlgorithm`).

| Strategy | Operation |
| --- | --- |
| **Speed Density** | Uses the dedicated SD map and effective MAP. |
| **Alpha-N** | Uses the dedicated Alpha-N map and TPS. It can optionally multiply the result by effective MAP. |
| **SD + Alpha-N** | Calculates the two cylinder air masses and combines them with the authority map. |
| **MAF Air Charge** | Uses measured MAF air mass and the dedicated MAF correction map. |
| **Lua** | Retains the existing Lua model behavior. |

SD, Alpha-N, and MAF always use separate maps. The former **Override VE table
load axis** control is gone because every main map has a fixed, natural axis.
Selecting a strategy puts its dedicated map into service.

MAF remains a standalone strategy. It is not a third branch in the SD +
Alpha-N blend.

The following editors remain visible under **Fuel** whenever injection is
enabled, so a map can be prepared before its strategy is selected:

- **Speed Density VE**
- **Alpha-N VE (reference filling)**
- **Airmass model settings and blending**
- **MAF correction**

An editor's live cursor is meaningful only while that model is being evaluated.
Editor or VE Analyze tab visibility does not establish that the model or
analyzer is currently eligible to run.

## Main maps and their physical meaning

| Map | Size | Load axis | Cell meaning |
| --- | ---: | --- | --- |
| **Speed Density VE** | 16 x 16 | Effective MAP, kPa | Volumetric efficiency for the pressure-based SD equation. |
| **Alpha-N VE (reference filling)** | 16 x 16 | TPS, % | Cylinder filling at the Alpha-N reference pressure and selected air temperature. |
| **MAF correction** | 16 x 16 | Uncorrected normalized cylinder filling, % | Correction to measured MAF air mass; 100% is neutral. |
| **% Alpha-N contribution** | 8 x 8 | TPS, % | Authority: 0% is SD only and 100% is Alpha-N only. |

All four maps use RPM as the other axis. The three 16 x 16 model maps have
independent RPM bins as well as independent load bins. Changing another
table's source does not change any of these axes.

SD VE and Alpha-N reference filling are different physical quantities. The
blend combines grams of air per cylinder, rather than averaging the percentages
stored in the two maps. With `w = authority / 100`:

```text
air mass = (1 - w) x SD air mass + w x Alpha-N air mass
```

In normal operation, at exactly 0% only SD is evaluated, and at exactly 100%
only Alpha-N is evaluated. Recovery can evaluate the other branch if the
selected branch fails. The existing **VE blend tables** are common
corrections: in combined operation their product is applied once after the two
masses have been blended. They do not set SD/Alpha-N authority.

## Airmass model settings and blending

Open **Fuel > Airmass model settings and blending**. This panel contains the
8 x 8 authority map, the current injection and recovery indicators, and the
controls below.

### Air temperature source

**Air temperature source** (`airmassTemperatureSource`) selects:

- **Tcharge**: the existing charge-temperature estimate, including its
  configured model and rate limits. If it is invalid, the model uses valid IAT
  or 20 degrees C when IAT is also invalid.
- **IAT**: the measured IAT sensor value. An invalid reading uses 20 degrees C,
  matching the upstream Alpha-N fallback.

SD and Alpha-N use this selection in standalone operation. During a combined
calculation, one temperature and one validity result are captured and shared by
both branches. MAF does not receive an additional ideal-gas density correction
from this setting.

Changing the source can change calculated mass without changing a VE cell.
Review the main maps, Idle VE, **Charge temperature estimation**, and thermal
fuel corrections after changing it. For old Alpha-N tunes using measured IAT,
select **IAT** with **Multiply MAP** and Alpha-N BARO compensation off to retain
the reference-pressure equation. The kelvin conversion now uses Celsius +
273.15 instead of Celsius + 273. The fallback value remains 20 degrees C, but
there is no fixed-temperature selector while IAT is healthy. Changing from an
old fixed-temperature tune to IAT or Tcharge requires reviewing the calibration
across temperatures.

Use `airmassTemperature` and `airmassTemperatureSourceUsed` to confirm the
temperature and source actually used.

### Permission to use the MAP estimate

**Use MAP estimate table** (`useMapEstimateTable`) is the general permission
for the existing TPS/RPM **MAP estimate table**. It is off by default.

| General permission | **Use MAP estimate during transient** | Result |
| --- | --- | --- |
| Off | Inactive | Uses measured MAP wherever pressure is required. The estimate is not evaluated for fallback or comparison. |
| On | Off | A valid estimate may replace unavailable measured MAP for an effective-MAP request. |
| On | On | A valid estimate may also participate in the existing transient comparison. An invalid estimate leaves valid measured MAP in service. |

The transient control remains under **Fuel > Acceleration enrichment** and is
subordinate to the general permission. The permission is shown for SD,
Alpha-N, SD + Alpha-N, and MAF because a downstream table can request effective
MAP even when the selected air model does not use pressure.

**Measured MAP** and **Effective MAP** are distinct sources. A measured-MAP
consumer never accepts the estimate. An effective-MAP consumer can use the
estimate only when permission is on and the RPM, estimate-table axes, and cells
are valid. Whenever an estimate is evaluated with invalid TPS, its lookup uses
0% TPS and reports fallback, matching upstream's lookup coordinate. Enabling the
permission does not certify that the estimate map is calibrated correctly.

No barometric normalization is applied to the MAP estimate table.

### Standalone Alpha-N: Multiply MAP

**Standalone Alpha-N: Multiply MAP** (`alphaNMultiplyMap`) changes only the
standalone Alpha-N strategy:

```text
hybrid Alpha-N mass = pure Alpha-N mass x effective MAP / 101.325 kPa
```

The main Alpha-N map remains indexed by TPS and RPM. The option requires valid
effective MAP, subject to the MAP-estimate permission described above. It also
suppresses the optional Alpha-N barometric factor because pressure is already
part of the equation.

The control can remain visible while SD + Alpha-N is selected, but it is
inactive there. The Alpha-N branch of the blend is always the pure
reference-pressure model, including at 100% authority. Moving the same map
between hybrid standalone Alpha-N and the pure blended branch therefore
requires a calibration review; no cells are converted.

### Alpha-N barometric compensation

**Use barometric compensation for Alpha-N** (`alphaNBaroCompensation`) applies
this coefficient to pure Alpha-N before blending:

```text
coefficient = ambient BARO / configured reference pressure
```

Set **Barometric reference pressure**
(`alphaNBaroReferencePressure`) to the fixed pressure at which the Alpha-N map
was calibrated. Do not rewrite it at each start.

The coefficient applies to pure standalone Alpha-N and to the Alpha-N branch
of SD + Alpha-N. It is inactive for hybrid standalone Alpha-N with **Multiply
MAP**, and it does not affect SD or MAF. At 0% Alpha-N authority the branch is
skipped, so BARO is not a dependency.

An absent or invalid BARO value uses the neutral coefficient 1, so Alpha-N can
continue with its unscaled 101.325 kPa pressure term. Use
`alphaNBaroCoefficient` and `airmassPressureFlags` to distinguish this fallback
from a valid ambient ratio.

The existing **Common barometric correction** remains a later residual fuel
correction and can affect every strategy. Review that table when enabling the
Alpha-N coefficient so the same ambient-pressure effect is not applied twice.

## Independent load sources

Tables and controls that have their own calibration can now choose their own
load coordinate. A source change affects only that consumer. It does not change
an airmass map axis, the authority map, or another consumer.

The common load selector used by the fuel, ignition, lambda, knock, and HPFP
consumers offers:

| Selection | Coordinate |
| --- | --- |
| **Model default** | SD: effective MAP; Alpha-N: TPS; MAF: uncorrected filling; SD + Alpha-N: effective MAP. |
| **Measured MAP** | Measured MAP in kPa, with no estimate fallback. |
| **TPS** | TPS1 in percent. |
| **Acc Pedal** | Accelerator-pedal position in percent. |
| **Cyl Filling %** | Final air mass normalized to the standard cylinder charge. |
| **Effective MAP** | MAP in kPa, with a permitted valid estimate when required. |

The table editor displays the selected units and its matching live cursor.
Optional consumer cursors clear to zero while their function is disabled. A
latched lambda-protection cut keeps the load cursor used to check restore
conditions until the cut clears, even if protection has been disabled.
Changing a source never converts breakpoints, cells, or thresholds. For
example, selecting TPS for a table whose bins contain kPa values produces an
incorrect calibration until those bins and values are rebuilt.

The independent selectors cover:

- target lambda and the main ignition table;
- injection phase;
- every cylinder fuel trim and every cylinder ignition trim;
- STFT region thresholds;
- staged injection;
- the lambda-deviation table and lambda-protection thresholds;
- trailing spark and the ignition IAT-correction table;
- maximum knock retard and every per-cylinder knock-gain table;
- the HPFP target table;
- intake and exhaust VVT target tables;
- fan PWM AC-off and AC-on tables;
- each open-loop and closed-loop boost correction table;
- GPPWM channels, which now also offer **Effective MAP**.

Use these controls to configure them:

| Function | Control location or label |
| --- | --- |
| Target lambda and injection phase | Injection settings: **Target lambda load source** and **Injection phase load source**. |
| Cylinder fuel trims | **Fuel > Cylinder fuel trims > Load sources**. |
| STFT regions | Closed-loop fuel correction: **Load source and units**. |
| Staged injection | Staged-injection settings: **Table load source**. |
| Lambda protection | **Threshold load source** and **Deviation table load source**. |
| Main ignition table | Ignition settings: **Ignition table load source**. |
| Trailing spark and IAT correction | **Ignition > Auxiliary table load sources**. |
| Cylinder ignition trims | **Ignition > Cylinder ign trims > Load sources**. |
| Knock retard and gain | **Ignition > Knock table load sources**. |
| HPFP target | HPFP configuration: **Target table load source**. |
| VVT targets | VVT settings: separate intake and exhaust target load-source controls. |
| Fan tables | Fan settings: separate **PWM AC-off table X axis** and **PWM AC-on table X axis** controls for each fan. |
| Boost correction tables | Each boost blend configuration has **Table X axis**; **Zero** inherits the corresponding main boost-table X source. |
| GPPWM | The existing channel selectors include **Effective MAP**. |

The target-lambda and main-ignition controls retain their previous role, with
labels that now describe a source instead of an axis override. VVT, fan, boost,
and GPPWM controls use the broader channel selector, whose existing choices
remain available. The fan AC-on source is now independent from AC-off. Each
boost correction table can now override its X source independently; its
existing blend parameter and Y-axis override continue to work as before.

Some independently selected tables still share a breakpoint array:

- all cylinder fuel-trim tables share one load-bin array;
- all cylinder ignition-trim tables share one load-bin array;
- all cylinder knock-gain tables share one load-bin array;
- each fan's AC-off and AC-on tables share that fan's numerical X bins.

Selecting MAP for one cylinder and TPS for another is possible, but their
shared breakpoints must be numerically suitable for both tables. There is no
separate per-cylinder bin array. The table dimensions have not changed.

A TPS selection for one consumer does not remove the MAP requirement of an
active SD branch. At 100% Alpha-N the SD branch is skipped, so MAP is not an air
model dependency. A fuel-path consumer may still be configured for measured or
effective MAP; if MAP is unavailable, that consumer uses the 200 kPa fallback
below and reports degraded operation.

Fuel-path load overrides retain upstream recovery values when their chosen
sensor is unavailable: measured or effective MAP uses 200 kPa, while TPS or
accelerator pedal uses 100%. Validity channels still report the failed source,
and the airmass status reports **Fallback active**. These values deliberately
select the high end of typical protection and enrichment tables; verify that
every affected table has a safe high-load region.

Small TPS and pedal calibration offsets within the configured sensor limits
(−10% to 110% by default) use the nearest 0% or 100% table coordinate. A valid
reading of −0.1%, for example, uses 0% without entering fallback or stopping
Alpha-N fueling. Invalid readings and readings outside those limits still use
the normal failure policy.

Optional actuator tables, including fans, GPPWM, VVT, boost, and similar
consumers, handle an unavailable coordinate locally. For example, fan and
GPPWM outputs use their configured error duty. Their load error does not make
the complete fuel calculation invalid.

## Idle VE

**Idle > Idle settings > Use idle VE table** remains the explicit enable.
When enabled, two controls define the single Idle VE table:

- **Idle VE target model** (`idleVeModel`) assigns it to the SD or Alpha-N
  branch during SD + Alpha-N operation.
- **Idle VE load source** (`idleVeLoadSource`) independently selects **Model
  default**, **Measured MAP**, **TPS**, or **Effective MAP**.

In combined operation, **Model default** follows the table owner: effective MAP
for SD and TPS for Alpha-N. In standalone operation it follows that strategy's
natural load. Idle VE does not offer accelerator pedal or cylinder filling.

Ownership and lookup source are independent. An SD-owned Idle VE table can use
TPS, and an Alpha-N-owned table can use MAP. The table replaces or interpolates
the VE/reference-filling value inside its owning branch before mass blending.
It has no effect while that branch has zero authority.

The existing idle/taper decision remains in control. Full Idle VE contribution
continues through half of the idle deactivation threshold and returns linearly
to the main table by the full threshold. The existing TPS-based return and the
optional cranking-taper use also remain. Changing the owner or source does not
change idle detection, taper, or blend authority.

If the Idle VE owner is invalid, or if the optional overlay cannot be evaluated,
the overlay is skipped. Each evaluated model continues on its main map and
reports **Fallback active** while idle or taper is active.

There is one Idle VE table. Changing its owner or axis requires manual
recalibration; the firmware does not preserve a second version or convert the
existing bins and cells.

## VE Analyze

### Calibrate separately, then blend

Use **SD + Alpha-N** throughout calibration so each branch uses the same
pressure and correction policies that it will use in the final blend:

1. With the engine stopped, disable separate Idle VE and set **every** cell of
   **% Alpha-N contribution** to **0%**. Start a new session and wait for
   `blendedVeAnalyzeEndpoint = 1` after the initial ten-second delay. VE Analyze
   now targets **Speed Density VE**, with its RPM/effective-MAP axes.
2. Collect the analysis for SD. Apply the resulting calibration with the engine
   stopped. Repeat qualified sessions as needed: writing a tune while running
   ends analysis eligibility for that session.
3. With the engine stopped, set every authority cell to **100%**. Start a new
   session and wait for `blendedVeAnalyzeEndpoint = 2`. Use VE Analyze for
   **Alpha-N VE**, with its RPM/TPS axes, and apply the results while stopped.
4. Once both models are calibrated over the required range, stop the engine
   and set the desired TPS/RPM authority map. The firmware reuses both maps
   directly and blends their calculated masses. VE Analyze is no longer
   eligible in a session with a mixed authority map.

Here, 0% means **100% SD authority** and 100% means **100% Alpha-N authority**.
The percentage is a model contribution, not a VE cell value. Reaching an
endpoint in only the current operating cell does not qualify the session.
Automatic application of VE Analyze changes while running also counts as a
tune write and ends blended analysis eligibility until the next stopped/start
session; this workflow uses collection followed by application while stopped.

### Eligibility and limits

In standalone SD, Alpha-N, or MAF, VE Analyze writes only the selected model's
main map. Enabling Idle VE disables main-map analysis because a delayed lambda
sample may reflect the idle table or its taper.

In SD + Alpha-N, VE Analyze is qualified only at a whole-map endpoint:

- every authority cell must be exactly 0% to analyze **Speed Density VE**;
- every authority cell must be exactly 100% to analyze **Alpha-N VE**.

The endpoint must remain uniform for the entire running session. Any mixed
cell, a live configuration write while the engine is running, a strategy
transition, or a degraded calculation invalidates qualification until the
engine has stopped and a new session starts. Analysis also remains disabled for
the first 10 seconds after cranking. Limp operation can continue, but VE Analyze
does not use samples collected during that session.

`blendedVeAnalyzeEndpoint` reports 0 when unqualified, 1 for SD, and 2 for
Alpha-N. TunerStudio can continue showing VE Analyze tabs or buttons when the
firmware reports 0. Their visibility is not permission to analyze or write a
map.

A lambda residual in a mixed region cannot identify which of the two model maps
is wrong. Do not apply the same residual to both maps. The analyzer's lambda
target follows the configured target-lambda load source.

## Compatibility with upstream tunes

The configuration layout changes to hold the additional maps and selectors.
Existing binary tunes are not migrated. Keep the upstream project, exported
maps, and matching firmware/INI as the reference for a manual transfer. Every
transferred map includes both axes as well as its cells.

Use this mapping to retain the previous mass calculation where possible. It
assumes matched table cells and both axes, displacement, cylinder count, input
values, Idle VE behavior, and common corrections. It does not establish that a
calibration is correct for the engine.

| Previous upstream configuration | Strategy and settings here | Calibration and load-source action |
| --- | --- | --- |
| SD with its native MAP axis | **Speed Density**, **Tcharge** | Copy the VE map and MAP/RPM axes into **Speed Density VE**. Enable **Use MAP estimate table** to retain the old fallback permission, with the same estimate map and transient setting. |
| SD with VE axis overridden to TPS | Standalone **Alpha-N**, **Multiply MAP** on, **Tcharge** | Copy the TPS/RPM VE map into **Alpha-N VE**. With matched effective MAP and temperature, this retains the `VE x MAP / temperature` mass equation. Enable the same MAP-estimate behavior. Explicitly select **Effective MAP** for consumers that previously inherited SD's native load; Alpha-N's default is TPS. |
| Alpha-N with measured IAT and native TPS axis | **Alpha-N**, **IAT**, **Multiply MAP** off, Alpha-N BARO compensation off | Copy the TPS/RPM map into **Alpha-N VE**. The reference-pressure equation is retained for valid IAT, subject to the kelvin-conversion change described above. |
| Alpha-N with fixed 20 degrees C and native TPS axis | **Alpha-N**, **Multiply MAP** off, Alpha-N BARO compensation off; choose **IAT** or **Tcharge** | Copy the TPS/RPM map as a starting point. The model uses 20 degrees C only when the selected temperature and its fallback are unavailable, so a healthy sensor changes the old fixed-temperature behavior. Review and recalibrate accordingly. |
| MAF with its native filling axis | **MAF Air Charge** | Copy the correction map and uncorrected-filling/RPM axes into **MAF correction**. The measured-flow conversion and native map coordinate are retained. |

Valid-input equation matching does not preserve every fallback behavior.
Review measured versus effective MAP separately, verify the 200 kPa and 100%
fuel-load fallback regions, and retain the existing downstream fuel corrections
only where they still apply.

The new selectors need to match each function's previous coordinate when
retaining an existing calibration. Upstream staging followed the lambda/AFR
source; lambda deviation and lambda-monitor thresholds followed the effective
fueling load. These could already be different. A single default selection does
not necessarily preserve every function.

### Behaviors retained from upstream

- The MAF flow conversion, native uncorrected-filling coordinate, and recovery
  from one failed sensor in a dual-MAF installation are retained.
- The 200 kPa MAP and 100% TPS/pedal fuel-load substitutes retain upstream's
  load-override recovery values. The source-validity diagnostics identify when
  a substitute is in use.
- Valid TPS/pedal readings within the configured sensor tolerance remain
  usable. Airmass lookups clamp small endpoint offsets to 0–100%; the default
  sensor tolerance remains −10% to 110%.
- Idle detection and taper, normal priming conditions, and downstream fuel
  enrichments retain their roles. The BMW M73 preset uses its intended 45%
  calibration in the dedicated Alpha-N map.

The important calibration differences are the explicit MAP-estimate permission
(off by default), the removal of fixed-temperature Alpha-N as a selectable
policy, independent downstream sources, and the chosen Idle VE owner and axis.
Alpha-N BARO compensation also needs to be considered together with **Common
barometric correction** to avoid applying the same pressure effect twice.

Copying cells without both axes does not preserve a calibration. Moving between
MAP and TPS, pure and hybrid Alpha-N, or IAT and Tcharge can require
recalibration even when the copied matrix is unchanged.

The firmware validates inputs, axes, and numerical results. Calibration
accuracy, idle bypass behavior at fixed TPS, transient response, and the region
where each model is useful require engine measurements. Each active map needs
strictly increasing axes and coverage of its normal and fallback operating
range.

## Limp operation and automatic recovery

The physical models use the same recovery rule in standalone and SD + Alpha-N
operation. A temporary failure uses a documented fallback where a usable mass
or fuel load can still be produced. The status reports **Fallback active** for
this limp operation, while sensor validity remains truthful. If no usable mass
is available, the status reports **Fuel temporarily unavailable** and new
injections are temporarily inhibited.

Recovery is automatic. Once the fault is corrected, the next fresh, complete,
valid fuel publication returns the status to **Ready** in SD + Alpha-N or
**Standalone** in a standalone physical model, and admits new injection work. A
physical stop and operator rearm are not required after a temporary failure or
strategy change.

Already accepted opening and closing callbacks remain owned by the injection
scheduler and finish normally while admission is closed. New work waits for
current admission. Priming also remains available in SD + Alpha-N under the
normal upstream priming conditions.

### Lambda protection and starting

Lambda protection retains its minimum-RPM condition: unavailable load while
below that threshold does not start a lean-mixture cut. At monitored RPM, an
invalid protection load is treated as a fault. An active cut keeps its
configured RPM/load/TPS restoration conditions while running; a confirmed
engine stop clears the previous run's cut even when no load is available. A
single zero-RPM sample while the engine is still running does not establish a
confirmed stop.

Airmass recovery and lambda-cut restoration are separate conditions. A usable
airmass result does not bypass lambda protection or the other fuel limiters.

### Model and load fallbacks

HPFP configured for measured MAP retains the upstream zero-load fallback when
MAP is unavailable; the 200 kPa override substitute does not apply to this
pressure-target lookup.

The main model fallbacks are:

| Failure | Recovery behavior |
| --- | --- |
| Selected IAT invalid | Use 20 degrees C. |
| Selected Tcharge invalid | Use valid IAT, otherwise 20 degrees C. |
| BARO invalid during Alpha-N compensation | Use coefficient 1. |
| Measured MAP invalid and estimate permitted | Use the estimate; if TPS is also invalid, look up the estimate at 0% TPS. |
| MAP estimate invalid during a transient | Keep valid measured MAP. |
| One required blend branch invalid | Promote the healthy branch to 100% authority. |
| Authority TPS, axes, or table invalid | Try SD at 100% authority; if SD fails, try Alpha-N. |
| Idle VE owner or optional overlay invalid | Skip the overlay and use each evaluated model's main map. |
| One common VE correction invalid | Use a neutral multiplier for that correction only. |
| Neither air model can produce mass | Temporarily inhibit new injection work until a fresh valid publication. |

These fallbacks cannot prove that a surviving map is calibrated for the whole
operating range. Calibrate both SD and Alpha-N for every region where either may
be asked to carry the engine alone. The firmware does not hold the last air-mass
value, and continued operation cannot be guaranteed when both models or the
injection system are unavailable.

## Useful log channels

| Channel | Meaning |
| --- | --- |
| `blendedSdMass`, `blendedAlphaNMass` | Raw mass from each evaluated branch. |
| `blendedRequestedAuthority` | Authority requested by the 8 x 8 map. |
| `blendedEffectiveAuthority` | Authority associated with the valid result. |
| `blendedSdLoad`, `blendedAlphaNLoad` | Captured native coordinate for each branch. |
| `blendedSdVe`, `blendedAlphaNVe` | Model-map value before common corrections. |
| `blendedCorrection` | Product of the common VE corrections. |
| `blendedStatus`, `blendedFault`, `blendedFlags` | Standalone, waiting, ready, temporarily unavailable, or fallback-active state; current cause; and fallback/calculation flags. |
| `airmassTemperature`, `airmassTemperatureSourceUsed` | Temperature and source used by the calculation. |
| `alphaNBaroCoefficient` | BARO coefficient applied to pure Alpha-N. |
| `airmassPressureFlags` | Pressure validity and provenance. |
| `blendedVeAnalyzeEndpoint` | VE Analyze qualification: 0 none, 1 SD, 2 Alpha-N. |
| Per-function `...Load` channels | Coordinate published for each independently selected consumer. |

`blendedFlags` identifies evaluated and valid branches, estimate activity,
calculation validity, and active fallbacks. A skipped branch can show zero in
its mass channel; that means unavailable for this calculation, rather than a
physical zero mass.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| An airmass editor is missing | Confirm that injection is enabled. The editor does not depend on the selected strategy. |
| A main-map cursor stays at zero | Confirm that its model is currently evaluated. The map remains editable while inactive. |
| MAP estimate is not used | Check the general permission, RPM/table validity, and whether the consumer requests Effective MAP. An invalid TPS uses the 0% coordinate whenever an estimate is evaluated and reports fallback. |
| A valid estimate is present but limp remains active | Look for an active fuel consumer configured for Measured MAP, or another fallback still in use. |
| Multiply MAP does not change the blend | This option applies only to standalone Alpha-N. |
| Alpha-N BARO has no effect | Confirm nonzero Alpha-N contribution, valid BARO/reference, and that standalone Multiply MAP is off. |
| A cursor no longer matches a table after changing its source | Check that the correct consumer was changed and recalibrate the shared axis in the new units. |
| Idle VE has no effect | Its owning branch may have zero authority, or idle/taper may be inactive. |
| VE Analyze remains unavailable | Require a uniform 0% or 100% authority map for the full session, disable Idle VE, avoid running tune writes and degraded fallbacks, and wait through the initial delay. Stop and begin a new session after degraded operation. |
| Injection remains inhibited after a sensor recovers | Wait for a fresh complete publication, then check the current cause and other fuel limiters. No rearm is required. |

Use the INI generated for the installed firmware and controller. Validate
temperature behavior, BARO compensation, MAP transients, idle bypass airflow,
and model transitions with vehicle measurements and logs.

For the calculation boundaries and ownership behind these controls, see
[Air estimation and load architecture](../development/airmass-architecture.md).
