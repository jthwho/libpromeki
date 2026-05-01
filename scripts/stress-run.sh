#!/usr/bin/env bash
#
# stress-run.sh — Run an application under stress, capture every run's
#                 output, and gather diagnostic artifacts on any
#                 non-zero exit (crash or timeout).
#
# Usage:
#   scripts/stress-run.sh [-n COUNT] [-t SECONDS] CAPTURE_DIR APP [APP_ARGS...]
#
# Options:
#   -n N    Run count.  Omit (or pass 0) to run forever until Ctrl-C.
#   -t S    Per-run timeout in seconds (default 60 — double the
#           current unit-test runtime).  Treated as a failure.
#   -h      Show this usage banner.
#
# Output layout (per run N, app pid P):
#   stress.log                  Master log; one line per run with
#                               timestamp, pid, rc, duration, status:
#                                 OK      — exited zero
#                                 FAIL    — non-zero exit (eg. test
#                                           assertion fired) without
#                                           a fatal signal
#                                 CRASH   — killed by a fatal signal
#                                           (rc=128+signo); the master
#                                           log records the signal name
#                                 TIMEOUT — exceeded -t, killed by the
#                                           timeout(1) wrapper
#                                 STOPPED — Ctrl-C while in flight
#   run-N-pidP.stdout           stdout of every run (always kept).
#   run-N-pidP.stderr           stderr of every run (always kept).
#   crash-N-pidP.bt             gdb 'thread apply all bt' on the core,
#                               when one was located.
#   crash-N-pidP.core           Copy of the core itself (so apport /
#                               systemd-coredump rotation can't sweep
#                               it out from under us).
#   crash-N-pidP.applog         Promeki in-process crash-handler log
#                               (when present in the run's stderr).
#
# SIGINT / SIGTERM stop the loop after the in-flight run is captured —
# you don't lose the artifact bundle from the run that was on the wire
# when you Ctrl-C'd.
#

set -u

usage() {
        # Print every leading-# line at the top of the script, stopping
        # at the first non-comment line.  Strips the leading "# " so
        # the user sees the same banner that lives at the top of the
        # source file.
        awk '
                NR == 1 && /^#!/ { next }
                /^#/ { sub(/^# ?/, ""); print; next }
                { exit }
        ' "$0"
        exit 64
}

timeout_s=60
count=0
while getopts "n:t:h" opt; do
        case "$opt" in
                n) count="$OPTARG";;
                t) timeout_s="$OPTARG";;
                h|*) usage;;
        esac
done
shift $((OPTIND-1))

[ $# -lt 2 ] && usage

capture_dir="$1"
shift
app="$1"
shift
# Remaining "$@" are app args.

if [ ! -x "$app" ]; then
        echo "Error: '$app' is not executable" >&2
        exit 1
fi
app=$(readlink -f "$app")
app_basename=$(basename "$app")
# Linux truncates /proc/<pid>/comm to TASK_COMM_LEN (16 incl. NUL = 15
# chars).  pgrep -x matches against comm, so a longer basename never
# equals the truncated comm and the lookup silently fails — leaving
# the timeout(1) wrapper's pid in app_pid.  Use the truncated form.
app_comm="${app_basename:0:15}"

mkdir -p "$capture_dir" || exit 1
capture_dir=$(readlink -f "$capture_dir")
master_log="$capture_dir/stress.log"

# Make sure the kernel will write a core file we can read.
ulimit -c unlimited 2>/dev/null
core_pattern=$(cat /proc/sys/kernel/core_pattern 2>/dev/null || echo "")

stop=0
on_signal() {
        # Only flip the flag — don't touch anything else.  The current
        # iteration finishes its capture step (rename outputs, gdb
        # extract, etc.) and the loop top observes stop=1 next time
        # around.  Killing the child manually here would race the
        # capture; SIGINT propagates to the child via timeout(1)
        # --foreground anyway.
        stop=1
        printf '\n[stress-run] signal received; finishing in-flight run before exit\n' >&2
}
trap on_signal INT TERM

# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------

# Locate a core file produced by the given pid.  Echoes a path on
# success or an empty string when nothing useful was found.  Tries
# Ubuntu/apport, plain core in cwd / capture dir, and finally
# coredumpctl when systemd-coredump is in use.
# Returns the canonical name of a fatal signal given its number, or an
# empty string when @p signo isn't a recognised signal.  Used to split
# CRASH (process was killed by the kernel / a sibling) from FAIL
# (process exited non-zero on its own).  @c kill -l would do this too
# but isn't reliable for portable signal numbering, so we hardcode the
# Linux/glibc subset that any of our crash modes can produce.
signal_name() {
        case "$1" in
                1)  echo SIGHUP;;
                2)  echo SIGINT;;
                3)  echo SIGQUIT;;
                4)  echo SIGILL;;
                5)  echo SIGTRAP;;
                6)  echo SIGABRT;;
                7)  echo SIGBUS;;
                8)  echo SIGFPE;;
                9)  echo SIGKILL;;
                11) echo SIGSEGV;;
                13) echo SIGPIPE;;
                14) echo SIGALRM;;
                15) echo SIGTERM;;
                24) echo SIGXCPU;;
                25) echo SIGXFSZ;;
                31) echo SIGSYS;;
                *)  echo "";;
        esac
}

