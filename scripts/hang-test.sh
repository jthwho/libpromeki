#!/usr/bin/env bash
#
# hang-test.sh — Run unit tests repeatedly to detect intermittent hangs
#                and failures.
#
# Usage:
#   scripts/hang-test.sh [OPTIONS] [-- DOCTEST_ARGS...]
#
# Options:
#   -n COUNT       Number of iterations (default: 50)
#   -t SECONDS     Per-run timeout in seconds (default: auto-detected)
#   -e EXECUTABLE  Test executable path (default: build/bin/unittest-promeki)
#   -o DIR         Output directory for logs (default: /tmp/hang-test-TIMESTAMP)
#   -v             Verbose: show every run, not just failures
#   -h             Show this help
#
# Everything after -- is forwarded to the test executable.
#
# On any hang or failure the full verbose output is captured to a per-run
# log file in the output directory.  A summary is written to summary.txt.
#
# Examples:
#   scripts/hang-test.sh                       # 50 runs, auto timeout
#   scripts/hang-test.sh -n 100 -t 15          # 100 runs, 15s timeout
#   scripts/hang-test.sh -- -tc="Thread*"      # test only Thread cases
#   scripts/hang-test.sh -e build/bin/unittest-tui

set -euo pipefail

# ── defaults ──
count=50
timeout_sec=0          # 0 = auto-detect
exe=""
outdir=""
verbose=false
doctest_args=()

# ── locate repo root ──
root=$(cd "$(dirname "$0")/.." && pwd)

# ── parse options ──
while [[ $# -gt 0 ]]; do
        case "$1" in
                -n) count="$2"; shift 2 ;;
                -t) timeout_sec="$2"; shift 2 ;;
                -e) exe="$2"; shift 2 ;;
                -o) outdir="$2"; shift 2 ;;
                -v) verbose=true; shift ;;
                -h) sed -n '2,/^$/{ s/^# \?//; p }' "$0"; exit 0 ;;
                --) shift; doctest_args=("$@"); break ;;
                *)  echo "Unknown option: $1" >&2; exit 1 ;;
        esac
done

# ── resolve executable ──
if [[ -z "$exe" ]]; then
        exe="$root/build/bin/unittest-promeki"
fi
if [[ ! -x "$exe" ]]; then
        echo "Error: executable not found or not executable: $exe" >&2
        echo "       Run 'build' first." >&2
        exit 1
fi

# ── create output directory ──
if [[ -z "$outdir" ]]; then
        outdir="/tmp/hang-test-$(date +%Y%m%d-%H%M%S)"
fi
mkdir -p "$outdir"

# ── helper ──
fmt_ms() { printf "%d.%03ds" "$(($1 / 1000))" "$(($1 % 1000))"; }

# ── auto-detect timeout (3 calibration runs, take max × 2 + 2s headroom) ──
if [[ "$timeout_sec" == "0" ]]; then
        echo "Calibrating timeout (3 warmup runs)..."
        max_ms=0
        for i in 1 2 3; do
                start_ns=$(date +%s%N)
                timeout 120 "$exe" --no-color "${doctest_args[@]}" >/dev/null 2>&1 || true
                end_ns=$(date +%s%N)
                ms=$(( (end_ns - start_ns) / 1000000 ))
                if (( ms > max_ms )); then max_ms=$ms; fi
                printf "  warmup %d: %s\n" "$i" "$(fmt_ms $ms)"
        done
        timeout_ms=$(( max_ms * 2 + 2000 ))
        timeout_sec=$(( (timeout_ms + 999) / 1000 ))
        echo "Auto timeout: ${timeout_sec}s (2x worst warmup + 2s headroom)"
        echo ""
fi

