# Cross-compile preset: Raspberry Pi 4 (Broadcom BCM2711, 64-bit).
#
# A thin specialisation of the generic `cross-aarch64-linux` preset that
# pins the Pi 4's SoC so the codec / DSP paths get -mcpu tuning, and
# flips on the bits that are actually present on a Pi 4 (its V4L2 stateful
# H.264 / HEVC codec).  Everything else — the Debian aarch64 toolchain,
# the host-only feature lockout (CUDA / NVENC / NDI / JPEG XS), the
# sysroot / staging plumbing — is inherited unchanged from the base.
#
#   cmake -B build-rpi4 -DPROMEKI_CONFIG_FILE=cross-rpi4
#
# The Pi 4 is aarch64 (ARM Cortex-A72).  This targets a 64-bit OS
# (Raspberry Pi OS 64-bit / Ubuntu arm64) — the modern default.  The
# legacy 32-bit (armhf) userland is a different triplet and is NOT
# covered here.
#
# SoC facts that drive the settings below:
#   - CPU:   quad Cortex-A72 @ 1.5/1.8 GHz, ARMv8-A.
#   - NEON:  always present on ARMv8-A (no runtime probe needed).
#   - CRC32: present (Cortex-A72 implements the CRC extension).
#   - Crypto (AES/SHA): NOT fitted on the BCM2711 — do not add +crypto;
#     gcc leaves it off for -mcpu=cortex-a72 by default, which is correct.
#   - Codec:  the Pi 4 exposes a V4L2 mem2mem H.264/HEVC decoder and an
#     H.264 encoder (bcm2835-codec) — see PROMEKI_ENABLE_V4L2 below.

# ---------------------------------------------------------------------------
# Inherit the generic aarch64-linux cross preset (toolchain file, host-only
# feature lockout, component selection, build hygiene).
# ---------------------------------------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/cross-aarch64-linux.cmake)

# ---------------------------------------------------------------------------
# SoC tuning — Cortex-A72.  -mcpu lets gcc pick a matching -mtune and the
# A72's available ISA extensions (incl. CRC) automatically; prefer it over
# a hand-rolled -march.  Override on the command line for a different Pi
# (e.g. -DPROMEKI_TARGET_CPU=cortex-a76 for a Pi 5's BCM2712).
# ---------------------------------------------------------------------------
promeki_config_string(PROMEKI_TARGET_CPU "cortex-a72")

# ---------------------------------------------------------------------------
# V4L2 — the Pi 4 has a real hardware codec behind V4L2 mem2mem
# (bcm2835-codec: H.264/HEVC decode, H.264 encode), which is exactly what
# the V4l2VideoEncoder / V4l2VideoDecoder backends target.  Enable it.
#
# Requirement: the target SYSROOT must carry the V4L2 uapi kernel headers
# (linux/videodev2.h) and, for the ALSA-backed audio capture paths,
# libasound2-dev:arm64.  On a Raspberry Pi OS sysroot both are present
# once you've installed `libasound2-dev` into it.  If your sysroot lacks
# them, override on the command line:  -DPROMEKI_ENABLE_V4L2=OFF
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_V4L2 ON)

# ---------------------------------------------------------------------------
# DMABUF / dma-heap — the Pi 4's V4L2 codec and KMS path support dma-buf
# import/export, so the zero-copy buffer backend is usable here.  It only
# needs the Linux dma-heap uapi headers (linux/dma-heap.h) in the sysroot,
# no external libraries.  Off by default in the base; turn it on for the Pi.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_DMABUF ON)

# ---------------------------------------------------------------------------
# C++ runtime ABI — pin the cross compiler to GCC 14 (match Pi OS Trixie).
#
# libpromeki uses C++20 std::format, whose libstdc++ support first shipped
# in GCC 13 — so the code cannot be built against an older runtime.  This
# preset therefore targets Raspberry Pi OS based on Debian 13 "Trixie",
# which ships GCC 14 (libstdc++ with std::format; newest symbol versions
# GLIBCXX_3.4.33 / CXXABI_1.3.15) and glibc 2.41.
#
# The compiler vintage must match the target's libstdc++: a binary built
# with a newer g++ than the Pi runs demands GLIBCXX/CXXABI versions the
# device's libstdc++.so.6 cannot export, and fails to load.  So pin the
# cross compiler to GCC 14.  Install it on the host:
#
#     sudo apt install gcc-14-aarch64-linux-gnu g++-14-aarch64-linux-gnu
#
# and the suffix below selects aarch64-linux-gnu-{gcc,g++}-14.  Pair this
# with a Trixie arm64 sysroot built by scripts/build-rpi4-sysroot.sh.
#
# (Targeting stock Bookworm instead is not possible without bundling a
# private libstdc++: its GCC 12 runtime lacks std::format entirely.)
# ---------------------------------------------------------------------------
promeki_config_string(PROMEKI_CROSS_TOOLCHAIN_SUFFIX "-14")

# ---------------------------------------------------------------------------
# Default sysroot location.  Built by scripts/build-rpi4-sysroot.sh into
# /mnt/data/sysroots/rpi4 (Debian Bookworm arm64 = Pi OS 64-bit glibc).
# Left commented so the path isn't hard-baked into the preset — pass it on
# the command line, or uncomment to make this the default for everyone:
#
#   cmake -B build-rpi4 -DPROMEKI_CONFIG_FILE=cross-rpi4 \
#         -DPROMEKI_SYSROOT=/mnt/data/sysroots/rpi4 \
#         -DPROMEKI_STAGING_PREFIX=/mnt/data/sysroots/rpi4
#
# promeki_config_path(PROMEKI_SYSROOT        "/mnt/data/sysroots/rpi4")
# promeki_config_path(PROMEKI_STAGING_PREFIX "/mnt/data/sysroots/rpi4")
