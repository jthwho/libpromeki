#!/bin/bash
# roundtrip-codecs.sh — Drive mediaplay through every registered video
# codec/backend combination (TPG → VideoEncoder → VideoDecoder →
# Inspector) and report pass/fail per combination.
#
# Codec/backend pairs are enumerated with `mediaplay --list-codecs video`.
# Only pairs with BOTH encoder and decoder registered for the codec are
# tested — encode-only or decode-only pairs are listed under SKIP.
#
# For every runnable pair the script invokes
#   mediaplay -s TPG \
#             -c VideoEncoder --cc VideoCodec:<NAME:BACKEND> \
#             -c VideoDecoder \
#             -d Inspector \
#             --frame-count <N>
# and classifies the exit code according to mediaplay's documented
# table (0 / 10 / 11 / 12 / 13 / 21).  The pipeline-level --frame-count
# guarantees the Inspector receives exactly N frames (or up to one GOP
# more on interframe-coded paths — MediaPipeline only cuts at keyframe
# boundaries to keep the GOP intact).
#
# Usage:
#   scripts/roundtrip-codecs.sh [-n FRAMES] [-k REGEX] [-v]
#
#     -n FRAMES   Required decoded frames per combo (default 60).
#     -k REGEX    Only test combos whose "codec:backend" label matches
#                 the regex (case-insensitive grep -E).  Repeatable.
#     -v          Verbose — print mediaplay's stderr on every run.
#     -h          Show this help.
#
# Exit status: 0 if every runnable pair passed, 1 if any failed.
# Pairs classified as SKIP (encode-only or decode-only) never count
# as failures.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MEDIAPLAY="$REPO_DIR/build/bin/mediaplay"

FRAMES=60
VERBOSE=false
FILTERS=()

usage() {
        sed -n '2,33p' "$0" | sed 's/^# \{0,1\}//'
}

while getopts "n:k:vh" opt; do
        case "$opt" in
                n) FRAMES="$OPTARG" ;;
                k) FILTERS+=("$OPTARG") ;;
                v) VERBOSE=true ;;
                h) usage; exit 0 ;;
                *) usage; exit 1 ;;
        esac
done

if [ ! -x "$MEDIAPLAY" ]; then
        echo "mediaplay not found at $MEDIAPLAY — run 'build mediaplay' first." >&2
        exit 1
fi

if ! [[ "$FRAMES" =~ ^[0-9]+$ ]] || [ "$FRAMES" -lt 1 ]; then
        echo "-n FRAMES must be a positive integer (got '$FRAMES')." >&2
        exit 1
fi

# No headroom needed anymore: MediaPipeline's frame-count cap drives
# the Inspector's terminal count directly, so asking for N at the
# pipeline level lands exactly N decoded frames at the Inspector (or
# up to one GOP more when the encoder emits interframe-coded output
# — still safe for roundtrip classification).

# Colors — only used when stdout is a tty.
if [ -t 1 ]; then
        RED='\033[0;31m'
        GREEN='\033[0;32m'
        YELLOW='\033[0;33m'
        BLUE='\033[0;34m'
        NC='\033[0m'
else
        RED='' GREEN='' YELLOW='' BLUE='' NC=''
fi

# Classify a mediaplay exit code into a short status label.  The
# numeric codes come straight from mediaplay's --help (Exit codes
# section) and the constants at the top of utils/mediaplay/main.cpp.
classify() {
        case "$1" in
                0)  echo "PASS" ;;
                1)  echo "GENERIC" ;;
                10) echo "BUILD_FAIL" ;;
                11) echo "OPEN_FAIL" ;;
                12) echo "START_FAIL" ;;
                13) echo "RUNTIME_FAIL" ;;
                21) echo "DISCONTINUITY" ;;
                *)  echo "EXIT_$1" ;;
        esac
}

status_color() {
        case "$1" in
                PASS)          printf '%s' "$GREEN" ;;
                SKIP)          printf '%s' "$YELLOW" ;;
                DISCONTINUITY|RUNTIME_FAIL) printf '%s' "$YELLOW" ;;
                BUILD_FAIL|OPEN_FAIL|START_FAIL|GENERIC|EXIT_*) printf '%s' "$RED" ;;
                *)             printf '%s' "$NC" ;;
        esac
}

