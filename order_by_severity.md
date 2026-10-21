# Engine-control findings, ordered by severity

Reviewed against FOME firmware sources at `f990e0fa5f` (`master`; production controller code is unchanged from `6b31ebaec8`). The [rusEFI review in PR #10416](https://github.com/rusefi/rusefi/pull/10416/changes) supplied the presentation pattern. Every conclusion below was checked against this fork's source and focused host tests. The tests assert the **observed faulty behavior** and therefore pass until that behavior is fixed.

## Ranking and evidence

Severity describes plausible operational consequence; confidence describes the evidence. **S1 — Critical** means credible unintended torque or immediate severe engine risk. **S2 — High** means a configured engine protection fails or prolonged damaging operation is credible. **S3 — Moderate** means a repeatable, narrower fuel/control error. **S4 — Low** means limited control or diagnostic impact. **Reproduced** means a focused host test exercised the controller path. These tests establish software state, output commands and mock pins, not actual plate travel, manifold pressure, fuel flow, or hardware timing.

| Rank | ID | Severity | Confidence | Finding |
| ---: | --- | --- | --- | --- |
| 1 | FOME-001 | S1 — Critical | Reproduced | Pedal failure can arm anti-lag and command ETB open |
| 2 | FOME-005 | S2 — High | Reproduced | Anti-lag spark skip remains active while anti-lag is disabled |
| 3 | FOME-002 | S2 — High | Reproduced | MAP failure releases an active boost fuel cut |
| 4 | FOME-007 | S2 — High | Reproduced | Injector-duty fuel protection ignores secondary injectors |
| 5 | FOME-003 | S2 — High | Reproduced | Anti-lag ignores its configured maximum duration |
| 6 | FOME-008 | S3 — Moderate | Reproduced | VVT activation drops fractional seconds from its delay |
| 7 | FOME-006 | S3 — Moderate | Reproduced | A queued prime pulse opens injectors after ignition off |
| 8 | FOME-004 | S3 — Moderate | Reproduced | Fuel-pump relay remains commanded on after ignition off |

## FOME-001 — Pedal failure can arm anti-lag and command ETB open

- **Source:** `AntilagSystemBase::isALSMaxThrottleIntentCondition()` in [antilag_system.cpp](firmware/controllers/algo/antilag_system.cpp) substitutes zero for invalid driver intent. For ETB installations, [init_tps.cpp](firmware/init/sensor/init_tps.cpp) proxies driver intent to the accelerator pedal. `EtbController::getSetpointEtb()` substitutes zero for an invalid pedal, then `getSetpointEtbNonTorqueModel()` in [electronic_throttle.cpp](firmware/controllers/actuators/electronic_throttle.cpp) adds `ALSEtbAdd` when anti-lag is active. `checkStatus()` counts only new pedal-fault edges and does not disable on the first invalid reading.
- **Trigger:** Anti-lag is enabled and armed at qualifying RPM/CLT, `ALSMaxTPS` is below the prior valid pedal request, `ALSEtbAdd` is positive, and pedal/driver-intent feedback fails while TPS feedback stays valid. The reproduced case uses non-torque-model ETB control, a 20% prior request and a 20% anti-lag ETB addition.
- **Actual:** Valid 20% intent keeps anti-lag inactive. When pedal and intent become invalid, both read as zero, anti-lag becomes active, and the ETB target remains 20% instead of the normal 1% failed-pedal fallback. With TPS feedback at 1%, the ETB controller issues an opening motor command even while the sensor checker is active.
- **Expected:** Invalid pedal feedback must not satisfy the anti-lag arming condition or add an opening target. The failed-pedal throttle fallback should remain effective.
- **Impact:** A pedal fault can leave throttle commanded open without a valid driver request, creating an unintended-torque risk. The host test proves the target and motor command, not physical plate movement or vehicle torque. Exposure depends on the anti-lag settings and ETB configuration above.
- **Validation:** `EngineControlReview.FailedPedalArmsAntiLagAndRaisesEtbFallback` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) exercises valid intent, invalid intent, an active sensor checker, ETB control and the motor output. Disabling anti-lag with the failed pedal restores the 1% target.
- **Fix direction:** Require valid driver intent before arming anti-lag, suppress its ETB addition on pedal failure, and test the actual motor path through persistent as well as initial pedal faults.

