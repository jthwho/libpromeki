# PROMEKI CMake Config Files

This directory holds reusable build presets that can be applied via:

```sh
cmake -S . -B build -DPROMEKI_CONFIG_FILE=<spec>
```

`<spec>` can be:

| Form                                  | Example                                                   |
| ------------------------------------- | --------------------------------------------------------- |
| Absolute path                         | `-DPROMEKI_CONFIG_FILE=/etc/promeki/site.cmake`           |
| Path relative to the source root      | `-DPROMEKI_CONFIG_FILE=etc/my-overrides.cmake`            |
| Path relative to `cmake/configs/`     | `-DPROMEKI_CONFIG_FILE=presets/headless.cmake`            |
| Bare name (with or without `.cmake`)  | `-DPROMEKI_CONFIG_FILE=minimal`                           |

The first match is used.  Once a config has been loaded for a build directory,
the file is registered as a configure-time dependency: editing the file and
re-running `build` (or `cmake --build`) re-runs CMake configure automatically.

## Stock configs

### Feature presets

| File                       | Use when…                                                                    |
| -------------------------- | ---------------------------------------------------------------------------- |
| `default.cmake`            | You just want the project's normal defaults written out explicitly.          |
| `minimal.cmake`            | You want the smallest possible promeki: core library only, no codecs/UI/net. |
| `headless-server.cmake`    | Server-side workloads (ingest, transcode, mux): network + proav, no UI.      |
| `media-workstation.cmake`  | Desktop development with the full set of codecs + TUI + SDL3 viewer.         |
| `full.cmake`               | Everything turned on, including optional GPU / SDK-gated backends.           |
| `docs-only.cmake`          | Build only the Doxygen `docs` target (equivalent of `-DPROMEKI_DOCS_ONLY=ON`). |

### Build-type presets

| File              | Use when…                                                                       |
| ----------------- | ------------------------------------------------------------------------------- |
| `debug.cmake`     | Forces `CMAKE_BUILD_TYPE=Debug` (-O0 -g, asserts on).                           |
| `release.cmake`   | Forces `CMAKE_BUILD_TYPE=Release` (-O3 -DNDEBUG, no debug info).                |
| `asan.cmake`      | Debug + `-fsanitize=address,undefined`.                                         |

The build-type presets don't touch feature flags — combine them with a
feature preset by including one from the other:

```cmake
# my-headless-asan.cmake
include(${CMAKE_CURRENT_LIST_DIR}/headless-server.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/asan.cmake)
```

(Order matters: the later `include()` wins on overlapping settings.)

### Cross-compile presets

| File                            | Target                                                              |
| ------------------------------- | ------------------------------------------------------------------- |
| `cross-aarch64-linux.cmake`     | Linux / aarch64 via Debian's `aarch64-linux-gnu-{gcc,g++}` toolchain. |

Cross configs set `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` /
`CMAKE_SYSTEM_NAME` / `CMAKE_SYSTEM_PROCESSOR` and force off the
host-only feature flags (CUDA, NDI, JPEG XS, sanitizers).  These
variables are read by `project()` exactly once — switching toolchains
later requires wiping the build directory.

#### Cross-toolchain inputs

The aarch64 toolchain (and any future cross toolchain that opts in)
reads several tunables from CMake variables, the cache, or the
environment.  Precedence: explicit CMake variable (`-D` or config-file
`promeki_config_*`) > cache > environment variable.  All are optional.

| Variable                          | Default                  | Purpose                                                            |
| --------------------------------- | ------------------------ | ------------------------------------------------------------------ |
| `PROMEKI_CROSS_TOOLCHAIN_PREFIX`  | `aarch64-linux-gnu-`     | Compiler binary prefix; appended `gcc`/`g++`/`ar`/`ranlib`/... .   |
| `PROMEKI_SYSROOT`                 | (unset)                  | Target sysroot — drives `--sysroot=`, `CMAKE_FIND_ROOT_PATH`, and `PKG_CONFIG_*`. |
| `PROMEKI_STAGING_PREFIX`          | (unset)                  | `CMAKE_STAGING_PREFIX` — install destination during cross builds.  |
| `PROMEKI_TARGET_ARCH`             | (unset)                  | `-march=<value>` (e.g. `armv8-a+crc`).                             |
| `PROMEKI_TARGET_CPU`              | (unset)                  | `-mcpu=<value>` (e.g. `cortex-a72`, `cortex-a78`, `neoverse-n1`).  |
| `PROMEKI_TARGET_TUNE`             | (unset)                  | `-mtune=<value>`.                                                  |