# Build the list of combinations.  `mediaplay --list-codecs video`
# already filters out codecs with no registered backend, so every row
# here has AT LEAST one direction; we then split rows by enc/dec:
#
#   enc=yes, dec=yes → runnable combination
#   otherwise        → SKIP (can't roundtrip without both directions)
declare -a RUNNABLE=()     # "codec:backend" strings
declare -a SKIP_LABELS=()  # "codec:backend  reason" strings

while IFS=$'\t' read -r kind codec backend enc dec; do
        [ "$kind" = "video" ] || continue
        label="${codec}:${backend}"

        # Apply -k filter(s) if any.
        if [ "${#FILTERS[@]}" -gt 0 ]; then
                matched=false
                for f in "${FILTERS[@]}"; do
                        if echo "$label" | grep -qiE -- "$f"; then
                                matched=true
                                break
                        fi
                done
                if ! $matched; then continue; fi
        fi

        if [ "$enc" = "yes" ] && [ "$dec" = "yes" ]; then
                RUNNABLE+=("$label")
        else
                # Describe what's missing (the opposite direction is
                # what we have; the skip reason is what we don't).
                reason="unknown"
                if [ "$enc" = "yes" ] && [ "$dec" = "no" ]; then reason="decoder not registered"; fi
                if [ "$enc" = "no" ] && [ "$dec" = "yes" ]; then reason="encoder not registered"; fi
                if [ "$enc" = "no" ] && [ "$dec" = "no" ]; then reason="no direction registered"; fi
                SKIP_LABELS+=("$label|$reason")
        fi
done < <("$MEDIAPLAY" --list-codecs video)

TOTAL_RUNNABLE="${#RUNNABLE[@]}"
TOTAL_SKIP="${#SKIP_LABELS[@]}"

if [ "$TOTAL_RUNNABLE" -eq 0 ] && [ "$TOTAL_SKIP" -eq 0 ]; then
        echo "No video codec backends registered in this build." >&2
        exit 1
fi

echo "=== libpromeki codec roundtrip ==="
echo "  mediaplay:       $MEDIAPLAY"
echo "  frames:          $FRAMES"
if [ "${#FILTERS[@]}" -gt 0 ]; then
        echo "  filters:         ${FILTERS[*]}"
fi
echo "  runnable combos: $TOTAL_RUNNABLE"
echo "  skipped combos:  $TOTAL_SKIP"
echo

# Per-combo results captured for the final table.
declare -a RESULTS=()

pass_count=0
fail_count=0

for label in "${RUNNABLE[@]}"; do
        log="$(mktemp /tmp/mediaplay-rt.XXXXXX.log)"
        "$MEDIAPLAY" \
                -s TPG \
                -c VideoEncoder --cc "VideoCodec:$label" \
                -c VideoDecoder \
                -d Inspector \
                --frame-count "$FRAMES" \
                >"$log" 2>&1
        rc=$?
        status="$(classify "$rc")"

        frames="$(grep "Total frames processed" "$log" 2>/dev/null | tail -1 | sed -E 's/.*: ([0-9]+).*/\1/')"
        [ -z "$frames" ] && frames="?"

        detail=""
        if [ "$status" != "PASS" ]; then
                # Pull the last error-style line for context.  mediaplay
                # prints "Error: …" or "Pipeline error at …" to stderr
                # on every failure path so one of those is almost always
                # present.
                detail="$(grep -E '^(Error:|Pipeline error|Inspector reported)' "$log" | tail -1)"
        fi

        color="$(status_color "$status")"
        printf "  [${color}%-13s${NC}] %-28s frames=%s\n" \
                "$status" "$label" "$frames"
        if [ -n "$detail" ]; then
                printf "                  %s\n" "$detail"
        fi
        if $VERBOSE; then
                sed 's/^/                  | /' "$log"
        fi

        RESULTS+=("$status|$label|$frames|$detail")
        if [ "$status" = "PASS" ]; then
                pass_count=$((pass_count + 1))
        else
                fail_count=$((fail_count + 1))
        fi

        rm -f "$log"
done

for row in "${SKIP_LABELS[@]}"; do
        label="${row%|*}"
        reason="${row#*|}"
        color="$(status_color "SKIP")"
        printf "  [${color}%-13s${NC}] %-28s %s\n" "SKIP" "$label" "$reason"
        RESULTS+=("SKIP|$label|-|$reason")
done

echo
echo "=== Summary ==="
echo "  Runnable: $TOTAL_RUNNABLE  (passed: $pass_count, failed: $fail_count)"
echo "  Skipped:  $TOTAL_SKIP"

if [ "$fail_count" -gt 0 ]; then
        exit 1
fi
exit 0