## FOME-005 — Anti-lag spark skip remains active while anti-lag is disabled

- **Source:** `onTriggerEventSparkLogic()` in [spark_logic.cpp](firmware/controllers/engine_cycle/spark_logic.cpp) calls `ALSsoftSparkLimiter.shouldSkip()` and then sets its target from `ALSSkipRatio` for every eligible spark, with no `antiLagEnabled` or `isAntilagCondition` check. `SoftSparkLimiter` in [launch_control.cpp](firmware/controllers/algo/launch_control.cpp) retains that nonzero target and alternates skips when the ratio is 1. The skip-ratio setting is exposed under anti-lag settings in [fome_config.txt](firmware/integration/fome_config.txt).
- **Trigger:** `ALSSkipRatio` remains nonzero in a tune after anti-lag is disabled, or the anti-lag arming conditions are unmet. Ignition is otherwise enabled.
- **Actual:** With a configured ratio of 1 and anti-lag disabled, the first eligible event installs the ratio and schedules its dwell/fire pair. The next eligible event schedules neither action. The same two events with ratio zero schedule both pairs.
- **Expected:** Anti-lag spark skipping must apply only while anti-lag is active. Disabling or disarming anti-lag should clear the skip target before the next eligible spark.
- **Impact:** Unrequested spark loss can cause severe misfire, hesitation or a stall while driving. The host test proves omission of scheduled dwell/fire actions; it does not establish how a particular engine or exhaust responds.
- **Validation:** `EngineControlReview.InactiveAntiLagStillSkipsSpark` and the zero-ratio control `EngineControlReview.ZeroAntiLagSkipRatioSchedulesBothSparks` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) run the production trigger spark scheduler with a mock executor. They observe two scheduled actions with ratio 1 versus four with ratio zero across two eligible events.
- **Fix direction:** Set the ALS skip target to zero whenever anti-lag is inactive, and update the target before calling `shouldSkip()`. Cover disable, disarm and re-arm transitions through actual spark scheduling.

## FOME-002 — MAP failure releases an active boost fuel cut

- **Source:** `LimpManager::updateCutsBoostCut()` in [limp_manager.cpp](firmware/controllers/limp_manager.cpp) feeds `Sensor::getOrZero(Map)` to the cut hysteresis. The result is included in `allowInjection()` during each `updateState()` call.
- **Trigger:** `boostCutPressure` is configured, MAP has exceeded the cut threshold, and MAP feedback becomes invalid while the engine is still above the boost threshold.
- **Actual:** A valid 95 kPa reading retains the cut after a 105 kPa trip with a 100 kPa threshold. Invalid MAP becomes zero, crosses the 80 kPa release threshold, and injection permission returns. Valid 105 kPa feedback trips the cut again.
- **Expected:** Missing MAP data should not be interpreted as confirmed low manifold pressure and clear an already active boost cut. Recovery needs a deliberate fault policy.
- **Impact:** The configured overboost fuel protection is lost during MAP failure. The test establishes the fuel-permission transition; it does not establish that the physical engine remains above the threshold after the fault or how other fueling fallbacks behave.
- **Validation:** `EngineControlReview.FailedMapReleasesActiveBoostFuelCut` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) exercises trip, valid hysteresis retention, invalid-sensor release and valid-sensor retrip.
- **Fix direction:** Make boost-cut hysteresis validity aware. Preserve the cut across invalid feedback or explicitly enter a fault state with a defined safe recovery condition.

## FOME-007 — Injector-duty fuel protection ignores secondary injectors