When `PROMEKI_SYSROOT` is set the toolchain also overrides
`PKG_CONFIG_LIBDIR`, `PKG_CONFIG_SYSROOT_DIR`, and `PKG_CONFIG_PATH`
so `pkg_check_modules()` returns sysroot-relative paths instead of
silently leaking host packages into the build.

Example — Raspberry Pi 4 against a Yocto SDK:

```sh
cmake -B build-rpi4 \
      -DPROMEKI_CONFIG_FILE=cross-aarch64-linux \
      -DPROMEKI_CROSS_TOOLCHAIN_PREFIX=/opt/poky/4.0/sysroots/x86_64-pokysdk-linux/usr/bin/aarch64-poky-linux/aarch64-poky-linux- \
      -DPROMEKI_SYSROOT=/opt/poky/4.0/sysroots/cortexa72-poky-linux \
      -DPROMEKI_STAGING_PREFIX=/opt/poky/4.0/sysroots/cortexa72-poky-linux \
      -DPROMEKI_TARGET_CPU=cortex-a72
```

See `docs/building.md` (the `Custom sysroot`, `Compiler from a vendor
SDK`, `Installing the cross build`, and `CPU / ABI tuning` subsections)
for the long-form walkthrough.  All inputs can also be hard-coded in a
config file — see the commented `promeki_config_*` block in
`cross-aarch64-linux.cmake`.

## Writing your own

Configs are plain CMake.  Use the helper macros defined in the top-level
`CMakeLists.txt` so values are pushed into the cache with `FORCE` — this
makes config edits propagate on reconfigure (CMake would otherwise honour
the prior cache entry):

```cmake
promeki_config_option  (<NAME> ON|OFF)   # for boolean options
promeki_config_string  (<NAME> "...")    # for string options (e.g. PROMEKI_SANITIZER, CMAKE_BUILD_TYPE)
promeki_config_path    (<NAME> "...")    # for directory paths   (e.g. PROMEKI_NDI_SDK_DIR)
promeki_config_filepath(<NAME> "...")    # for file paths        (e.g. CMAKE_TOOLCHAIN_FILE, CMAKE_C_COMPILER)
```

