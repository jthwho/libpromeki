#!/bin/bash
# rtp-audit.sh — End-to-end RTP round-trip audit for all supported
# uncompressed, JPEG, and JPEG XS video formats.
#
# Each format is tested two ways:
#   1. mediaplay TX → mediaplay RX  (promeki ↔ promeki)
#   2. mediaplay TX → ffmpeg   RX  (promeki → third-party)
#
# For every (format × receiver) combination the script writes three
# files into the output directory:
#
#   <tag>_mp.png   / <tag>_ff.png    — captured frame
#   <tag>_mptx.log / <tag>_fftx.log  — sender log
#   <tag>_mprx.log / <tag>_ffrx.log  — receiver log
#
# where <tag> is a short slug derived from the PixelDesc name
# (e.g. rgb8, uyvy8_422_709, jpeg_422_601f).
#
# Usage:
#   scripts/rtp-audit.sh <output-dir> [options] [-- --ic K:V ...]
#
# Options:
#   -f GLOB     Only run formats whose tag matches GLOB (e.g. "jpeg*",
#               "rgb*", "*422*").  May be repeated to match multiple
#               patterns.  Default: run all formats.
#   -p PORT     Base RTP port (default 10000).
#   -s WxH      Video size (default 1920x1080).
#   -n COUNT    Frames to send per test (default 10).
#   -v          Verbose — print full logs on failure.
#   -h          Show this help.
#
# Everything after '--' is passed verbatim to the mediaplay TX
# command as extra --ic flags.  This lets you override any TPG
# or RTP config key without editing the script:
#
#   scripts/rtp-audit.sh /tmp/audit -- --ic FrameRate:25 --ic VideoPattern:Ramp
#
# Exit code is 0 when every test passes, 1 if any test fails.
# A summary table is printed at the end.
#
# Adding a new format:
#   Append a line to the FORMATS array below.  Each entry is:
#     "<PixelDesc>|<tag>|<receivers>"
#   where <receivers> is a comma-separated list of "mp" and/or "ff"
#   (use "mp" for mediaplay-only when ffmpeg can't decode the format).

set -euo pipefail

# ── Format table ─────────────────────────────────────────────────
# PixelDesc                        | short tag          | receivers
# Receivers: mp = mediaplay, ff = ffmpeg, mp,ff = both
FORMATS=(
        # Uncompressed (RFC 4175)
        "RGB8_sRGB|rgb8|mp,ff"
        "YUV8_422_UYVY_Rec709|uyvy8_422_709|mp,ff"
        "YUV8_422_Rec709|yuyv8_422_709|mp,ff"
        "YUV8_422_UYVY_Rec601|uyvy8_422_601|mp,ff"
        "YUV8_422_Rec601|yuyv8_422_601|mp,ff"

        # JPEG (RFC 2435)
        "JPEG_YUV8_422_Rec601_Full|jpeg_422_601f|mp,ff"
        "JPEG_YUV8_420_Rec601_Full|jpeg_420_601f|mp,ff"
        "JPEG_YUV8_422_Rec709|jpeg_422_709|mp,ff"

        # JPEG XS (RFC 9134)
        "JPEG_XS_YUV10_422_Rec709|jxs_10_422|mp"
        "JPEG_XS_YUV12_422_Rec709|jxs_12_422|mp"
)

# ── Defaults ─────────────────────────────────────────────────────
PORT=10000
SIZE="1920x1080"
FRAME_COUNT=10
VERBOSE=false
TX_EXTRA=()
FILTERS=()

# ── Parse arguments ──────────────────────────────────────────────
usage() {
        sed -n '2,/^$/{ s/^# //; s/^#//; p }' "$0"
        exit 0
}

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
        usage
fi

OUTDIR="$1"; shift

while [ $# -gt 0 ]; do
        case "$1" in
                -f) FILTERS+=("$2"); shift 2 ;;
                -p) PORT="$2"; shift 2 ;;
                -s) SIZE="$2"; shift 2 ;;
                -n) FRAME_COUNT="$2"; shift 2 ;;
                -v) VERBOSE=true; shift ;;
                -h|--help) usage ;;
                --) shift; TX_EXTRA=("$@"); break ;;
                *)  echo "Unknown option: $1" >&2; exit 1 ;;
        esac
done

# ── Locate tools ─────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
MEDIAPLAY="$BUILD_DIR/bin/mediaplay"