echo "Executable: $exe"
echo "Iterations: $count"
echo "Timeout:    ${timeout_sec}s"
echo "Output dir: $outdir"
if [[ ${#doctest_args[@]} -gt 0 ]]; then
        echo "Extra args: ${doctest_args[*]}"
fi
echo ""

# ── run loop ──
pass=0
hang=0
fail=0
hang_runs=""
fail_runs=""
times_ms=()

for i in $(seq 1 "$count"); do
        logfile=$(printf "%s/run-%03d.log" "$outdir" "$i")
        start_ns=$(date +%s%N)
        # Always run with -s (verbose) so log files have full detail
        timeout "$timeout_sec" "$exe" --no-color -s "${doctest_args[@]}" \
                > "$logfile" 2>&1 || true
        rc=${PIPESTATUS[0]:-$?}
        end_ns=$(date +%s%N)
        ms=$(( (end_ns - start_ns) / 1000000 ))
        times_ms+=("$ms")
        elapsed_fmt=$(fmt_ms $ms)

        # Detect hang: timeout returns 124, or elapsed >= timeout
        if [[ $rc -eq 124 ]] || (( ms >= timeout_sec * 1000 - 100 )); then
                hang=$((hang + 1))
                hang_runs+=" $i"
                # Extract last test case from log
                last_tc=$(grep "TEST CASE:" "$logfile" | tail -1 | sed 's/.*TEST CASE:  //' || echo "unknown")
                printf "  Run %3d: %8s  ** HANG **  last test: %s\n" "$i" "$elapsed_fmt" "$last_tc"
        # Detect test failure
        elif grep -q "Status: FAILURE" "$logfile"; then
                fail=$((fail + 1))
                fail_runs+=" $i"
                # Extract the failing assertion(s)
                failed_line=$(grep "NOT correct\|FAILED" "$logfile" | head -1 | sed 's/\x1b\[[0-9;]*m//g' || echo "unknown")
                printf "  Run %3d: %8s  ** FAIL **  %s\n" "$i" "$elapsed_fmt" "$failed_line"
        else
                pass=$((pass + 1))
                # Remove log for successful runs to save disk space
                rm -f "$logfile"
                if $verbose; then
                        printf "  Run %3d: %8s  ok\n" "$i" "$elapsed_fmt"
                fi
        fi
done

# ── compute stats ──
min_ms=999999999
max_ms=0
sum_ms=0
for t in "${times_ms[@]}"; do
        sum_ms=$((sum_ms + t))
        (( t < min_ms )) && min_ms=$t
        (( t > max_ms )) && max_ms=$t
done
avg_ms=$((sum_ms / count))

IFS=$'\n' sorted=($(printf '%s\n' "${times_ms[@]}" | sort -n)); unset IFS
median_ms=${sorted[$(( count / 2 ))]}
p95_idx=$(( count * 95 / 100 ))
p95_ms=${sorted[$p95_idx]}

# ── summary ──
summary() {
        echo ""
        echo "════════════════════════════════════════════════════════"
        echo " Results: $count runs"
        echo "════════════════════════════════════════════════════════"
        echo "  Pass: $pass   Fail: $fail   Hang: $hang"
        if [[ -n "$hang_runs" ]]; then
                echo "  Hang runs:$hang_runs"
        fi
        if [[ -n "$fail_runs" ]]; then
                echo "  Fail runs:$fail_runs"
        fi
        echo ""
        echo " Timing:"
        echo "    Min: $(fmt_ms $min_ms)"
        echo "    Max: $(fmt_ms $max_ms)"
        echo "    Avg: $(fmt_ms $avg_ms)"
        echo " Median: $(fmt_ms $median_ms)"
        echo "    P95: $(fmt_ms $p95_ms)"
        echo "════════════════════════════════════════════════════════"
        if (( hang > 0 || fail > 0 )); then
                echo ""
                echo " Logs for failed/hung runs are in: $outdir"
                echo " Inspect with:  less $outdir/run-NNN.log"
        else
                # All passed — clean up the output directory
                rmdir "$outdir" 2>/dev/null || true
        fi
}

# Print to terminal and save to file
summary | tee "$outdir/summary.txt"

# Exit non-zero if any hang or failure occurred
if (( hang > 0 || fail > 0 )); then
        exit 1
fi
exit 0
