# Valgrind cleanup

**Status:** COMPLETE (2026-04-23). `unittest-promeki` under
`scripts/run-valgrind.sh` reports `ERROR SUMMARY: 0 errors from 0 contexts`.
The full investigation, category-by-category fixes, and the leak-history
table live in git history.

The runner (`scripts/run-valgrind.sh`) and suppression file
(`scripts/valgrind.supp`) are the artifacts kept for future regression
sweeps.

## Remaining follow-ups

- **Logger singleton thread** shows up as "definitely lost" under
  valgrind even though `Logger::~Logger()` joins the worker thread —
  valgrind's leak check races libstdc++ thread-state teardown.
  Currently suppressed. Revisit only if a real teardown bug surfaces;
  the fix would be a doctest reporter that tears down the Logger
  before `main()` returns.
- **`/tmp` paths in `tests/unit/imagefileio_jpegxs.cpp`** — 32 sites
  that should move under `Dir::temp()` per the project-wide tmp-path
  feedback. Separate test-hygiene sweep, not a valgrind issue.
- **One-off `--track-origins=yes` rerun** if any future change appears
  to introduce uninitialised reads.
