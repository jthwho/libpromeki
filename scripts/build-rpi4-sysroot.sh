#!/usr/bin/env bash
#
# build-rpi4-sysroot.sh — build a Raspberry Pi 4 (aarch64) cross-compile
# sysroot from a Debian Trixie arm64 rootfs, using Docker + qemu.
#
# Raspberry Pi OS (64-bit, Trixie-based) is Debian Trixie (glibc 2.41,
# GCC 14), so a Debian Trixie arm64 rootfs is binary-compatible with a
# stock Pi 4 for the standard system libraries libpromeki links against
# (glibc, libstdc++, ALSA) and the kernel uapi headers it needs (V4L2,
# dma-heap/dma-buf).  Trixie (vs the older Bookworm) is required because
# libpromeki uses C++20 std::format, whose libstdc++ support landed in
# GCC 13 — Bookworm's GCC 12 libstdc++ lacks it.  Almost everything else
# (codecs, TLS, SRT, JSON, fonts) is vendored under thirdparty/ and built
# from source, so the sysroot package set is small.
#
# Pair this sysroot with a GCC-14 cross compiler (matching Trixie's
# libstdc++ symbol versions) — see cmake/configs/cross-rpi4.cmake, which
# pins PROMEKI_CROSS_TOOLCHAIN_SUFFIX=-14.
#
# This produces a sysroot suitable for the `cross-rpi4` CMake preset:
#
#     scripts/build-rpi4-sysroot.sh
#     cmake -B build-rpi4 -DPROMEKI_CONFIG_FILE=cross-rpi4 \
#           -DPROMEKI_SYSROOT=/mnt/data/sysroots/rpi4 \
#           -DPROMEKI_STAGING_PREFIX=/mnt/data/sysroots/rpi4
#
# Prerequisites (one-time):
#   - Docker, with the running user in the `docker` group.
#   - qemu arm64 binfmt registered on the host kernel:
#         docker run --privileged --rm tonistiigi/binfmt --install arm64
#     (The script registers it automatically if the handler is missing.)
#
# It is safe to re-run: the target directory is rebuilt from scratch.
#
# Tunables (environment variables):
#   SYSROOT       Destination directory  (default /mnt/data/sysroots/rpi4)
#   SUITE         Debian suite           (default trixie — Pi OS 64-bit)
#   ARCH          dpkg architecture      (default arm64)
#   IMAGE         Base Docker image      (default debian:${SUITE})
#   LIBSTDCXX_DEV libstdc++ -dev package (default libstdc++-14-dev, the
#                 GCC major that matches SUITE; set to libstdc++-12-dev
#                 for bookworm, etc.)
#   EXTRA_PKGS    Extra apt packages to add to the default set (space-sep)

set -euo pipefail

SYSROOT="${SYSROOT:-/mnt/data/sysroots/rpi4}"
SUITE="${SUITE:-trixie}"
ARCH="${ARCH:-arm64}"
IMAGE="${IMAGE:-debian:${SUITE}}"
LIBSTDCXX_DEV="${LIBSTDCXX_DEV:-libstdc++-14-dev}"

# Default package set — the system libraries + uapi headers the
# cross-rpi4 preset actually needs.  Everything else is vendored.
#   libc6-dev          glibc headers, libs, crt objects (the link floor)
#   ${LIBSTDCXX_DEV}   Trixie's libstdc++ (GCC 14) — provides libstdc++.so
#                      and the matching headers; the cross compiler is
#                      pinned to GCC 14 so the produced binaries top out at
#                      GLIBCXX_3.4.33, which a stock Trixie Pi satisfies.
#   linux-libc-dev     kernel uapi: linux/videodev2.h, linux/dma-heap.h,
#                      linux/dma-buf.h  (V4L2 + DMABUF backends)
#   libasound2-dev     ALSA — V4L2 audio capture path
#   pkg-config         (-dev packages drop .pc files; harmless to include)
DEFAULT_PKGS="libc6-dev ${LIBSTDCXX_DEV} linux-libc-dev libasound2-dev pkg-config"
PKGS="${DEFAULT_PKGS} ${EXTRA_PKGS:-}"

