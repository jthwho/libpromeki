#!/usr/bin/env bash
#
# precommit.sh — Pre-commit verification gate for libpromeki.
#
# Performs the checks every commit should pass before going in:
#
#   1. clang-format conformance over the whole source tree
#      (`format-check-tree` target — fails on any diff).
#   2. A full configure-from-scratch in a fresh build directory,
#      with every auto-detectable feature module enabled and warnings
#      promoted to errors (-DPROMEKI_WARNINGS_AS_ERRORS=ON).
#   3. The full default build — must complete with zero warnings (the
#      configure step puts -Werror on every TU we own; vendored
#      thirdparty/ stays quiet).
#   4. The `tests` target build + the `check` target run, so every
#      unit-test exec is built and run (ctest --output-on-failure).
#   5. The `docs` target if Doxygen is on PATH, so doxygen issues
#      surface before commit.
#
# By default the build directory is `build-precommit-<timestamp>` at
# the repo root.  Pass --keep to keep it on success, or --build-dir
# DIR to override the location entirely.
#
# Concurrency: template-heavy C++ TUs in this codebase budget at
# roughly 1.5 GB of resident memory per job, so the safe ceiling is
# `available_memory_GB / 1.5` parallel jobs.  Two simultaneous
# precommit runs will overshoot that on most workstations.  This
# script holds a non-blocking flock on `${REPO_ROOT}/.precommit.lock`;
# a second precommit invocation while one is running will exit
# immediately with a clear error rather than queue up.
#
# Exits non-zero on first failure with a clear summary.  Run from
# anywhere inside the libpromeki working tree.

set -e
set -u
set -o pipefail

# Resolve repo root regardless of caller's cwd.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

KEEP_BUILD=0
BUILD_DIR=""
JOBS=""
SKIP_DOCS=0

usage() {
        cat <<EOF
Usage: scripts/precommit.sh [OPTIONS]

Options:
  --keep            Keep the build directory on success
                    (default: removed after a clean run)
  --build-dir DIR   Use DIR for the build (default: a fresh
                    \${REPO_ROOT}/build-precommit-<timestamp> dir)
  --jobs N          Parallel build jobs (default: nproc-derived,
                    capped by available RAM)
  --no-docs         Skip the Doxygen build step even when Doxygen
                    is available
  -h, --help        This message
EOF
}

while [ $# -gt 0 ]; do
        case "$1" in
                --keep)
                        KEEP_BUILD=1
                        shift
                        ;;
                --build-dir)
                        BUILD_DIR="$2"
                        shift 2
                        ;;
                --jobs)
                        JOBS="$2"
                        shift 2
                        ;;
                --no-docs)
                        SKIP_DOCS=1
                        shift
                        ;;
                -h|--help)
                        usage
                        exit 0
                        ;;
                *)
                        echo "precommit: unknown option: $1" >&2
                        usage >&2
                        exit 2
                        ;;
        esac
done

# Single-runner lock.  flock is released automatically when the
# script exits because the OS closes fd 9.  --no-fork (-n) is
# essential — we want a second invocation to fail immediately, not
# queue up behind the first (which can take 10+ minutes).
LOCK_FILE="${REPO_ROOT}/.precommit.lock"
exec 9>"${LOCK_FILE}"
if ! flock -n 9; then
        # Surface the holding PID + start time when the lockfile
        # carries them; otherwise just say someone else has it.
        held=$(cat "${LOCK_FILE}" 2>/dev/null || true)
        echo "precommit: another precommit is already running" >&2
        if [ -n "${held}" ]; then
                echo "precommit:   ${held}" >&2
        fi
        echo "precommit: refusing to start a second one — full builds saturate RAM" >&2
        exit 3
fi
# Stamp the lock file with our identity so the contention message
# above is informative.  This text is overwritten on each successful
# acquire and is only read by the failing branch.
echo "pid=$$ started=$(date '+%Y-%m-%d %H:%M:%S%z') host=$(hostname -s)" >&9

