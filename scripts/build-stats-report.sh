#!/usr/bin/env bash
# Summarize per-TU build stats produced by build-stats-wrapper.sh.
# Shows the top-N slowest and most memory-hungry translation units.
#
# Usage: build-stats-report.sh [log-file] [N]
#   log-file defaults to ./build-stats.log (run from the build dir)
#   N defaults to 20

set -eu

log="${1:-build-stats.log}"
n="${2:-20}"

if [ ! -f "$log" ]; then
    echo "no stats log at $log" >&2
    echo "run a (clean) build with PROMEKI_BUILD_STATS=ON to populate it" >&2
    exit 1
fi

rows=$(wc -l < "$log")
echo "=== Build stats ($rows translation units, $log) ==="

echo
echo "-- Top $n by wall-clock (seconds) --"
sort -t$'\t' -k2 -g -r "$log" | head -n "$n" | awk -F'\t' '{
    printf "  %7.2fs  %8d KB  %s\n", $2, $1, $5
}'

echo
echo "-- Top $n by peak RSS (KB) --"
sort -t$'\t' -k1 -g -r "$log" | head -n "$n" | awk -F'\t' '{
    printf "  %8d KB  %7.2fs  %s\n", $1, $2, $5
}'

echo
awk -F'\t' '
    { total_wall += $2; total_user += $3; total_sys += $4
      if ($1 > max_rss) { max_rss = $1; max_tu = $5 } }
    END {
        printf "-- Totals (%d TUs) --\n", NR
        printf "  sum user+sys:   %8.1fs\n", total_user + total_sys
        printf "  sum wall:       %8.1fs (contended under -j)\n", total_wall
        printf "  peak single RSS: %7d KB  (%s)\n", max_rss, max_tu
    }' "$log"
