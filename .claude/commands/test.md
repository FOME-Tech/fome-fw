---
description: Build and run FOME unit tests
argument-hint: [test_filter]
allowed-tools: [Bash]
---

# Unit Tests

Build and run the FOME unit test suite.

The user invoked this command with: $ARGUMENTS

## Instructions

1. Build the unit tests:
   ```
   cd unit_tests && make -j12
   ```

2. If the build fails, report the first compiler error and stop. If the build fails due to a PCH issue, run `make clean` then retry once before stopping.

3. Run the tests. If an argument was provided, use it as a gtest filter:
   - With filter: `./build/fome_test --gtest_filter=$ARGUMENTS`
   - Without filter: `./build/fome_test`

4. Report the test results summary (passed/failed counts). If any tests failed, list the failing test names and their failure messages. If a single test failed, re-run filtering to that one test.