- **Source:** [engine2.cpp](firmware/controllers/algo/engine2.cpp) calculates `injectionDuration` and `injectionDurationStage2` separately. [fuel_math.cpp](firmware/controllers/algo/fuel_math.cpp) exposes both `getInjectorDutyCycle()` and `getInjectorDutyCycleStage2()`. `LimpManager::updateCutsInjectorDuty()` in [limp_manager.cpp](firmware/controllers/limp_manager.cpp) checks only the primary result against the instant and sustained duty limits. [fuel_schedule.cpp](firmware/controllers/engine_cycle/fuel_schedule.cpp) independently schedules secondary injector pulses when staging is active.
- **Trigger:** Staged injection is enabled and a secondary injector pulse exceeds the configured duty limit while the primary pulse remains below it. This can occur with a smaller secondary injector, unequal stage flows or a high stage-2 fraction.
- **Actual:** At 3000 RPM with sequential injection, a 35 ms secondary pulse is 87.5% duty and a 2 ms primary pulse is 5%. With a 60% instant limit, the limiter still permits injection. Moving the same 35 ms pulse to the primary stage trips the cut.
- **Expected:** Injector-duty protection should consider the greatest duty of every active injector stage, using durations representative of scheduled output.
- **Impact:** Secondary injectors can saturate without the configured protection acting, potentially causing a lean mixture under load and engine damage. The host test proves the duty calculation and fuel-permission result; it does not establish physical injector flow or mixture.
- **Validation:** `EngineControlReview.SecondaryInjectorDutyDoesNotTripFuelCut` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) compares the secondary over-limit state with an otherwise equivalent primary over-limit control through the production `LimpManager`.
- **Fix direction:** Apply instant and sustained limits to both active stages, with coverage for stage transitions, trims and actual scheduled pulse durations.

## FOME-003 — Anti-lag ignores its configured maximum duration

- **Source:** The user-facing `ALSMaxDuration` setting appears in [fome_config.txt](firmware/integration/fome_config.txt) and is assigned a default in [default_base_engine.cpp](firmware/controllers/algo/defaults/default_base_engine.cpp). `AntilagSystemBase::update()` in [antilag_system.cpp](firmware/controllers/algo/antilag_system.cpp) tests only enable, RPM, CLT, throttle intent and switch conditions. No production controller reads `ALSMaxDuration`; `ALSTimerCondition` is exposed but never set. The active condition feeds ETB, fueling and ignition corrections.
- **Trigger:** Anti-lag remains enabled with its arming inputs continuously satisfied longer than the configured maximum duration.
- **Actual:** With a 1-second maximum, anti-lag remains active after five simulated seconds and `ALSTimerCondition` remains false.
- **Expected:** Anti-lag should expire at its configured duration, with a defined re-arm condition.
- **Impact:** Prolonged anti-lag can sustain enrichment, timing changes and ETB addition beyond the calibrated limit, plausibly increasing exhaust/turbo heat and engine stress. The host test establishes persistent software activation, not component temperature or damage.
- **Validation:** `EngineControlReview.AntiLagContinuesPastConfiguredTimeout` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) runs the production anti-lag state machine before and after the configured timeout.
- **Fix direction:** Track continuous anti-lag activation time, enforce `ALSMaxDuration`, and test timeout, deactivation and re-arm behavior.

## FOME-008 — VVT activation drops fractional seconds from its delay

- **Source:** `VvtController::onFastCallback()` in [vvt.cpp](firmware/controllers/actuators/vvt.cpp) compares elapsed seconds with `vvtActivationDelayMs / MS_PER_SECOND`. Both operands are integers, so the configured millisecond delay is truncated to whole seconds before the comparison. The setting in [fome_config.txt](firmware/integration/fome_config.txt) describes this delay as time for oil pressure to build before VVT diverts flow.
- **Trigger:** VVT control is configured with a non-whole-second activation delay, such as 500 ms, and its other RPM/temperature and feedback conditions are satisfied.
- **Actual:** A 500 ms setting becomes zero seconds. At 250 ms after entering the running state, the controller permits a nonzero VVT output. At the same runtime, a 1000 ms setting still blocks the output.
- **Expected:** VVT output should remain inhibited until the configured millisecond delay has elapsed.
- **Impact:** The VVT solenoid may begin consuming oil flow before the intended pressure-build interval has finished, and cam timing can change earlier than calibrated. The host test establishes the output gate; it does not measure oil pressure or cam motion.
- **Validation:** `EngineControlReview.VvtActivationDelayDropsFractionalSeconds` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) uses the production VVT callback and output path at 250 ms after engine start, with 500 ms and 1000 ms configurations as controls.
- **Fix direction:** Compare elapsed time in milliseconds or convert the configured duration to floating-point seconds before division. Test fractional and whole-second settings near their boundaries.