CONTAINER="promeki-rpi4-sysroot-build"

log() { printf '\033[1;36m[rpi4-sysroot]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[rpi4-sysroot] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

command -v docker >/dev/null || die "docker not found in PATH"
docker version >/dev/null 2>&1 || die "cannot talk to the docker daemon (in the 'docker' group?)"

# ---------------------------------------------------------------------------
# Ensure qemu arm64 binfmt is registered (needed to run foreign-arch
# containers).  Registering is a privileged, host-global, until-reboot
# operation; only do it when the handler is actually missing.
# ---------------------------------------------------------------------------
if [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
    log "registering qemu arm64 binfmt handler (one-time, host-global)…"
    docker run --privileged --rm tonistiigi/binfmt --install "${ARCH}" >/dev/null
fi

# ---------------------------------------------------------------------------
# Build the rootfs inside a throwaway arm64 container, then export it.
# ---------------------------------------------------------------------------
docker rm -f "${CONTAINER}" >/dev/null 2>&1 || true

log "installing packages into ${SUITE}/${ARCH}: ${PKGS}"
docker run --name "${CONTAINER}" --platform "linux/${ARCH}" "${IMAGE}" bash -c "
    set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y --no-install-recommends ${PKGS}
    apt-get clean
    rm -rf /var/lib/apt/lists/*
"

log "exporting rootfs to ${SYSROOT}"
rm -rf "${SYSROOT}"
mkdir -p "${SYSROOT}"
docker export "${CONTAINER}" | tar -C "${SYSROOT}" -xf -
docker rm -f "${CONTAINER}" >/dev/null

# ---------------------------------------------------------------------------
# Relativize absolute symlinks.  A `docker export` rootfs keeps symlinks
# exactly as the container made them — many are absolute (e.g.
# /usr/lib/aarch64-linux-gnu/libasound.so -> /lib/aarch64-linux-gnu/libasound.so.2).
# In a cross sysroot an absolute symlink target escapes the sysroot and
# resolves against the HOST filesystem, so -lasound / -lc etc. silently
# break.  Rewrite every absolute symlink to a sysroot-relative one.
# ---------------------------------------------------------------------------
log "relativizing absolute symlinks under the sysroot"
python3 - "${SYSROOT}" <<'PY'
import os, sys
root = os.path.abspath(sys.argv[1])
fixed = 0
for dirpath, dirnames, filenames in os.walk(root):
    for name in dirnames + filenames:
        p = os.path.join(dirpath, name)
        if not os.path.islink(p):
            continue
        target = os.readlink(p)
        if not target.startswith('/'):
            continue                      # already relative — leave it
        # Resolve the absolute target *as if rooted at the sysroot*, then
        # express it relative to the symlink's own directory.
        in_sysroot = os.path.join(root, target.lstrip('/'))
        rel = os.path.relpath(in_sysroot, os.path.dirname(p))
        os.remove(p)
        os.symlink(rel, p)
        fixed += 1
print(f"  rewrote {fixed} absolute symlink(s)")
PY

# ---------------------------------------------------------------------------
# Sanity check — verify the files the cross-rpi4 build relies on are present.
# ---------------------------------------------------------------------------
log "verifying sysroot contents"
miss=0
check() { if [ -e "${SYSROOT}/$1" ]; then echo "  ok   $1"; else echo "  MISS $1"; miss=$((miss+1)); fi; }
check usr/include/linux/videodev2.h
check usr/include/linux/dma-heap.h
check usr/include/linux/dma-buf.h
check usr/include/alsa/asoundlib.h
check usr/include/aarch64-linux-gnu/sys/cdefs.h
check usr/lib/aarch64-linux-gnu/libc.so.6
check usr/lib/aarch64-linux-gnu/libasound.so
check usr/lib/aarch64-linux-gnu/pkgconfig/alsa.pc

du -sh "${SYSROOT}" 2>/dev/null | awk '{print "  size " $1}'
[ "${miss}" -eq 0 ] && log "sysroot ready at ${SYSROOT}" || die "${miss} expected file(s) missing — see MISS lines above"
