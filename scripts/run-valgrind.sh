#!/usr/bin/env bash
#
# run-valgrind.sh — Run an application under valgrind/memcheck with the
#                   flag set we use for libpromeki investigations, and
#                   tee the full log to disk.
#
# Usage:
#   scripts/run-valgrind.sh [OPTIONS] -- APP [APP_ARGS...]
#   scripts/run-valgrind.sh [OPTIONS] APP [APP_ARGS...]
#
# Options:
#   -o FILE          Log file (default: build/bin/<app>-valgrind.log)
#   -s FILE          Extra valgrind suppression file (can be repeated)
#   -g               Emit --gen-suppressions=all (for crafting new
#                    suppressions; adds a lot of output, use sparingly)
#   -q               Quieter child output (--child-silent-after-exec=yes)
#   -n               Don't follow children (skip --trace-children=yes)
#   -t TOOL          valgrind tool (default: memcheck)
#   -x               Pass --error-exitcode=1 so the script exits non-zero
#                    on any memcheck error (useful in CI)
#   -r               Include "still reachable" in the leak report
#                    (--show-leak-kinds=all).  Off by default — under
#                    --trace-children, the test's atexit handlers don't
#                    always fire before valgrind's leak check, so
#                    process-lifetime registries and caches register
#                    thousands of "reachable" entries that aren't real
#                    leaks.  Pass -r when investigating a real leak.
#   -h               Show this help
#
# Everything after `--` (or after the first non-option argument) is the
# target application and its arguments.
#
# The script always:
#   - tees stdout+stderr into the log file (ascending 2>&1 | tee)
#   - enables --leak-check=full --track-origins=yes
#   - sets --show-leak-kinds=definite,indirect,possible by default;
#     pass -r to also include reachable
#   - bumps --num-callers=40 (default 12 is too shallow for the
#     template/lambda-heavy frames in promeki)
#   - disables the 100-error cutoff with --error-limit=no
#   - follows child processes by default (unittest-promeki forks)
#
# Examples:
#   # Standard full-suite memcheck run, same as the baseline log:
#   scripts/run-valgrind.sh build/bin/unittest-promeki
#
#   # A single doctest case:
#   scripts/run-valgrind.sh -- build/bin/unittest-promeki \
#       -tc="MediaIO::copyFrames: basic TPG -> DPX sequence"
#
#   # Run a small repro app you wrote, with extra suppressions:
#   scripts/run-valgrind.sh -s scripts/valgrind.supp \
#       build/bin/my-repro --frames 5
#
#   # Generate suppression entries for the leaks you see:
#   scripts/run-valgrind.sh -g -o /mnt/data/tmp/promeki/gen.log \
#       build/bin/unittest-promeki

set -euo pipefail

# ── locate repo root ──
root=$(cd "$(dirname "$0")/.." && pwd)

# ── defaults ──
logfile=""
suppressions=()
gen=false
quiet_child=false
trace_children=true
tool="memcheck"
error_exitcode=false
show_reachable=false

# ── parse options ──
app_args=()
while [[ $# -gt 0 ]]; do
        case "$1" in
                -o) logfile="$2"; shift 2 ;;
                -s) suppressions+=("$2"); shift 2 ;;
                -g) gen=true; shift ;;
                -q) quiet_child=true; shift ;;
                -n) trace_children=false; shift ;;
                -t) tool="$2"; shift 2 ;;
                -x) error_exitcode=true; shift ;;
                -r) show_reachable=true; shift ;;
                -h) sed -n '2,/^$/{ s/^# \?//; p }' "$0"; exit 0 ;;
                --) shift; app_args=("$@"); break ;;
                -*) echo "Unknown option: $1" >&2; exit 1 ;;
                *)  app_args=("$@"); break ;;
        esac
done

if [[ ${#app_args[@]} -eq 0 ]]; then
        echo "Error: no application specified." >&2
        echo "       Run '$0 -h' for usage." >&2
        exit 1
fi

# ── sanity-check the target binary ──
app="${app_args[0]}"
if [[ ! -x "$app" ]]; then
        # Try interpreting as a path relative to the repo root.
        if [[ -x "$root/$app" ]]; then
                app="$root/$app"
                app_args[0]="$app"
        else
                echo "Error: target not found or not executable: ${app_args[0]}" >&2
                echo "       Did you forget to run 'build'?" >&2
                exit 1
        fi
fi

# ── choose log file ──
if [[ -z "$logfile" ]]; then
        app_base="$(basename "$app")"
        logfile="$root/build/${app_base}-valgrind.log"
fi
mkdir -p "$(dirname "$logfile")"

# ── check for valgrind ──
if ! command -v valgrind >/dev/null; then
        echo "Error: valgrind not on PATH." >&2
        exit 1
fi

# ── assemble valgrind argv ──
vg_args=(
        --tool="$tool"
        --num-callers=40
        --error-limit=no
)

if [[ "$tool" == "memcheck" ]]; then
        vg_args+=(
                --leak-check=full
                --track-origins=yes
                --read-var-info=yes
        )
        if $show_reachable; then
                # Include "still reachable" blocks in the leak report.
                # Use sparingly: under --trace-children=yes the test
                # process's atexit handlers don't always fire before
                # valgrind's leak check, so function-local-static state
                # in registries / caches shows up here as a sea of
                # ~18,000 entries that are not real leaks.  Pass -r when
                # you actually want to chase a process-lifetime leak.
                vg_args+=(--show-leak-kinds=all)
        else
                vg_args+=(--show-leak-kinds=definite,indirect,possible)
        fi
fi

if $trace_children; then
        vg_args+=(--trace-children=yes)
fi
if $quiet_child; then
        vg_args+=(--child-silent-after-exec=yes)
fi
if $gen; then
        vg_args+=(--gen-suppressions=all)
fi
if $error_exitcode; then
        vg_args+=(--error-exitcode=1)
fi
for s in "${suppressions[@]}"; do
        if [[ ! -f "$s" ]]; then
                echo "Warning: suppression file not found: $s" >&2
        else
                vg_args+=(--suppressions="$s")
        fi
done

# Auto-pick up a project-local suppression file if present.
if [[ -f "$root/scripts/valgrind.supp" ]]; then
        # Skip if the user already passed it explicitly.
        already=false
        for s in "${suppressions[@]}"; do
                [[ "$s" == "$root/scripts/valgrind.supp" ]] && already=true
        done
        if ! $already; then
                vg_args+=(--suppressions="$root/scripts/valgrind.supp")
        fi
fi

# ── show what we're about to do ──
echo "valgrind: $(valgrind --version)"
echo "log:      $logfile"
echo "tool:     $tool"
echo "target:   ${app_args[*]}"
echo

# ── run ──
# 2>&1 | tee "$logfile" is what the user was doing by hand; the
# combined stdout/stderr goes both to the terminal and to the log.
# Use PIPESTATUS so the script still returns the valgrind exit code.
set +e
valgrind "${vg_args[@]}" "${app_args[@]}" 2>&1 | tee "$logfile"
status=${PIPESTATUS[0]}
set -e

echo
echo "Log written to: $logfile"
exit $status
