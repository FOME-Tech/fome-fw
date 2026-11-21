---
name: log-analysis
description: Diagnose engine/ECU behaviour from a FOME datalog and tune by reading them against the firmware source - "why does it do X in this log". Use this whenever the user supplies or points at a datalog (.csv, .msl, .mlg), a tune (.msq), or a TunerStudio screenshot and asks why the engine/ECU behaved some way - idle hunting or dropping out of idle, hard starts, stalls, rich/lean, trims, misfires, knock, boost, ETB, trigger/RPM glitches - including user bug reports forwarded with "here's the log". Also use it for multi-session tuning investigations on a car.
---

# Datalog + tune analysis against the source

The user brings a symptom ("it blips in and out of idle", "takes three tries to start cold"), usually
with a log, sometimes a tune, sometimes only a screenshot. The job is to find **which code path
produced the behaviour and which input or setting drove it**, prove it with numbers from the log, and
say what to change (tune, wiring/sensor, or firmware).

The advantage you have over a tuner staring at the log is the source. Use it: the answer is almost
always "function F decided X because input Y had value Z", and you can read F.

## Tools

Three stdlib-only scripts in `scripts/` (run with `python3`, paths relative to this skill dir). Logs
are often 10k-100k rows x 700 columns, so never `cat` or `head` them raw - query them.

- `fome_log.py` - datalogs, both TunerStudio text (.csv/.msl) and binary MLG (.mlg).
  - `info LOG` - firmware signature + git hash, duration, sample rate, markers, time-resolution warnings
  - `cols LOG [regex...] [--varying]` - find columns; `--varying` hides the hundreds that never change
  - `rows LOG COL... [--from T --to T | --rows A:B] [--where EXPR] [--changes] [--every N]`
  - `stats LOG COL...` - min/p5/median/p95/max/mean per column, with the same row filters
  - `hist LOG COL` - value counts (enums, flags) or a binned histogram
  - `segments LOG --where EXPR --show COL...` - spans where a condition holds: engine starts
    (`RPM > 0`), each visit to an idle phase, every time a flag is set...
  - Any column argument can be an expression: `"{instantRpm} / {RPM}"`, `"{Idle: Phase} == 1"`. Bare
    identifiers work when the column name is one. Columns resolve exactly, then case-insensitively,
    then by unique substring. Bit fields logged as On/Off read as 1/0.
- `fome_msq.py` - tunes.
  - `info MSQ` (signature, board, git hash), `get MSQ NAME...` (tables/curves printed against their
    axes), `grep MSQ REGEX`, `find MSQ "UI label or dialog/menu title"`, `diff A B`.
- `fome_ini.py` - translation between what people see and what the code calls it, via the generated
  TunerStudio ini. Put `--board B` / `--rev HASH` after the subcommand.
  - `label "TPS threshold"` or `label "Idle settings"` - UI label or whole dialog -> config fields,
    visibility conditions, tooltips
  - `field NAME` - ini definition, tooltip, which dialog/table shows it, `fome_config.txt` line, code refs
  - `logcol "Idle: Phase"` - datalog column -> output channel -> struct `.txt` definition -> code that
    writes it
  - `table NAME` - a table/curve's axes

The ini is built by `make` and reflects the *current* checkout; if it's missing, build the firmware
(`make -j12` in `firmware/`).

## Workflow

### 1. Pin down the claim and the evidence

Restate the symptom as something you can test against the log: "phase leaves Idling while RPM is
below target", "fuel per cylinder drops 20x in the first 0.5 s after cranking". Note what the user
believes ("it's below target so it should stay in idle") - that belief is often the very assumption
that's wrong, and the answer should address it directly.

Inventory what you have. Screenshots are partial tunes: transcribe the values that matter, and notice
what the dialog *doesn't* show - `fome_ini.py label "<dialog title>"` lists every field the dialog
can contain, including ones hidden by visibility conditions, which are often relevant.

### 2. Identify the firmware the log came from

`fome_log.py info` / `fome_msq.py info` print the signature and a git hash when there is one (e.g.
`FOME atlas 20261110@5571527`). The behaviour you are explaining is that build's, not HEAD's:

- `git log -1 <hash>` to confirm it exists here; `git diff <hash> HEAD -- <files you read>` to see if
  the logic you're reading changed since. If it did, read it at that version: `git show <hash>:<path>`.
- No hash (a log exported without a header, like a bare TS CSV)? Infer from the columns present and
  say that you're assuming roughly current code.
- A hash not in this repo means a rusEFI or fork build - say so, and be careful about divergence.

### 3. Survey the log

