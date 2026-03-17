#!/bin/bash
#
# vidgen-stream.sh — Launch vidgen and open the stream in a player.
#
# Tries ffplay first (most reliable for RTP/SDP), then VLC.
# Note: Ubuntu's VLC package is built without live555, so it cannot
# open SDP files or receive RTP streams. Use ffplay or build VLC
# from source with --enable-live555.
#
# Usage:
#   ./tests/functional/vidgen-vlc.sh [mjpeg|st2110] [multicast]
#
# Examples:
#   ./tests/functional/vidgen-vlc.sh              # MJPEG loopback
#   ./tests/functional/vidgen-vlc.sh st2110        # ST 2110 loopback
#   ./tests/functional/vidgen-vlc.sh mjpeg multicast  # MJPEG multicast
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VIDGEN="$REPO_ROOT/build/bin/vidgen"
SDP="/tmp/vidgen-test-$$.sdp"

if [[ ! -x "$VIDGEN" ]]; then
        echo "Error: vidgen not found at $VIDGEN"
        echo "Build it first: build vidgen"
        exit 1
fi

MODE="${1:-mjpeg}"
NETWORK="${2:-loopback}"

COMMON_ARGS=(
        --framerate 30
        --width 640 --height 480
        --pattern colorbars
        --tc-burn
        --sdp "$SDP"
        --verbose
)

case "$MODE" in
        mjpeg)   COMMON_ARGS+=(--transport mjpeg) ;;
        st2110)  COMMON_ARGS+=(--transport st2110) ;;
        *)
                echo "Unknown mode: $MODE (use mjpeg or st2110)"
                exit 1
                ;;
esac

case "$NETWORK" in
        loopback)
                COMMON_ARGS+=(--dest 127.0.0.1:5004)
                ;;
        multicast)
                COMMON_ARGS+=(--multicast 239.0.0.1:5004)
                ;;
        *)
                echo "Unknown network: $NETWORK (use loopback or multicast)"
                exit 1
                ;;
esac

cleanup() {
        echo ""
        echo "Cleaning up..."
        [[ -n "${VIDGEN_PID:-}" ]] && kill "$VIDGEN_PID" 2>/dev/null && wait "$VIDGEN_PID" 2>/dev/null
        [[ -n "${PLAYER_PID:-}" ]] && kill "$PLAYER_PID" 2>/dev/null && wait "$PLAYER_PID" 2>/dev/null
        rm -f "$SDP"
}
trap cleanup EXIT

echo "Starting vidgen ($MODE, $NETWORK)..."
echo "  $VIDGEN ${COMMON_ARGS[*]}"
echo ""

"$VIDGEN" "${COMMON_ARGS[@]}" &
VIDGEN_PID=$!

# Wait for SDP file to appear
for i in $(seq 1 20); do
        [[ -f "$SDP" ]] && break
        sleep 0.1
done

if [[ ! -f "$SDP" ]]; then
        echo "Error: SDP file not created after 2 seconds"
        exit 1
fi

echo ""
echo "=== SDP ==="
cat "$SDP"
echo "==========="
echo ""

# ffplay is the most reliable receiver for RTP/SDP.
# Ubuntu VLC is built without live555 and cannot handle SDP/RTP.
PLAYER_PID=""
if command -v ffplay &>/dev/null; then
        echo "Opening in ffplay..."
        ffplay -protocol_whitelist rtp,udp,file -i "$SDP" -window_title "vidgen ($MODE)" 2>/dev/null &
        PLAYER_PID=$!
elif command -v vlc &>/dev/null; then
        echo "Trying VLC (may not work without live555 support)..."
        vlc "$SDP" 2>/dev/null &
        PLAYER_PID=$!
else
        echo "No player found. Install ffmpeg (for ffplay) or VLC with live555."
        echo "SDP file: $SDP"
fi

echo "Press Ctrl+C to stop."
wait "$VIDGEN_PID"