# Classifies a child exit code into one of OK / TIMEOUT / CRASH / FAIL
# and (for CRASH) returns the signal name in the second word.
#
# Convention bash and most shells follow: a child killed by signal N
# returns 128+N from wait(2).  An app could legitimately return 139
# from its own main(), but the heuristic is the only signal we get
# from the wait status, so we lean on it.
#
# The duration_ms / timeout_s pair lets us catch timeout(1)'s exit-
# code variants — depending on whether we're in --foreground mode
# and whether the child was a shell wrapper, GNU timeout(1) returns
# 124, 125, 137, or 143 for a hit timeout.  Anything that exited
# right at the timeout boundary is a TIMEOUT regardless of rc.
classify_exit() {
        local rc="$1"
        local duration_ms="$2"
        local timeout_ms="$3"
        if [ "$rc" -eq 0 ]; then
                echo "OK"
                return
        fi
        # Duration heuristic: a run that died within 200ms of the
        # configured per-run timeout is almost certainly the timeout
        # firing, regardless of the exit code timeout(1) reported.
        if [ "$timeout_ms" -gt 0 ] && [ "$duration_ms" -ge $((timeout_ms - 200)) ]; then
                echo "TIMEOUT"
                return
        fi
        if [ "$rc" -gt 128 ] && [ "$rc" -lt 256 ]; then
                local sig=$((rc - 128))
                local name
                name=$(signal_name "$sig")
                if [ -n "$name" ]; then
                        echo "CRASH $name"
                else
                        echo "CRASH SIG$sig"
                fi
                return
        fi
        echo "FAIL"
}

find_core_for_pid() {
        local target_pid="$1"
        local c
        # Apport: /var/lib/apport/coredump/core.<binary>.<uid>.<bootid>.<pid>.<timestamp>
        # All `*` chars must stay unquoted for the glob to expand.  When
        # nothing matches, the unmatched pattern survives and the
        # `[ -f ]` test rejects it on the next line.
        for c in /var/lib/apport/coredump/core.*.${target_pid}.* \
                 /var/lib/apport/coredump/core.*.${target_pid}; do
                [ -f "$c" ] && [ -r "$c" ] && { echo "$c"; return; }
        done
        # Plain core in cwd / capture dir.
        for c in "core" "core.${target_pid}" "$capture_dir/core" "$capture_dir/core.${target_pid}"; do
                [ -f "$c" ] && [ -r "$c" ] && { echo "$c"; return; }
        done
        # systemd-coredump fallback.
        if command -v coredumpctl >/dev/null 2>&1; then
                local tmp_core="$capture_dir/.coredumpctl.$$.$target_pid.tmp"
                if coredumpctl -1 dump "$target_pid" --output="$tmp_core" >/dev/null 2>&1; then
                        if [ -s "$tmp_core" ]; then
                                echo "$tmp_core"
                                return
                        fi
                        rm -f "$tmp_core"
                fi
        fi
        echo ""
}