# Default build dir lives at the repo root so it sits beside other
# build trees (build/, build-precommit-*/).  .gitignore's `build-*/`
# rule keeps these out of git status.
if [ -z "${BUILD_DIR}" ]; then
        BUILD_DIR="${REPO_ROOT}/build-precommit-$(date +%Y%m%d-%H%M%S)"
fi

# Emit a final summary on every exit path so the user can see at a
# glance which step failed.  Trapped on EXIT (any path).
SUMMARY_STATUS="(no step run)"
SUMMARY_EXIT=1
cleanup() {
        local rc=$?
        echo
        echo "=============================================================="
        if [ ${SUMMARY_EXIT} -eq 0 ]; then
                echo "  precommit: PASS — ${SUMMARY_STATUS}"
        else
                echo "  precommit: FAIL — ${SUMMARY_STATUS}"
                if [ -d "${BUILD_DIR}" ]; then
                        echo "  build dir kept for inspection: ${BUILD_DIR}"
                fi
        fi
        echo "=============================================================="
        if [ ${SUMMARY_EXIT} -eq 0 ] && [ ${KEEP_BUILD} -eq 0 ]; then
                rm -rf "${BUILD_DIR}"
        fi
        exit ${rc}
}
trap cleanup EXIT

step() {
        local name="$1"
        SUMMARY_STATUS="step '${name}' did not complete"
        echo
        echo "=== precommit: ${name} ==="
}

if [ -z "${JOBS}" ]; then
        # Cap parallelism by available memory so template-heavy TUs
        # don't OOM the box.  Rule of thumb: max jobs = mem_avail /
        # 1.5 GB.  Falls back to nproc when /proc/meminfo can't be
        # read (non-Linux).
        per_job_mb=1500
        cpus=$(nproc --all)
        avail_kb=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo 2>/dev/null || echo "")
        if [ -n "${avail_kb}" ]; then
                mem_jobs=$(( avail_kb / (per_job_mb * 1024) ))
                [ "${mem_jobs}" -lt 1 ] && mem_jobs=1
                if [ "${mem_jobs}" -lt "${cpus}" ]; then
                        JOBS=${mem_jobs}
                else
                        JOBS=${cpus}
                fi
        else
                JOBS=${cpus}
        fi
fi

cd "${REPO_ROOT}"
echo "precommit: repo=${REPO_ROOT} build_dir=${BUILD_DIR} jobs=${JOBS}"

# 1) Configure (fresh build dir).  -Wno-dev silences CMake-internal
# deprecation chatter from vendored deps; we want this gate to
# enforce *our* warnings, not third-party CMake noise.
step "configure (fresh ${BUILD_DIR})"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=DevRelease \
        -DPROMEKI_WARNINGS_AS_ERRORS=ON \
        -Wno-dev

# 2) Format check — runs the same `format-check-tree` target the
# developer invokes manually, just inside the fresh build tree.
step "format-check (clang-format conformance)"
cmake --build "${BUILD_DIR}" --target format-check-tree

# 3) Full default build — `all` excludes test execs (they're
# EXCLUDE_FROM_ALL), so this is the library + demos + utils only.
step "build all (warnings-as-errors)"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

# 4) Build + run unit tests.  `check` depends on the test execs, so
# this also covers the test build with warnings-as-errors.
step "build + run unit tests (check target)"
cmake --build "${BUILD_DIR}" --target check -j "${JOBS}"

# 5) Doxygen — only when available.  The `docs` target is added by
# the top-level CMakeLists.txt only when find_package(Doxygen) wins;
# query the cache to decide whether to invoke it.
if [ ${SKIP_DOCS} -eq 0 ]; then
        if grep -qE '^DOXYGEN_EXECUTABLE:FILEPATH=.+' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
                step "doxygen build (docs target)"
                cmake --build "${BUILD_DIR}" --target docs
        else
                echo
                echo "=== precommit: doxygen build (skipped — Doxygen not found) ==="
        fi
fi

SUMMARY_STATUS="all checks passed"
SUMMARY_EXIT=0
