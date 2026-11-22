# Pitfalls and baselines by area

What the source won't tell you: how to read the signals, what "normal" looks like in real logs, and
traps that have already cost time. For *how* something works, read the code at the user's firmware
version - each section lists where to start. If behaviour around the event changed recently,
`git log <hash>..HEAD -- <those files>` will say so.

## Log quirks

- **Sample rate vs control rate.** Logs run ~50-100 Hz; the fast callback runs at 250 Hz and the
  trigger code runs once per tooth. Anything that toggles per tick is aliased in the log - judge by
  proportions (`hist`) and correlation, not individual transitions.
- **Logged ≠ used.** Look-alike columns can be different signals, and the code may read the one
  nobody is watching (instant RPM vs RPM, pedal vs TPS on ETB cars, filtered vs raw).
- **Rounding.** Some exports round Time to whole seconds (`info` warns; use `--rows`). Channels
  without field options log at 0 decimals, so a 1.31 multiplier shows as 1.
- **Computed channels.** Some columns are ini formulas, not values the ECU used - `logcol` flags them.
- **Several attempts per file.** Split with `segments --where "RPM > 0"` before computing anything.
- **Cumulative counters.** Many counters persist across starts or since boot - read the change per
  start, not the absolute value.

## Trigger and engine sync

Start at: `controllers/trigger/trigger_decoder.cpp` (wheel decoding, errors), `trigger_central.cpp`
(tooth handling, cam decoding, phase resolution), `getCurrentIgnitionMode()` /
`getCurrentInjectionMode()` (behaviour before full sync), `checkTriggers()` in `sensor_checker.cpp`
(DTCs). Get the trigger setup first:
`fome_msq.py grep MSQ "trigger_type|vvtMode|vvtOffsets|camInputs|engineSyncCam|CamPhaseResolve|isPhaseSyncRequired"`.

- **Two levels of sync.** Crank sync (`Trg: Sync counter` counting) is enough for RPM, wasted spark
  and batch fuel. Full sync (`Trg: Fully synchronized`) needs the cam on a crank-speed wheel; until
  then the engine runs on a *guessed* phase, which is wrong about half the time.
- **Phase guess per start:** `Trg: Resync counter` +0 = guess was right, +1 (with a 360 adjustment) =
  corrected once, still climbing while running = phase flipping (cam noise, wrong `vvtMode`, or cam
  tooth too close to the crank sync point).
- **`Trg: Fully synchronized` dropping mid-run points at the crank, not the cam.** Losing the cam
  doesn't drop full sync; a primary trigger error does.
- **Baselines from a healthy 36-2 + one cam (Atlas):** full sync ~0.2 s after crank sync while
  cranking; above 1000 RPM `Trg: Tooth angle error` within ±0.5° and `instantRpm / RPM` = 1.000.
  Every stall logs **one** crank error (warning 9003) with a big tooth angle error as the crank
  rocks back - normal, ignore it.
- **Wheel definition doesn't match the wheel:** RPM looks fine but `instantRpm / RPM` clusters away
  from 1 (seen: 0.5 and 4-5x) and tooth angle error is large. Noise instead looks random. Errors
  only while cranking suggest battery dip or VR gain; only at high RPM, VR threshold or ignition
  noise.
- **`Error: Trigger` is not a DTC** - it's "recent decode errors". `recentErrorCode N` columns are
  history and collect unrelated codes; look numbers up in `obd_error_codes.h`.
- **A datalog can't show edges.** For anything about tooth timing or where the cam tooth falls,
  ask for a composite logger capture (crank + cam) around the problem, plus the tune.

```
fome_log.py segments LOG --where "RPM > 0" --show "Trg: Resync counter" "Trg: Fully synchronized" "Trg: Sync error counter"
fome_log.py stats LOG "Trg: Tooth angle error" "{instantRpm} / {RPM}" --where "RPM > 1000"
```

## Idle

Start at: `controllers/actuators/idle_thread.cpp` - `IdleTargetController::determinePhase()` decides
idling vs not; `IdleController::getIdlePosition()` computes the output.

- Phase decisions use **instant RPM**, so a bad trigger shows up as idle flapping (see above).
- If `Idle: Target RPM` sits near target + "RPM upper limit" with "Ramp target on return to idle"
  on, the idle-entry timer is being reset constantly - the phase is flapping in and out of Idling.
- Screenshots of the idle dialog hide fields behind visibility conditions; `fome_ini.py label "Idle
  settings"` lists them all.

Worked example: "blipping in and out of idle", RPM 1200 vs target 1300. Phase was Coasting 78% of
the time because instant RPM read ~6200 (4-5x real) and dipped below the entry threshold about once
a revolution. Average RPM was fine; the trigger setup didn't match the wheel. None of the settings
in the screenshot mattered.

## Cranking and startup fueling

Start at: `controllers/algo/fuel_math.cpp` (cranking vs running fuel), `controllers/algo/engine.cpp`
(`getCurrentInjectionMode()`), `controllers/engine_cycle/fuel_schedule.cpp`. Full worked
investigation (2AR, E85, MAF): `mr2_startup_tuning.md` at the repo root, if present.

- Look at each attempt separately: time to fire, peak RPM, whether it ran, fuel injected before it
  ran.
- Fuel can drop 10-20x at the `cranking_rpm` crossover if the running airmass estimate is low.
- **MAF under-reads for ~0.5 s after the catch** (manifold still near baro, MAF only sees flow past a
  closed throttle, plus filter lag) - 20-30% of a MAP-based estimate in the MR2 logs. When RPM then
  falls, the lagging MAF over-reads and floods.
- Lambda in the first second is dominated by cranking fuel coming off the walls; the leanest point
  was a few hundred ms after leaving cranking.
- Failed attempts leave fuel in ports, cylinders and oil - rich running for the next minute may be
  that, not the tune.

## Closed-loop fuel and widebands

Start at: `controllers/math/closed_loop_fuel.cpp`.

- Align fuel with lambda before judging it. Measured on port injection: injection -> combustion
  ~1 cycle while cranking (lambda lean-clipped until the first real combustion); combustion ->
  sensor ~0.1 s; injection -> sensor ~0.1-0.2 s at 1500-2500 RPM; ~0.2-0.3 s plus slow mixing at
  idle.
- Trims pinned at their clamp no longer tell you the size of the error.
- Per-cylinder widebands: identify cylinders by response order (a 360° pair responds one rev apart)
  and check `cylinderBankSelect`. Cylinders agreeing once running means the error is global base
  fuel, not an injector.
- When two effects change together (warm-up and MAF operating point during a cold idle), say the
  log can't separate them and suggest the test that would.