# Locate the in-process promeki crash-handler log for the given pid.
# The crash handler builds the path from Dir::temp() (typically /tmp
# but can be overridden via LibraryOptions::CrashLogDir) and emits the
# path to stderr just before exit.  Prefer to grep that path out of
# the run's captured stderr; fall back to a glob across well-known
# scratch dirs.
find_promeki_crash_log() {
        local target_pid="$1"
        local err_log="$2"
        local found=""
        if [ -n "$err_log" ] && [ -f "$err_log" ]; then
                found=$(grep -oE '/[^[:space:]]*promeki-crash-[^[:space:]]*\.log' "$err_log" 2>/dev/null | head -1)
        fi
        if [ -n "$found" ] && [ -f "$found" ]; then
                echo "$found"
                return
        fi
        local f
        # Same unquoted-glob convention as find_core_for_pid.  The
        # candidate scratch dirs come from the env vars promeki itself
        # honours — PROMEKI_OPT_CrashLogDir takes precedence, then
        # PROMEKI_OPT_TempDir, then the system default /tmp.  Always
        # also check the capture dir on the off chance the user pinned
        # the crash log there.
        local d crashdirs
        crashdirs=()
        [ -n "${PROMEKI_OPT_CrashLogDir:-}" ] && crashdirs+=("$PROMEKI_OPT_CrashLogDir")
        [ -n "${PROMEKI_OPT_TempDir:-}" ] && crashdirs+=("$PROMEKI_OPT_TempDir")
        crashdirs+=("/tmp" "$capture_dir")
        for d in "${crashdirs[@]}"; do
                for f in "$d"/promeki-crash-*-${target_pid}.log; do
                        [ -f "$f" ] && [ -r "$f" ] && { echo "$f"; return; }
                done
        done
        echo ""
}

capture_crash_artifacts() {
        local run_num="$1"
        local app_pid="$2"
        local err_log="$3"
        local ts="$4"
        local status="$5"
        local rc="$6"

        # Give the kernel / apport / systemd-coredump a moment to flush
        # the core file before we go looking for it.  Half a second is
        # plenty for apport's hand-off; longer values just slow the
        # stress loop down.
        sleep 0.5

        local core
        core=$(find_core_for_pid "$app_pid")
        if [ -n "$core" ]; then
                local core_dest="$capture_dir/crash-${run_num}-pid${app_pid}.core"
                if cp -f "$core" "$core_dest" 2>/dev/null; then
                        echo "[stress-run]   core:    $core_dest" >&2
                        if command -v gdb >/dev/null 2>&1; then
                                local bt_dest="$capture_dir/crash-${run_num}-pid${app_pid}.bt"
                                {
                                        echo "=== App:    $app"
                                        echo "=== Core:   $core"
                                        echo "=== Time:   $ts"
                                        echo "=== Status: $status (rc=$rc)"
                                        echo
                                        gdb --batch -q -nx \
                                                -ex "set pagination off" \
                                                -ex "thread apply all bt" \
                                                -ex "info registers" \
                                                "$app" "$core" 2>&1
                                } > "$bt_dest"
                                echo "[stress-run]   bt:      $bt_dest" >&2
                        fi
                else
                        echo "[stress-run]   WARN: could not copy core ($core)" >&2
                fi
        else
                echo "[stress-run]   no core located for pid=$app_pid (pattern: $core_pattern)" >&2
        fi

        local applog
        applog=$(find_promeki_crash_log "$app_pid" "$err_log")
        if [ -n "$applog" ]; then
                local applog_dest="$capture_dir/crash-${run_num}-pid${app_pid}.applog"
                if cp -f "$applog" "$applog_dest" 2>/dev/null; then
                        echo "[stress-run]   applog:  $applog_dest" >&2
                fi
        fi
}

# ----------------------------------------------------------------------
# Main loop
# ----------------------------------------------------------------------

run_num=0
ok_count=0
crash_count=0
fail_count=0
timeout_count=0
start_time=$(date +%s)

count_label="$count"
[ "$count" -eq 0 ] && count_label="forever"

echo "[stress-run] $(date -Iseconds) starting" | tee -a "$master_log"
echo "[stress-run]   app=$app args=$*" | tee -a "$master_log"
echo "[stress-run]   capture=$capture_dir count=$count_label timeout=${timeout_s}s" | tee -a "$master_log"
echo "[stress-run]   core_pattern=$core_pattern" | tee -a "$master_log"