`info`, then `cols LOG <topic keywords> --varying` to see what's actually moving. Find the moments
the symptom happens (`segments`, `hist`, `rows --changes`), and look at a few dozen rows around one
or two of them. Get a feel for sample rate and timestamp resolution before reasoning about timing -
`info` warns when Time is rounded so several rows share a timestamp (use `--rows`, not `--from/--to`,
then).

### 4. Read the code that makes the decision

Start from the output the user is complaining about and walk backwards:

1. Find where it's computed. `fome_ini.py logcol "<column>"` takes you from a log column to the
   struct field and the line that assigns it. From there, read the function that decides it.
2. List **every input** that function reads - each sensor, each config field, each piece of internal
   state (timers, latches, hysteresis flags) - and every branch that can produce the bad outcome.
3. Map each input to a log column (`logcol`, or `cols LOG <name>`) and each config field to a tune
   value (`fome_msq.py get`, or the screenshot).

Be precise about *which* signal the code reads. Two log columns that look alike are often different
signals, and the code may read the one nobody is looking at: idle phase uses `instantRpm`, not `RPM`;
ETB idle reads the pedal, not TPS; a "filtered" sensor may lag the raw one. That mismatch was the
whole answer in the idle example below.

Also check for multiple writers or callers: if two modules call the same stateful function per tick,
the logged value is whichever ran last, and latches/timers get poked twice.

### 5. Test every branch against the data

For each condition that could produce the symptom, evaluate it over the log with an expression and
see whether it's true when the symptom happens and false otherwise:

```
fome_log.py hist LOG "{Idle: Phase}"
fome_log.py rows LOG RPM instantRpm "Idle: Phase" "{instantRpm} > {Idle: Exit threshold}" --rows 3000:3020
fome_log.py stats LOG "{instantRpm} / {RPM}" --where "RPM > 500"
```

Rule things out with numbers, not by assertion ("TPS is -0.2%, threshold 5% - not the cause"). When
one condition explains every occurrence, check the counter-case too (the rows where the symptom
*doesn't* happen should have it false). If nothing in the code explains the data, consider that
the log is lying: wrong units/scale, a logged value that's rounded (some channels log 0 decimals), a
value computed in the ini rather than sent by the ECU (`logcol` flags these), or sampling far slower
than the control loop so fast toggling is aliased.

### 6. Classify the root cause and recommend

- **Tune** - a setting does what it says, but the value is wrong for this engine. Give the UI label,
  the field name, current and suggested value, and why.
- **Input** - a sensor, wiring or trigger setup feeds the code bad data (the code is fine given its
  input). Say which signal is wrong, how you know, and what data would confirm it (tooth log, scope,
  a specific channel logged at a higher rate).
- **Firmware** - the logic is wrong, or too fragile given realistic inputs. Point at `file:line`,
  describe the fix, and offer to make it (with a unit test reproducing the logged scenario).

Often it's more than one: bad input plus logic that trusts it too much. Say both.

If you can't be sure without more data, say exactly what to ask the reporter for and why each item
would settle it.

### 7. Report

Lead with the answer in a sentence or two, in terms of the user's belief ("it's not the throttle or
speed settings - the idle state machine reads instant RPM, which is ~5x real RPM in this log").
Then:

- A handful of log rows that show it (not a wall - 5-10 rows, aligned, with the deciding columns)
- The code path with `file:line`, and the condition that fires
- What you ruled out, briefly, with the numbers
- Recommendations, split by tune / input / firmware
- What to ask the reporter for, if anything

Keep secondary observations short and clearly secondary.

## Multi-session investigations

When the analysis spans several logs or sessions (tuning someone's own car, a back-and-forth with a
reporter), keep a running notes file at the repo root (untracked), e.g. `mr2_startup_tuning.md`, with
logs under `tuning/`. Offer to start one. Structure that worked well:

- The car (engine, fuel, hardware, fuel/air model) and the goal in the owner's words
- A table of the relevant tune settings and which `.msq` they came from
- One section per log: conditions, a results table, findings with numbers, open questions
- Tune changes made, proposed changes (not yet applied), next tests to run
- Firmware work: done (commit hashes, tests), in progress, roadmap ranked by payoff

Update it as findings land; it is the source of truth for the next session, so record numbers and
reasoning, not just conclusions.

## Domain notes

`references/recipes.md` has, per area (log quirks, trigger and engine sync, idle, startup fueling,
closed-loop fuel and widebands), where in the source to start, how to read the key signals, healthy
baselines from real logs, and traps that have already cost time. Read the section for the area
you're in before step 4.

When an investigation turns up something the source doesn't tell you - a baseline, how to read a
signal, a trap - add it there. Don't add descriptions of how the code works; read the code instead,
because a copy goes stale and then misleads.