if [ ! -x "$MEDIAPLAY" ]; then
        echo "mediaplay not found at $MEDIAPLAY — run 'build' first." >&2
        exit 1
fi

HAS_FFMPEG=true
if ! command -v ffmpeg &>/dev/null; then
        echo "warning: ffmpeg not found — skipping ffmpeg receiver tests." >&2
        HAS_FFMPEG=false
fi

# ── Colours ──────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Counters ─────────────────────────────────────────────────────
PASS=0
FAIL=0
SKIP=0
RESULTS=()

# ── Setup output dir ─────────────────────────────────────────────
mkdir -p "$OUTDIR"
echo -e "${BOLD}RTP round-trip audit${NC}"
echo "  output:  $OUTDIR"
echo "  size:    $SIZE"
echo "  port:    $PORT"
echo "  frames:  $FRAME_COUNT"
if [ ${#TX_EXTRA[@]} -gt 0 ]; then
        echo "  tx args: ${TX_EXTRA[*]}"
fi
echo ""

# ── Wait-with-timeout helper ─────────────────────────────────────
# Waits for a PID to exit, killing it after $2 seconds if it hasn't.
wait_or_kill() {
        local pid=$1
        local timeout=$2
        local elapsed=0
        while kill -0 "$pid" 2>/dev/null; do
                sleep 1
                elapsed=$((elapsed + 1))
                if [ $elapsed -ge "$timeout" ]; then
                        kill "$pid" 2>/dev/null
                        wait "$pid" 2>/dev/null
                        return 1
                fi
        done
        wait "$pid" 2>/dev/null
        return $?
}

# ── Generate SDP ─────────────────────────────────────────────────
# Runs a 1-frame sender to produce the SDP file.
generate_sdp() {
        local fmt="$1"
        local sdp="$2"
        "$MEDIAPLAY" \
                --ic VideoPixelFormat:"$fmt" \
                --ic VideoSize:"$SIZE" \
                --ic AudioEnabled:false \
                ${TX_EXTRA[@]+"${TX_EXTRA[@]}"} \
                -o Rtp \
                --oc VideoRtpDestination:127.0.0.1:$PORT \
                --oc RtpPacingMode:Userspace \
                --oc RtpSaveSdpPath:"$sdp" \
                --frame-count 1 >/dev/null 2>&1
}

# ── TX helper ────────────────────────────────────────────────────
run_tx() {
        local fmt="$1"
        local log="$2"
        "$MEDIAPLAY" \
                --ic VideoPixelFormat:"$fmt" \
                --ic VideoSize:"$SIZE" \
                --ic AudioEnabled:false \
                ${TX_EXTRA[@]+"${TX_EXTRA[@]}"} \
                -o Rtp \
                --oc VideoRtpDestination:127.0.0.1:$PORT \
                --oc RtpPacingMode:Userspace \
                --frame-count "$FRAME_COUNT" \
                >"$log" 2>&1
}

# ── mediaplay RX ─────────────────────────────────────────────────
run_rx_mp() {
        local sdp="$1"
        local png="$2"
        local log="$3"
        "$MEDIAPLAY" \
                "$sdp" \
                -c --cc OutputPixelDesc:RGB8_sRGB \
                -o "$png" \
                --frame-count 1 \
                >"$log" 2>&1
}

# ── ffmpeg RX ────────────────────────────────────────────────────
run_rx_ff() {
        local sdp="$1"
        local png="$2"
        local log="$3"
        ffmpeg -y \
                -protocol_whitelist file,udp,rtp \
                -analyzeduration 2000000 \
                -probesize 10000000 \
                -i "$sdp" \
                -frames:v 1 -update 1 \
                "$png" \
                >"$log" 2>&1
}

# ── Single test driver ───────────────────────────────────────────
#   run_test <PixelDesc> <tag> <receiver>
# where receiver is "mp" or "ff".
run_test() {
        local fmt="$1"
        local tag="$2"
        local rx="$3"

        local suffix
        if [ "$rx" = "mp" ]; then suffix="mp"; else suffix="ff"; fi

        local sdp="$OUTDIR/${tag}.sdp"
        local png="$OUTDIR/${tag}_${suffix}.png"
        local tx_log="$OUTDIR/${tag}_${suffix}tx.log"
        local rx_log="$OUTDIR/${tag}_${suffix}rx.log"
        local rx_label
        if [ "$rx" = "mp" ]; then rx_label="mediaplay"; else rx_label="ffmpeg"; fi

        # Generate SDP
        if ! generate_sdp "$fmt" "$sdp"; then
                printf "  ${RED}FAIL${NC}  %-35s → %-10s  (SDP generation failed)\n" "$fmt" "$rx_label"
                FAIL=$((FAIL + 1))
                RESULTS+=("FAIL|$fmt|$rx_label|SDP generation failed")
                return
        fi

        # Start receiver in background
        if [ "$rx" = "mp" ]; then
                run_rx_mp "$sdp" "$png" "$rx_log" &
        else
                run_rx_ff "$sdp" "$png" "$rx_log" &
        fi
        local rx_pid=$!
        sleep 2

        # Start sender in background
        run_tx "$fmt" "$tx_log" &
        local tx_pid=$!

        # Wait for receiver (timeout 60s), then sender
        local rx_rc=0
        if ! wait_or_kill $rx_pid 60; then
                rx_rc=1
        fi
        wait_or_kill $tx_pid 60 || true

        # Check result
        if [ -f "$png" ] && [ "$rx_rc" -eq 0 ]; then
                local size
                size=$(stat -c%s "$png" 2>/dev/null || echo 0)
                if [ "$size" -gt 0 ]; then
                        printf "  ${GREEN}PASS${NC}  %-35s → %-10s  (%s bytes)\n" "$fmt" "$rx_label" "$size"
                        PASS=$((PASS + 1))
                        RESULTS+=("PASS|$fmt|$rx_label|$size bytes")
                        return
                fi
        fi

        # Failure
        printf "  ${RED}FAIL${NC}  %-35s → %-10s\n" "$fmt" "$rx_label"
        FAIL=$((FAIL + 1))
        RESULTS+=("FAIL|$fmt|$rx_label|see logs")

        if $VERBOSE; then
                echo "    --- RX log (last 5 lines) ---"
                tail -5 "$rx_log" 2>/dev/null | sed 's/^/    /'
                echo ""
        fi
}

# ── Filter helper ────────────────────────────────────────────────
# Returns 0 (true) if the tag matches any -f glob, or if no -f was given.
tag_matches() {
        local tag="$1"
        if [ ${#FILTERS[@]} -eq 0 ]; then return 0; fi
        for pat in "${FILTERS[@]}"; do
                # shellcheck disable=SC2254
                case "$tag" in $pat) return 0 ;; esac
        done
        return 1
}

# ── Main loop ────────────────────────────────────────────────────
for entry in "${FORMATS[@]}"; do
        IFS='|' read -r fmt tag receivers <<< "$entry"

        if ! tag_matches "$tag"; then continue; fi

        IFS=',' read -ra rx_list <<< "$receivers"
        for rx in "${rx_list[@]}"; do
                if [ "$rx" = "ff" ] && ! $HAS_FFMPEG; then
                        printf "  ${YELLOW}SKIP${NC}  %-35s → %-10s  (ffmpeg not found)\n" "$fmt" "ffmpeg"
                        SKIP=$((SKIP + 1))
                        RESULTS+=("SKIP|$fmt|ffmpeg|ffmpeg not found")
                        continue
                fi
                run_test "$fmt" "$tag" "$rx"
        done
done

# ── Summary ──────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}Summary${NC}"
echo "──────────────────────────────────────────────────────────────"
printf "  ${GREEN}PASS${NC}: %d   ${RED}FAIL${NC}: %d   ${YELLOW}SKIP${NC}: %d\n" $PASS $FAIL $SKIP
echo ""

# Write machine-readable summary
SUMMARY="$OUTDIR/summary.txt"
{
        printf "%-6s  %-35s  %-10s  %s\n" "STATUS" "FORMAT" "RECEIVER" "DETAIL"
        for r in "${RESULTS[@]}"; do
                IFS='|' read -r status fmt rx detail <<< "$r"
                printf "%-6s  %-35s  %-10s  %s\n" "$status" "$fmt" "$rx" "$detail"
        done
} > "$SUMMARY"
echo "Results written to $SUMMARY"
echo "Output images and logs in $OUTDIR/"

if [ $FAIL -gt 0 ]; then
        exit 1
fi
exit 0