You can `include()` other configs from inside a config — start from one of
the stock presets and override only what you need:

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/configs/headless-server.cmake)
promeki_config_option(PROMEKI_ENABLE_NVENC ON)
promeki_config_path  (PROMEKI_NVENC_SDK_DIR "/opt/nvidia/video-codec-sdk")
```

## Precedence

Values set by a config file override any `-DPROMEKI_*` values passed on
the same `cmake` invocation, because the helpers use cache-FORCE.  This
is intentional — a config is meant to be an authoritative preset.  To
override one of its settings, edit the file (or copy it and point at
the copy) rather than relying on command-line `-D`.

## Available variables

These are the options a config can set.  See the top-level `CMakeLists.txt`
for the authoritative list and documentation of each.

### CMake build-type / toolchain (read by `project()` — must be in the config, not just on `-D`)
```
CMAKE_BUILD_TYPE             Debug / Release / DevRelease / RelWithDebInfo / MinSizeRel
CMAKE_TOOLCHAIN_FILE         path to a CMake toolchain file
CMAKE_C_COMPILER             explicit C compiler   (cross-compile)
CMAKE_CXX_COMPILER           explicit C++ compiler (cross-compile)
CMAKE_SYSTEM_NAME            target OS for cross builds
CMAKE_SYSTEM_PROCESSOR       target arch for cross builds
CMAKE_FIND_ROOT_PATH         where find_package / find_library look
CMAKE_FIND_ROOT_PATH_MODE_*  per-mode override of the above
CMAKE_STAGING_PREFIX         on-disk install destination during cross builds
PROMEKI_CROSS_TOOLCHAIN_PREFIX  cross compiler binary prefix (see "Cross-toolchain inputs")
PROMEKI_SYSROOT                 target sysroot                (see "Cross-toolchain inputs")
PROMEKI_STAGING_PREFIX          drives CMAKE_STAGING_PREFIX   (see "Cross-toolchain inputs")
PROMEKI_TARGET_ARCH             -march=                       (see "Cross-toolchain inputs")
PROMEKI_TARGET_CPU              -mcpu=                        (see "Cross-toolchain inputs")
PROMEKI_TARGET_TUNE             -mtune=                       (see "Cross-toolchain inputs")
```

The toolchain / compiler variables can ONLY be honoured on the very
first configure of a build directory — CMake locks the detected
compiler into the cache after that.  To switch toolchains in an
existing build dir, wipe it first (`rm -rf build/`).

### Component builds
```
PROMEKI_BUILD_TUI            PROMEKI_BUILD_SDL
PROMEKI_BUILD_UTILS          PROMEKI_BUILD_DEMOS
PROMEKI_BUILD_TESTS          PROMEKI_BUILD_DOCS
PROMEKI_BUILD_BENCHMARKS     PROMEKI_BUILD_STATS
```

### Build hygiene
```
PROMEKI_WARNINGS_AS_ERRORS   PROMEKI_USE_PCH
PROMEKI_USE_CCACHE           PROMEKI_SANITIZER (string)
PROMEKI_DOCS_ONLY
```

### Feature flags
```
PROMEKI_ENABLE_NETWORK       PROMEKI_ENABLE_HTTP
PROMEKI_ENABLE_TLS           PROMEKI_ENABLE_SRT
PROMEKI_ENABLE_PROAV         PROMEKI_ENABLE_MUSIC
PROMEKI_ENABLE_PNG           PROMEKI_ENABLE_JPEG
PROMEKI_ENABLE_JPEGXS        PROMEKI_ENABLE_FREETYPE
PROMEKI_ENABLE_AUDIO         PROMEKI_ENABLE_FLAC
PROMEKI_ENABLE_VORBIS        PROMEKI_ENABLE_MP3
PROMEKI_ENABLE_OPUS          PROMEKI_ENABLE_AAC
PROMEKI_ENABLE_SRC
PROMEKI_ENABLE_CSC           PROMEKI_ENABLE_CIRF
PROMEKI_ENABLE_V4L2          PROMEKI_ENABLE_MEMFD
PROMEKI_ENABLE_CUDA          PROMEKI_ENABLE_NVENC
PROMEKI_ENABLE_NVDEC         PROMEKI_ENABLE_NDI
PROMEKI_ENABLE_NTV2
```

### External SDK paths (path)
```
PROMEKI_NVENC_SDK_DIR        PROMEKI_NDI_SDK_DIR
```

### Vendored vs system deps
```
PROMEKI_USE_SYSTEM_ZLIB          PROMEKI_USE_SYSTEM_LIBSPNG
PROMEKI_USE_SYSTEM_LIBJPEG       PROMEKI_USE_SYSTEM_SVT_JPEG_XS
PROMEKI_USE_SYSTEM_FREETYPE      PROMEKI_USE_SYSTEM_SNDFILE
PROMEKI_USE_SYSTEM_OPUS          PROMEKI_USE_SYSTEM_FDKAAC
PROMEKI_USE_SYSTEM_OGG           PROMEKI_USE_SYSTEM_FLAC
PROMEKI_USE_SYSTEM_VORBIS        PROMEKI_USE_SYSTEM_MPG123
PROMEKI_USE_SYSTEM_LAME          PROMEKI_USE_SYSTEM_SAMPLERATE
PROMEKI_USE_SYSTEM_NLOHMANN_JSON
PROMEKI_USE_SYSTEM_PUGIXML       PROMEKI_USE_SYSTEM_VTC
PROMEKI_USE_SYSTEM_HIGHWAY       PROMEKI_USE_SYSTEM_CIRF
PROMEKI_USE_SYSTEM_LLHTTP        PROMEKI_USE_SYSTEM_MBEDTLS
PROMEKI_USE_SYSTEM_SRT           PROMEKI_USE_SYSTEM_NTV2
```
