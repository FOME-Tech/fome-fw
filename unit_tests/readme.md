TL, DR: just follow [tests](tests) folder as examples.

1. Run 'make' to build desktop binary.
1. Execute fome_test binary on your PC/Mac, it's expected to say SUCCESS and not fail :) Googletest will also print results summary.
1. To run only one test, run like this: `build/fome_test --gtest_filter=MyTestName`

In this folder we have rusEFI unit tests using https://github.com/google/googletest

Unit tests are not aware of ChibiOS or ARM or else, they are just plain C/C++ which you build for your desktop, not your MCU.



# Simulated engine harness

For tests of anything that depends on the engine turning (trigger decoding, RPM, spark and injection
scheduling), prefer the harness in [harness](harness) to hand-firing teeth with delays:

- `EngineSim` spins a model engine at whatever speed profile you describe and feeds the firmware
  correctly timed crank and cam edges, with glitches, missing teeth or a moving cam on request.
- `OutputRecorder` records what the coils and injectors really did, with the true crank angle.
- `engine_invariants.h` has ready-made checks such as `noOverdwell`, `sparkTimingWithin` and
  `rpmNeverZero`.

See [tests/trigger/test_engine_invariants.cpp](tests/trigger/test_engine_invariants.cpp) for examples.

[Code Coverage Report](https://rusefi.com/docs/unit_tests_coverage/)

See also [https://github.com/rusefi/rusefi/wiki/Build-Server-and-Automation](https://github.com/rusefi/rusefi/wiki/Build-Server-and-Automation)

# Triggers Images

Trigger images generation is still a two-step manual process:

Step 1: Invoke unit_tests. One of the unit_tests artifacts is triggers.txt

Step 2: Once we have triggers.txt updated by unit_tests we can invoke firmware/gen_trigger_images.bat in order
to generate actual trigger images.