while :; do
        [ "$stop" -eq 1 ] && break
        if [ "$count" -ne 0 ] && [ "$run_num" -ge "$count" ]; then break; fi

        run_num=$((run_num + 1))
        ts=$(date -Iseconds)

        # Stage stdout / stderr in the capture dir so even an
        # un-PID'd run's output ends up archived if something goes
        # sideways before we get to the rename step.  After the run
        # we mv these to the final per-pid names.
        stdout_tmp="$capture_dir/.run-${run_num}.stdout.tmp"
        stderr_tmp="$capture_dir/.run-${run_num}.stderr.tmp"

        run_start_ms=$(date +%s%N)
        # timeout(1) --foreground: keep the wrapper in the foreground
        # so SIGINT delivered to this script reaches the app too.
        # --kill-after grace: SIGKILL if SIGTERM doesn't take.
        timeout --foreground --kill-after=5 "$timeout_s" "$app" "$@" \
                >"$stdout_tmp" 2>"$stderr_tmp" &
        wait_pid=$!

        # Best-effort: snapshot the *real* app pid (timeout(1) is the
        # immediate child; the app is its child).  We need the app pid
        # to match against /proc and apport file names.  pgrep is the
        # most portable way to walk one level deeper.
        app_pid=""
        if command -v pgrep >/dev/null 2>&1; then
                for _ in 1 2 3 4 5; do
                        app_pid=$(pgrep -P "$wait_pid" -x "$app_comm" 2>/dev/null | head -1)
                        [ -n "$app_pid" ] && break
                        sleep 0.05
                done
        fi
        [ -z "$app_pid" ] && app_pid="$wait_pid"

        wait "$wait_pid"
        rc=$?
        run_end_ms=$(date +%s%N)
        duration_ms=$(((run_end_ms - run_start_ms) / 1000000))

        out_log="$capture_dir/run-${run_num}-pid${app_pid}.stdout"
        err_log="$capture_dir/run-${run_num}-pid${app_pid}.stderr"
        mv -f "$stdout_tmp" "$out_log" 2>/dev/null
        mv -f "$stderr_tmp" "$err_log" 2>/dev/null

        if [ "$stop" -eq 1 ]; then
                # User-initiated stop while a run was in flight.  Keep
                # the captured output (might still be useful) but mark
                # this run STOPPED rather than CRASH so the master log
                # doesn't lie about what happened.
                echo "[$ts] run=$run_num pid=$app_pid rc=$rc duration=${duration_ms}ms STOPPED" >> "$master_log"
                echo "[stress-run] run $run_num pid=$app_pid: STOPPED (rc=$rc)" >&2
                break
        fi

        # classify_exit returns "OK" / "TIMEOUT" / "FAIL" / "CRASH <SIGNAME>"
        verdict=$(classify_exit "$rc" "$duration_ms" "$((timeout_s * 1000))")
        status="${verdict%% *}"
        case "$status" in
                OK)      ok_count=$((ok_count + 1));;
                TIMEOUT) timeout_count=$((timeout_count + 1));;
                CRASH)   crash_count=$((crash_count + 1));;
                FAIL)    fail_count=$((fail_count + 1));;
        esac

        echo "[$ts] run=$run_num pid=$app_pid rc=$rc duration=${duration_ms}ms $verdict" >> "$master_log"

        if [ "$status" != "OK" ]; then
                echo "[stress-run] run $run_num pid=$app_pid: $verdict (rc=$rc, duration=${duration_ms}ms)" >&2
                # Only CRASH and TIMEOUT can produce a core file; FAIL is
                # a clean non-zero exit (assertion fired, argv error,
                # etc.) so we skip the gdb / core-extraction step there
                # — the captured stdout/stderr is the diagnostic
                # everyone wants.
                if [ "$status" = "CRASH" ] || [ "$status" = "TIMEOUT" ]; then
                        capture_crash_artifacts "$run_num" "$app_pid" "$err_log" "$ts" "$verdict" "$rc"
                fi
        fi
done

elapsed=$(($(date +%s) - start_time))
summary="runs=$run_num ok=$ok_count fail=$fail_count crash=$crash_count timeout=$timeout_count elapsed=${elapsed}s"
echo "[stress-run] $(date -Iseconds) stopped: $summary" | tee -a "$master_log"
echo "[stress-run] capture folder: $capture_dir"
