#!/usr/bin/env bash
# Compiler launcher that records per-TU peak RSS and wall time.
# Invoked transparently via CMAKE_CXX_COMPILER_LAUNCHER when the CMake
# probe for `/usr/bin/time -v` succeeds.  See PROMEKI_BUILD_STATS in the
# top-level CMakeLists.txt.
#
# Usage: build-stats-wrapper.sh <stats-log> <compiler> <compiler-args...>
#
# Each successful compile appends one tab-separated line to <stats-log>:
#   RSS_KB\tWALL_S\tUSER_S\tSYS_S\tTARGET_OBJ
#
# Appends are serialized with flock on <stats-log>.lock, so concurrent
# parallel builds don't interleave lines.

set -u

if [ $# -lt 2 ]; then
    echo "build-stats-wrapper: expected <log> <cmd> [args...]" >&2
    exit 2
fi

log="$1"; shift
time_bin="${PROMEKI_TIME_BIN:-/usr/bin/time}"

# Find the -o target (compilation output path) for logging.
target=""
prev=""
for a in "$@"; do
    if [ "$prev" = "-o" ]; then
        target="$a"
        break
    fi
    prev="$a"
done

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

"$time_bin" -f 'R=%M W=%e U=%U S=%S' -o "$tmp" -- "$@"
rc=$?

if [ $rc -eq 0 ] && [ -n "$target" ] && [ -s "$tmp" ]; then
    line=$(tail -n 1 "$tmp")
    rss=${line#*R=};  rss=${rss%% *}
    wall=${line#*W=}; wall=${wall%% *}
    usr=${line#*U=};  usr=${usr%% *}
    sys=${line#*S=};  sys=${sys%% *}
    (
        flock 9
        printf '%s\t%s\t%s\t%s\t%s\n' "$rss" "$wall" "$usr" "$sys" "$target" >> "$log"
    ) 9> "${log}.lock"
fi

exit $rc
