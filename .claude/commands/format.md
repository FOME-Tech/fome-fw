---
description: Check or apply clang-format code formatting
argument-hint: [check]
allowed-tools: [Bash]
---

# Format Code

The user invoked this command with: $ARGUMENTS

## Instructions

Run from the repo root (`/home/matthew/source/fome-fw` or wherever the repo is).

- If the argument is `check`, run: `./format.sh check`
  - Report which files need formatting, or confirm everything is clean.
- Otherwise (no argument), run: `./format.sh`
  - Apply formatting in-place and report completion.
