# Engine-control defect review

## Goal

Inspect this FOME firmware revision for defects that can affect engine control. Reproduce credible findings with focused host unit tests and rank them by operational severity in `order_by_severity.md`. Use [rusEFI PR #10416](https://github.com/rusefi/rusefi/pull/10416/changes) as a report-format reference only; establish every finding from this repository's code and test behavior.

## Five-hour allocation

| Work | Time | Output |
| --- | ---: | --- |
| Trace engine-control and protection paths | 2 hours | Reachable trigger conditions and candidate defects |
| Build focused reproductions and run tests | 2 hours | Tests that assert observed faulty behavior, with controls |
| Rank findings and write the report | 1 hour | Source, trigger, actual/expected behavior, impact, evidence, and fix direction for each finding |

## Review rules

- Prioritize unintended actuation, loss of engine protection, fuel/spark scheduling, and unexpected loss of propulsion.
- Rank severity separately from confidence. A host test can establish firmware state and commands, but not physical torque, fuel volume, or hardware timing.
- Record exact preconditions and avoid ranking a speculative physical outcome as proven.
- Leave production behavior unchanged so the reproductions remain reviewable. Preserve pre-existing worktree changes.

## Completion criteria

- Each ranked finding has a source path, a plausible engine-control consequence, and a focused test or clearly identified source-only limitation.
- The report gives the test command and observed result, including any failing build or test limitation.

## Review output

The review traced anti-lag/ETB, boost protection, injector duty, ignition scheduling, prime injection, fuel-pump and VVT paths. It has yielded eight reproduced findings and ten focused tests. See [order_by_severity.md](order_by_severity.md) for ranking, evidence and validation results.
