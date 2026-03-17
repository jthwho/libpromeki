#!/bin/bash
#
# vidgen-stream.sh — Launch vidgen and open the stream in a player.
#
# Usage:
#   ./tests/functional/vidgen-vlc.sh [mjpeg|st2110] [loopback|multicast] [capture]
#
# The optional "capture" argument records the stream to /tmp instead of
# live playback.  The captured file can be inspected in Audacity or with
# ffprobe after the run.
#
# Examples:
#   ./tests/functional/vidgen-vlc.sh                     # MJPEG loopback, live
#   ./tests/functional/vidgen-vlc.sh mjpeg loopback capture  # record to file
#   ./tests/functional/vidgen-vlc.sh st2110 multicast    # ST 2110 multicast, live
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
CAPTURE_MODE="${3:-}"

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

VIDGEN_PID=""
RECEIVER_PID=""
CAPTURE_FILE=""

cleanup() {
        echo ""
        echo "Cleaning up..."
        [[ -n "$VIDGEN_PID" ]] && kill "$VIDGEN_PID" 2>/dev/null && wait "$VIDGEN_PID" 2>/dev/null
        [[ -n "$RECEIVER_PID" ]] && kill "$RECEIVER_PID" 2>/dev/null && wait "$RECEIVER_PID" 2>/dev/null
        rm -f "$SDP"
        if [[ -n "$CAPTURE_FILE" && -f "$CAPTURE_FILE" ]]; then
                echo ""
                echo "Capture saved to: $CAPTURE_FILE"
                ffprobe -hide_banner "$CAPTURE_FILE" 2>&1 || true
                echo ""
                echo "  Play:    ffplay $CAPTURE_FILE"
                echo "  Inspect: audacity $CAPTURE_FILE"
        fi
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

if [[ "$CAPTURE_MODE" == "capture" ]]; then
        # Record mode: save audio+video to file for offline inspection
        CAPTURE_FILE="/tmp/vidgen-capture-$$.mkv"
        echo "Recording stream to $CAPTURE_FILE (Ctrl+C to stop)..."
        ffmpeg -y -protocol_whitelist rtp,udp,file -i "$SDP" \
                -c:v copy -c:a pcm_s24le \
                "$CAPTURE_FILE" &
        RECEIVER_PID=$!
else
        # Live playback mode
        if command -v ffplay &>/dev/null; then
                echo "Opening in ffplay..."
                # Use Wayland SDL backend when available to avoid GLX
                # context creation failures through Xwayland.
                SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-${WAYLAND_DISPLAY:+wayland}}" \
                ffplay -protocol_whitelist rtp,udp,file -i "$SDP" \
                        -window_title "vidgen ($MODE)" &
                RECEIVER_PID=$!
        elif command -v vlc &>/dev/null; then
                echo "Trying VLC (may not work without live555 support)..."
                vlc "$SDP" 2>/dev/null &
                RECEIVER_PID=$!
        else
                echo "No player found. Install ffmpeg (for ffplay) or VLC with live555."
                echo "SDP file: $SDP"
        fi
fi

echo "Press Ctrl+C to stop."
wait "$VIDGEN_PID"
