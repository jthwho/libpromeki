# Cross-compile preset: Linux / aarch64 (ARM 64-bit).
#
# Targets the Debian / Ubuntu cross toolchain installed by:
#
#     sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# (Other distros ship the same compilers under the same triplet — the
# binary names below are the GNU multi-arch standard.)
#
# CMAKE_TOOLCHAIN_FILE, CMAKE_C_COMPILER, CMAKE_CXX_COMPILER,
# CMAKE_SYSTEM_NAME, and CMAKE_SYSTEM_PROCESSOR are read by project()
# exactly once — on the first cmake invocation in a given build
# directory.  Changes to these variables on subsequent reconfigures
# are ignored; wipe the build dir to switch toolchains.
#
# Most host-only features (CUDA, NVENC/NVDEC, NDI, JPEG XS x86 asm,
# V4L2 host capture, sanitizers) are forced OFF below — the SDKs are
# x86_64 libraries and won't link, and the host probes would
# false-positive against the running x86_64 system.  Network / TLS /
# proav stay ON because they cross-build fine.

# ---------------------------------------------------------------------------
# Toolchain — set BEFORE project() so CMake's compiler detection picks
# the cross binaries instead of the host gcc.
#
# We delegate to a real CMake toolchain file rather than poking
# CMAKE_SYSTEM_PROCESSOR / CMAKE_C_COMPILER directly here.  Setting those
# individually is enough to get the right compiler picked, but it does
# NOT flip CMAKE_CROSSCOMPILING when host and target share an OS name
# (Linux → Linux/aarch64), and project() then resets CMAKE_SYSTEM_PROCESSOR
# back to the host's value.  A toolchain file is the path CMake honours,
# and it also gets forwarded to every ExternalProject_Add sub-build so
# vendored thirdparty libraries cross-compile too.
# ---------------------------------------------------------------------------
promeki_config_filepath(CMAKE_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/aarch64-linux-gnu.cmake")

# ---------------------------------------------------------------------------
# Toolchain inputs (all optional)
# ---------------------------------------------------------------------------
# The toolchain file reads several tunables from CMake variables, the
# cache, or the environment.  All have working defaults; set any that
# the target needs.  Anything set here via promeki_config_*() is
# cache-FORCEd and therefore wins over a same-invocation -D... .
#
# PROMEKI_CROSS_TOOLCHAIN_PREFIX
#     Compiler binary prefix.  Default "aarch64-linux-gnu-" (Debian).
#     Yocto / Buildroot / vendor SDKs typically ship under a different
#     name — e.g. "/opt/yocto/sdk/.../aarch64-poky-linux-".  The
#     toolchain appends "gcc" / "g++" / "ar" / "ranlib" / ... to it.
#
# PROMEKI_SYSROOT
#     Absolute path to the target sysroot (Yocto / Buildroot / vendor
#     BSP).  When set, fed to the compiler as --sysroot=<path>, prepended
#     to CMAKE_FIND_ROOT_PATH, and used to wire pkg-config so
#     pkg_check_modules() returns sysroot-relative paths.  Unset →
#     Debian's /usr/aarch64-linux-gnu staging area is used.
#
# PROMEKI_STAGING_PREFIX
#     Absolute path used as CMAKE_STAGING_PREFIX, so that
#     `cmake --install --prefix /usr` lands inside this directory
#     instead of the host's /usr.  Typically the same value as
#     PROMEKI_SYSROOT.
#
# PROMEKI_TARGET_ARCH / PROMEKI_TARGET_CPU / PROMEKI_TARGET_TUNE
#     Forwarded as -march= / -mcpu= / -mtune= compile flags.  Empty by
#     default — generic aarch64.  Prefer -mcpu= on a known SoC
#     (cortex-a72, cortex-a78, neoverse-n1, ...) for a measurable
#     codec / DSP speedup.
#
# Set via -D on the command line:
#
#     cmake -B build -DPROMEKI_CONFIG_FILE=cross-aarch64-linux \
#                    -DPROMEKI_SYSROOT=/opt/my-bsp/sysroot \
#                    -DPROMEKI_STAGING_PREFIX=/opt/my-bsp/sysroot \
#                    -DPROMEKI_TARGET_CPU=cortex-a72
#
# Or uncomment and edit below to hard-code per-target defaults in this
# preset (or in a wrapper config that include()s this one).
# ---------------------------------------------------------------------------
# promeki_config_string(PROMEKI_CROSS_TOOLCHAIN_PREFIX "aarch64-linux-gnu-")
# promeki_config_path  (PROMEKI_SYSROOT                "/opt/my-bsp/sysroot")
# promeki_config_path  (PROMEKI_STAGING_PREFIX         "/opt/my-bsp/sysroot")
# promeki_config_string(PROMEKI_TARGET_ARCH            "armv8-a+crc")
# promeki_config_string(PROMEKI_TARGET_CPU             "cortex-a72")
# promeki_config_string(PROMEKI_TARGET_TUNE            "cortex-a72")

# ---------------------------------------------------------------------------
# Build hygiene
# ---------------------------------------------------------------------------
# DevRelease is fine for cross-builds.  Pick Release to strip debug info.
# promeki_config_string(CMAKE_BUILD_TYPE "Release")
promeki_config_option(PROMEKI_WARNINGS_AS_ERRORS OFF)
promeki_config_option(PROMEKI_USE_PCH            ON)
promeki_config_option(PROMEKI_USE_CCACHE         ON)
# Sanitizers compile but at this point we usually don't intend to run
# the cross binaries on the build host — keep them off by default.
promeki_config_string(PROMEKI_SANITIZER          "")

# ---------------------------------------------------------------------------
# Component builds — typical for a server-class ARM target (no SDL viewer).
# ---------------------------------------------------------------------------
# WIP cross-compile bring-up: temporarily disabled to keep the surface small
# while the core library compiles cleanly.  Re-enable as each layer comes up.
promeki_config_option(PROMEKI_BUILD_TUI         ON)
promeki_config_option(PROMEKI_BUILD_SDL         OFF)  # SDL3 not yet packaged for cross.
promeki_config_option(PROMEKI_BUILD_UTILS       ON)
promeki_config_option(PROMEKI_BUILD_DEMOS       ON)
promeki_config_option(PROMEKI_BUILD_TESTS       ON)
promeki_config_option(PROMEKI_BUILD_DOCS        OFF)
promeki_config_option(PROMEKI_BUILD_BENCHMARKS  OFF)
promeki_config_option(PROMEKI_BUILD_STATS       OFF)

# ---------------------------------------------------------------------------
# Feature flags — cross-build-friendly subset.  Host-only / x86-only
# backends are forced OFF.
#
# WIP layered bring-up — start with everything OFF, turn the next layer
# ON once the previous one cross-builds clean.  Final state (all ON
# except JPEGXS / CUDA / NVENC / NVDEC / NDI) is restored when the full
# stack is green.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_NETWORK   ON)
promeki_config_option(PROMEKI_ENABLE_HTTP      ON)
promeki_config_option(PROMEKI_ENABLE_TLS       ON)
promeki_config_option(PROMEKI_ENABLE_SRT       ON)
promeki_config_option(PROMEKI_ENABLE_PROAV     ON)
promeki_config_option(PROMEKI_ENABLE_MUSIC     OFF)
promeki_config_option(PROMEKI_ENABLE_PNG       ON)
promeki_config_option(PROMEKI_ENABLE_JPEG      ON)
promeki_config_option(PROMEKI_ENABLE_JPEGXS    OFF)  # SVT-JPEG-XS is x86-only.
# x264 cross-builds for aarch64 — its NEON kernels assemble with the C
# toolchain (no nasm/yasm), and the build derives --host / --cross-prefix
# from the toolchain automatically.  GPL-2.0-or-later: enabling it makes
# this build GPL.
promeki_config_option(PROMEKI_ENABLE_X264      ON)
promeki_config_option(PROMEKI_ENABLE_FREETYPE  ON)
promeki_config_option(PROMEKI_ENABLE_AUDIO     ON)
promeki_config_option(PROMEKI_ENABLE_OPUS      ON)
promeki_config_option(PROMEKI_ENABLE_AAC       ON)
promeki_config_option(PROMEKI_ENABLE_SRC       ON)
promeki_config_option(PROMEKI_ENABLE_CSC       ON)
promeki_config_option(PROMEKI_ENABLE_CIRF      ON)
# V4L2 is Linux + the runtime kernel headers — the target sysroot
# usually has them.  Pin OFF if your target sysroot doesn't.
promeki_config_option(PROMEKI_ENABLE_V4L2      OFF)  # WIP: needs libasound2-dev:arm64 in sysroot.
promeki_config_option(PROMEKI_ENABLE_MEMFD     ON)
# NVIDIA / NDI bits are x86_64 SDKs at this time.
promeki_config_option(PROMEKI_ENABLE_CUDA      OFF)
promeki_config_option(PROMEKI_ENABLE_NVENC     OFF)
promeki_config_option(PROMEKI_ENABLE_NVDEC     OFF)
promeki_config_option(PROMEKI_ENABLE_NDI       OFF)
# libajantv2 cross-builds, but needs libudev-dev:arm64 in the sysroot.
# Pin OFF for the WIP bring-up; flip ON once the target sysroot is
# verified to have libudev.
promeki_config_option(PROMEKI_ENABLE_NTV2      OFF)  # WIP: needs libudev-dev:arm64 in sysroot.