## FOME-006 — A queued prime pulse opens injectors after ignition off

- **Source:** `PrimeController::onIgnitionStateChanged(true)` in [prime_injection.cpp](firmware/controllers/engine_cycle/prime_injection.cpp) queues the priming callback after the configured delay. Its ignition-off branch returns without canceling the queued action. `onPrimeStart()` checks only pulse duration, then calls `startInjection()` directly. The callback does not recheck ignition state or `LimpManager` injection permission.
- **Trigger:** A nonzero priming delay leaves a queued prime start when ignition-off notification is delivered. The reproduced setting uses a one-second configured delay and ignition off after 250 ms, allowing for the normal 200 ms low-voltage debounce in [ignition_controller.cpp](firmware/controllers/ignition_controller.cpp).
- **Actual:** The queued callback fires at 1.1 seconds after the original ignition-on notification and opens the injector output despite the intervening ignition-off notification. The matching end callback closes the output 10 ms later.
- **Expected:** Ignition off should cancel the pending prime or cause its callback to decline injection. A prime that has already started should still receive a matching close.
- **Impact:** A delayed prime can deliver an unrequested all-cylinder fuel pulse after key-off, with possible fuel accumulation during repeated attempts. The host test proves injector output state, not physical fuel flow. Exposure requires a delayed prime and a key-off interval before the callback.
- **Validation:** `EngineControlReview.IgnitionOffDoesNotCancelQueuedPrimeInjection` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) uses the real scheduler and prime controller with a mocked injector-duration model. It confirms the queued event survives ignition off, opens injector 1 and subsequently closes it.
- **Fix direction:** Track ignition state in the prime controller, cancel or guard the queued start on ignition off, and preserve a close callback for any pulse already open.

## FOME-004 — Fuel-pump relay remains commanded on after ignition off

- **Source:** `FuelPumpController::onIgnitionStateChanged(false)` in [fuel_pump.cpp](firmware/controllers/modules/fuel_pump/fuel_pump.cpp) records `ignitionOn = false` but does not clear the prime timer or gate the relay. `onSlowCallback()` commands the relay from prime time, recent trigger activity or forced state without checking ignition state. [ignition_controller.cpp](firmware/controllers/ignition_controller.cpp) distributes the ignition-off event after its low-voltage debounce.
- **Trigger:** Ignition goes off during the configured pump-prime interval, or just after a trigger event. The slow callback continues running.
- **Actual:** The relay command stays on after the controller records `ignitionOn = false`. In the default four-second prime configuration, it remains on until the prime interval expires; recent trigger activity independently holds it on until the engine-motion window expires.
- **Expected:** A normal ignition-off event should deassert the pump relay promptly, unless an explicit service/force mode is active.
- **Impact:** The ECU continues requesting fuel-pump operation after key-off. The test checks the mock output pin, not actual pump power; board wiring or a main relay may also remove pump power. This limits the physical-risk claim.
- **Validation:** `EngineControlReview.FuelPumpKeepsPrimingAfterIgnitionOff` and `EngineControlReview.FuelPumpRemainsOnAfterIgnitionOffWhileTriggerRecent` in [test_engine_control_review.cpp](unit_tests/tests/test_engine_control_review.cpp) exercise the two independent hold-on paths.
- **Fix direction:** Gate normal prime and engine-motion pump commands with ignition state and explicitly define how force mode behaves after key-off.

## Test result and scope

From `unit_tests`, the following commands build and run the reproductions:

```sh
make -j4
build/fome_test --gtest_filter='EngineControlReview.*'
build/fome_test
```

The ten new tests passed **10/10**, and the full host suite passed **758/758** after the latest addition. The test file is registered in [tests.mk](unit_tests/tests/tests.mk). The pre-existing `injection_mode_transition.cpp` work was left untouched.

This review traced anti-lag/ETB, boost protection, injector duty, ignition scheduling, prime injection, fuel-pump and VVT paths. It does not establish that the remaining firmware is defect-free. Hardware integration and board-specific power paths were not exercised.
