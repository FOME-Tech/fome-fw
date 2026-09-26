# ADC lifecycle regression and diagnostics

Run all engine and ADC lifecycle tests from `unit_tests`: `make -j12 test`.
The default build includes separate ADCv2 (F4/F7) and ADCv4 (H7) host binaries
with ASan/UBSan by default. The unit-test CI workflow builds all three binaries
in `Build Tests` and runs them through the common `test` target in `Run Tests`
on Linux and macOS.

To build and run only the ADC suites: `make -j12 adc-lifecycle-test`.

## Real callers and HAL

Both binaries include the complete production ADC port and `software_knock.cpp`,
the vendored ChibiOS `hal_adc.c`, and the matching low-level driver headers.
The static fast timer callback on ADCv2 is reached through the GPT configuration
registered by `portInitAdc`; knock is reached through `onStartKnockSampling`, with
the scheduler's system lock represented by the fixture. ADCv4 starts its circular
stream through `readSlowAnalogInputs`. No production test hooks are needed.

Mocks replace MCU registers/MPU, RTOS services and unrelated engine services. The
HAL's assertions, conversion state transitions and half/full/error ISR macros are
real. Tests cover all initial states, configuration guards, completion preemption,
pending knock processing, one result per full buffer, distinct rejection reasons,
HAL errors (including ADCv2 DMA error code zero), recovery, counter wrap, and console
snapshots. Console output is checked for length and for formatting outside the lock.
ADCv4 tests distinguish circular stream starts from completed buffers and preserve
the existing policy: the stream is not automatically restarted after an error.

With the phase-1 guards reverted, either command aborts in `hal_adc.c` with `not ready`:

```
./build/adc_lifecycle_test --gtest_filter=AllStates/AdcStartState.FastTimer/4
./build/adc_lifecycle_test --gtest_filter=AllStates/AdcStartState.KnockScheduler/4
```

The fixtures test software ordering, not physical ADC inputs, DMA timing, interrupt
priorities, RTOS scheduling or filter quality. They cannot establish sample-loss
rates, latency distributions or stability under real IRQ load.

## Using `adc_stats` on an ECU

The console command prints one snapshot in seven lines: a header, then three lines
for each of `fast` and `knock`. It does not clear counters or change configuration.
Example header and fast counters:

```
adc mode=timer started_unit=conversion completed_unit=buffer counters=uint32_since_boot knock_compiled=0
adc fast started=100 completed=100 errors=0 last_error=0 processed=0
adc fast skip_active=0 skip_complete=0 skip_not_ready=0
adc fast skip_no_channels=42 skip_invalid_channels=0 skip_disabled=0 skip_pending=0
```

H7 reports `mode=circular started_unit=stream`: one start can produce many completed
buffers. Its fast counters describe the shared continuous ADC stream, which also
feeds slow sensors. `knock_compiled=0` distinguishes unavailable software knock from
an idle knock ADC. Fast `processed` is unused; knock `processed` counts results
delivered after filtering. Unsupported reasons for either path remain zero.

`started` counts accepted HAL starts, and `completed` counts full-buffer callbacks
only. An error callback increments `errors` and stores the raw architecture-specific
code in `last_error`; it does not publish a completed buffer or change recovery
policy. A raw error value of zero is valid on ADCv2. Starting from `ADC_ERROR` is
allowed by the HAL and does not itself increment `errors`.

Each rejected start increments exactly its first rejection reason, preserving the
original guard order:

- `skip_active`, `skip_complete`: the ADC is still converting or completing.
- `skip_not_ready`: any other state rejected by the existing readiness guard.
- `skip_no_channels`, `skip_invalid_channels`: ADCv2 has zero or too many fast channels.
- `skip_disabled`: software knock is disabled in the active configuration.
- `skip_pending`: the knock processing thread still owns the previous sample buffer.

Skip counters cover only requests that reached the caller and were rejected. They
do not count GPT ticks lost or coalesced while interrupts are masked, or analog
errors. Matching start/completion counts with no errors does not prove that no
samples were lost.

Counters wrap modulo 2^32 (about 4.97 days at 10 kHz). Compute differences modulo
2^32 between snapshots taken less than one wrap apart, and detect reboots separately.
There is no reset command. No calibration or TunerStudio output layout changes.

## Concurrency and cost

The two counter records occupy 96 bytes. Each counter has one serialized writer:
fast timer and knock scheduler starts already hold the system lock; ADC/DMA callbacks
run at `EFI_IRQ_ADC_PRIORITY`; only the knock thread writes `processed`. The F4/F7
configuration uses the same ADC and DMA IRQ priority, and the H7 HAL allocates its
DMA/BDMA handlers at its configured ADC IRQ priority. These IRQs are masked by the
system lock. Taking the 96-byte snapshot also prevents a thread switch, so the
console sees a consistent pair of records.

A successful single conversion adds one increment at start and one at full-buffer
completion. A rejection adds one increment, and an error callback updates a count
and its last code. There are no additional locks, allocations or console writes in
the ADC/scheduler callbacks. The console briefly locks for the copy, then formats
and queues all lines after unlocking. Existing console buffering can still drop
lines under heavy logging; discard incomplete snapshots instead of treating missing
fields as zero.
