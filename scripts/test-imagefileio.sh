#!/bin/bash
# test-imagefileio.sh — Generate image files in all supported formats and
# validate them with ImageMagick/GraphicsMagick identify.
#
# Usage: scripts/test-imagefileio.sh [-d]
#
#   -d    Display each image after validation (uses 'display' command).
#
# Files are written to /tmp/promeki_imgtest/ and cleaned up on success.

set -euo pipefail

DISPLAY_IMAGES=false

while getopts "d" opt; do
        case $opt in
                d) DISPLAY_IMAGES=true ;;
                *) echo "Usage: $0 [-d]"; exit 1 ;;
        esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
TOOL="$BUILD_DIR/bin/imgtest"
OUTDIR="/tmp/promeki_imgtest"
PASS=0
FAIL=0
SKIP=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

cleanup() {
        rm -rf "$OUTDIR"
}

# Check that the tool exists
if [ ! -x "$TOOL" ]; then
        echo "imgtest not found at $TOOL — run 'build' first."
        exit 1
fi

# Check that identify is available
if ! command -v identify &>/dev/null; then
        echo "identify (ImageMagick/GraphicsMagick) not found in PATH."
        exit 1
fi

if $DISPLAY_IMAGES && ! command -v display &>/dev/null; then
        echo "display (ImageMagick/GraphicsMagick) not found in PATH."
        exit 1
fi

rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"

echo "=== Generating test images ==="
"$TOOL" "$OUTDIR"
echo ""

# Verify each file with identify
echo "=== Validating with identify ==="
for f in "$OUTDIR"/*; do
        fname="$(basename "$f")"

        # Run identify
        output=$(identify "$f" 2>&1) && rc=0 || rc=$?

        if [ $rc -eq 0 ]; then
                printf "${GREEN}PASS${NC} %-35s %s\n" "$fname" "$output"
                PASS=$((PASS + 1))
                if $DISPLAY_IMAGES; then
                        # +matte strips alpha (DPX uses inverted alpha convention)
                        display +matte "$f" &
                        DISPLAY_PID=$!
                        echo "     Displaying $fname (press any key to continue, or close the window)"
                        read -r -n1 -s
                        kill $DISPLAY_PID 2>/dev/null || true
                        wait $DISPLAY_PID 2>/dev/null || true
                fi
        else
                # Some formats may not be supported by this build of identify
                if echo "$output" | grep -qi "unable to read\|not supported\|unrecognized\|no decode delegate"; then
                        printf "${YELLOW}SKIP${NC} %-35s (not supported by identify)\n" "$fname"
                        SKIP=$((SKIP + 1))
                else
                        printf "${RED}FAIL${NC} %-35s %s\n" "$fname" "$output"
                        FAIL=$((FAIL + 1))
                fi
        fi
done

echo ""
echo "=== Results: ${PASS} passed, ${FAIL} failed, ${SKIP} skipped ==="

if [ $FAIL -gt 0 ]; then
        echo "Output files preserved in $OUTDIR for inspection."
        exit 1
fi

cleanup
echo "All files validated. Temp files cleaned up."
exit 0
