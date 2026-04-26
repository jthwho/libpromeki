# Contributing to libpromeki

This document is the day-to-day reference for working on libpromeki:
the local development loop, what every commit must pass before going
in, and the expectations for code style, tests, and documentation.

For build configuration / feature flags / install / downstream
integration, see [docs/building.md](docs/building.md).  For the
project's coding conventions in detail, see
[CODING_STANDARDS.md](CODING_STANDARDS.md).

---

## Quick Start

```sh
git clone --recurse-submodules https://github.com/jthwho/libpromeki.git
cd libpromeki
cmake -B build
cmake --build build -j$(nproc)
```

Day-to-day rebuild:

```sh
build                          # via the repo's `build` helper, or
cmake --build build -j$(nproc) # plain CMake
```

---

## Development Loop

`cmake --build build` (or `build`) produces only the libraries,
demos, and utilities — **not** the unit-test executables.  This
keeps incremental builds fast when you're iterating on something
that doesn't touch tests.  Two umbrella targets bring tests in:

| Target | What it does |
|--------|--------------|
| `tests` | Builds `unittest-promeki`, `unittest-tui`, `unittest-sdl` |
| `check` | Builds the test execs, then runs the full ctest suite |

```sh
build check                                # via the helper
cmake --build build --target check         # plain CMake
```

For a fast iteration cycle on a single test, build the relevant test
exec and run it directly with doctest's `--test-case=` filter:

```sh
build unittest-promeki
./build/bin/unittest-promeki --test-case='String*'
```

---

## Code Formatting

Every owned source file is formatted with `clang-format` against the
`.clang-format` rules at the repo root.  Two CMake targets cover the
flow:

```sh
cmake --build build --target format-tree         # rewrite in place
cmake --build build --target format-check-tree   # dry-run, errors on diff
```

`format-check-tree` is the gate run by `scripts/precommit.sh` — every
commit must pass it.  Vendored code under `thirdparty/` is excluded.

---

## Pre-commit Verification

`scripts/precommit.sh` is the single entry point that gates every
commit.  Run it before `git commit` and only commit if it exits with
PASS.  It runs:

1. `format-check-tree` — clang-format conformance over the source
   tree.
2. A fresh `cmake -S . -B <tmp>` configure with **all auto-detected
   modules enabled** and `PROMEKI_WARNINGS_AS_ERRORS=ON`.
3. The full default build (`cmake --build <tmp>`) — must complete
   with zero warnings on our targets.
4. `cmake --build <tmp> --target check` — builds the unit-test
   binaries and runs the full ctest suite serially.
5. `cmake --build <tmp> --target docs` if Doxygen is on `PATH`, so
   doxygen issues surface before commit.

By default the build directory is `build-precommit-<timestamp>` at
the repo root, and it is cleaned up on success.

```sh
scripts/precommit.sh                 # the standard run
scripts/precommit.sh --keep          # keep the build dir on success
scripts/precommit.sh --no-docs       # skip the Doxygen step
scripts/precommit.sh --build-dir DIR # explicit build dir
scripts/precommit.sh --jobs N        # override -j
```

Template-heavy C++ TUs in this codebase budget at roughly 1.5 GB of
resident memory per job, so the safe ceiling is `mem_avail / 1.5 GB`
parallel jobs.  The script holds a non-blocking flock on
`.precommit.lock` at the repo root, so a second precommit invocation
while one is running exits immediately with an error rather than
queueing up.  When you're running another long build by hand, hold
off on precommit until that finishes — one at a time.

The script exits non-zero on the first failed step and prints a
clear summary at the end.

---

## Warning Policy

`-Wall -Wextra` is always applied to *our* targets (libraries, demos,
utilities, tests).  Vendored third-party code is intentionally
exempt.

`-Werror` is opt-in via `PROMEKI_WARNINGS_AS_ERRORS=ON`.  Default off
so a new compiler version's freshly-introduced warning category
cannot break a clean local build out of the blue;
`scripts/precommit.sh` flips it on so the gating build is strict.

A small set of well-known noisy warnings are project-wide disabled:

- `-Wno-unused-parameter` — virtual-method overrides routinely ignore
  inherited parameters.
- `-Wno-maybe-uninitialized` — GCC's interprocedural pass produces
  false positives on `std::variant` storage and heavily inlined
  templates.  `-Wuninitialized` (the reliable intra-function form)
  stays on via `-Wall`.
- `-Wno-missing-field-initializers` — designated-initializer struct
  literals (`FormatDesc{.create = ..., .configSpecs = ...}`)
  intentionally elide fields that should value-initialize.

If you find a warning category that's noisy without surfacing real
bugs, add a `-Wno-...` (with a comment explaining why) to
`promeki_warnings` in the top-level `CMakeLists.txt`.  If you find
one that surfaces real bugs we keep silencing, fix the root cause
instead.

---

## Coding Standards

See [CODING_STANDARDS.md](CODING_STANDARDS.md) for the full document.
Key high-level rules:

- 8-space indentation, no tabs.  Enforced by `clang-format`.
- Error reporting standardized on `Error *err` out-parameters.
- Library-first: common logic belongs inside `promeki`; demos and
  utilities are thin façades over library APIs.
- Prefer the library's own classes (`String`, `List`, `Map`, ...)
  over `std::*` equivalents.
- Designated initializers welcome.  Comment lines should explain
  *why*, not *what*.

---

## Recommended Helpers

Several Claude Code agents are wired up for the common pre-commit
review steps.  They are advisory, not gating — `scripts/precommit.sh`
is the single hard gate — but they catch a lot before review:

| Agent | When to invoke |
|-------|----------------|
| `cpp-changeset-reviewer` | Before commit, to scan staged + unstaged changes for bugs and standard violations |
| `doxygen-review` | After API changes, to verify Doxygen completeness |
| `test-coverage-enforcer` | After non-trivial code changes, to confirm new code paths have tests |
| `checkpoint` | Orchestrates the above three plus `commit-msg-writer` |

Run `checkpoint` (or invoke the individual agents) when you're
ready to commit.  Do not run `checkpoint` proactively without being
asked.

---

## Commit Hygiene

- One logical change per commit; do not bundle unrelated edits.
- Commit messages explain the *why*, not the *what* — the diff
  already shows what changed.
- Land foundation work (types, abstractions) before the visible
  feature that depends on it; don't collapse layers to ship sooner.
- The `commit-msg-writer` agent generates a `COMMIT_MSG` file you
  can use as the basis of your commit.  Never let an agent commit
  for you — always review the message and run `git commit` yourself.

---

## See Also

- [docs/building.md](docs/building.md) — complete build / install /
  downstream-integration reference
- [CODING_STANDARDS.md](CODING_STANDARDS.md) — code style and naming
- [docs/debugging.md](docs/debugging.md) — debug logging, build
  types, crash handler
