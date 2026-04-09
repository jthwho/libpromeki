#!/bin/bash
# rtp-rx-ffplay.sh — Receive an RTP stream produced by mediaplay (the
# MediaIOTask_Rtp backend) and play it through ffplay.
#
# Two input modes:
#
#   1. Pass an existing SDP file:
#        scripts/rtp-rx-ffplay.sh -s /tmp/stream.sdp
#
#   2. Pass stream parameters and let the script synthesize a
#      minimal SDP file on the fly (useful when testing MJPEG TX
#      against a known destination without running the sender with
#      RtpSaveSdpPath):
#        scripts/rtp-rx-ffplay.sh -a 239.0.0.1 -p 5004 \
#                                 -t video -m MJPEG -c 90000
#
# The script writes a temporary SDP file, runs ffplay with the
# protocol whitelist needed for RTP over UDP, and cleans up the
# temporary file on exit.  Use --keep to leave the temp SDP in
# place for inspection.
#
# ffplay must be installed and on PATH.

set -euo pipefail

SDP_FILE=""
ADDRESS=""
PORT=""
MEDIA="video"
PAYLOAD="MJPEG"
CLOCK_RATE="90000"
PAYLOAD_TYPE="26"
KEEP_TMP=false
EXTRA_ARGS=()

usage() {
        cat <<EOF
Usage:
  $0 -s <sdp-file> [-- <extra ffplay args>]
  $0 -a <address> -p <port> [-t video|audio] [-m MJPEG|raw|L16|L24]
      [-c <clock-rate>] [-P <payload-type>] [-- <extra ffplay args>]

Modes:
  -s FILE       Use an existing SDP file as the receive source.
  -a ADDR       Destination address (unicast or multicast group).
  -p PORT       Destination port.

Stream shape (only used in synthesized-SDP mode):
  -t KIND       video (default) or audio.
  -m FORMAT     MJPEG | raw | L16 | L24 (default: MJPEG).
  -c RATE       RTP clock rate in Hz (default 90000 for video).
  -P TYPE       RTP payload type number (default 26 for MJPEG).

Other:
  -k, --keep    Do not delete the temporary SDP file on exit.
  -h, --help    Show this help.

Anything after '--' is forwarded to ffplay.
EOF
}

# Parse args
while [[ $# -gt 0 ]]; do
        case "$1" in
                -s)  SDP_FILE="$2"; shift 2 ;;
                -a)  ADDRESS="$2"; shift 2 ;;
                -p)  PORT="$2"; shift 2 ;;
                -t)  MEDIA="$2"; shift 2 ;;
                -m)  PAYLOAD="$2"; shift 2 ;;
                -c)  CLOCK_RATE="$2"; shift 2 ;;
                -P)  PAYLOAD_TYPE="$2"; shift 2 ;;
                -k|--keep) KEEP_TMP=true; shift ;;
                -h|--help) usage; exit 0 ;;
                --)  shift; EXTRA_ARGS=("$@"); break ;;
                *)   echo "Unknown option: $1" >&2; usage; exit 1 ;;
        esac
done

if ! command -v ffplay >/dev/null 2>&1; then
        echo "error: ffplay is not installed or not on PATH" >&2
        exit 1
fi

TMP_SDP=""

cleanup() {
        if [[ -n "$TMP_SDP" && "$KEEP_TMP" == false ]]; then
                rm -f "$TMP_SDP"
        fi
}
trap cleanup EXIT

if [[ -n "$SDP_FILE" ]]; then
        if [[ ! -r "$SDP_FILE" ]]; then
                echo "error: SDP file not readable: $SDP_FILE" >&2
                exit 1
        fi
        SDP_PATH="$SDP_FILE"
else
        if [[ -z "$ADDRESS" || -z "$PORT" ]]; then
                echo "error: either -s or both -a and -p are required" >&2
                usage >&2
                exit 1
        fi

        # Default payload type per media/format.
        case "$MEDIA" in
                video)
                        case "$PAYLOAD" in
                                MJPEG) RTPMAP="JPEG/${CLOCK_RATE}" ;;
                                raw)   RTPMAP="raw/${CLOCK_RATE}" ;;
                                *)     echo "error: unknown video format '$PAYLOAD'" >&2; exit 1 ;;
                        esac
                        ;;
                audio)
                        # Default clock rate for audio unless overridden
                        if [[ "$CLOCK_RATE" == "90000" ]]; then CLOCK_RATE="48000"; fi
                        case "$PAYLOAD" in
                                L16) RTPMAP="L16/${CLOCK_RATE}/2" ;;
                                L24) RTPMAP="L24/${CLOCK_RATE}/2" ;;
                                *)   echo "error: unknown audio format '$PAYLOAD'" >&2; exit 1 ;;
                        esac
                        ;;
                *)
                        echo "error: media kind must be 'video' or 'audio'" >&2
                        exit 1
                        ;;
        esac

        TMP_SDP="$(mktemp /tmp/promeki-rtp-rx.XXXXXX.sdp)"
        {
                echo "v=0"
                echo "o=- 0 0 IN IP4 0.0.0.0"
                echo "s=promeki RTP receive"
                echo "c=IN IP4 ${ADDRESS}"
                echo "t=0 0"
                echo "m=${MEDIA} ${PORT} RTP/AVP ${PAYLOAD_TYPE}"
                echo "a=rtpmap:${PAYLOAD_TYPE} ${RTPMAP}"
        } > "$TMP_SDP"

        SDP_PATH="$TMP_SDP"
fi

echo "SDP session:"
sed 's/^/  /' "$SDP_PATH"

# Run ffplay.  The protocol whitelist is needed because RTP input
# uses SDP files which reference udp/rtp, both of which ffmpeg
# marks as unsafe by default.
exec ffplay \
        -protocol_whitelist "file,udp,rtp" \
        -fflags nobuffer \
        -flags low_delay \
        -i "$SDP_PATH" \
        "${EXTRA_ARGS[@]}"
