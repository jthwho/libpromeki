# Building and Installing libpromeki {#building}

Complete build, install, and downstream-integration guide.

This page is the canonical reference for building libpromeki from
source, configuring its feature flags, running its tests, installing
it, and consuming it from a downstream CMake project. Every
user-facing CMake option is documented here.

## Requirements {#building_requirements}

- A C++20 compiler (GCC 11+ or Clang 14+ recommended)
- CMake 3.22 or newer
- `git` (required — CMake auto-initializes the
  vendored-dependency submodules under `thirdparty/` on first
  configure; only the ones the active feature set actually needs are
  fetched, see @ref building_submodules "Submodule auto-init" below)
- Python 3 with the `jsonschema` and `jinja2` modules (only when
  `PROMEKI_ENABLE_TLS` is on, the default).  The vendored
  mbedTLS 3.6 LTS runs a build-time PSA Crypto driver-wrapper
  generator that imports both modules.  Install with one of:

  ```sh
  # Debian / Ubuntu
  sudo apt install python3-jsonschema python3-jinja2

  # Fedora / RHEL
  sudo dnf install python3-jsonschema python3-jinja2

  # Generic / macOS — virtualenv recommended
  python3 -m pip install jsonschema jinja2
  ```

  If TLS support is not needed, configure with
  `-DPROMEKI_ENABLE_TLS=OFF` to skip the requirement.

Some vendored libraries (libjpeg-turbo, SVT-JPEG-XS) contain
hand-written SIMD assembly. When `nasm` or `yasm` is on `PATH`
those libraries build their accelerated code paths; without an
assembler they still build, but without the SIMD fast paths.

Optional host tooling picked up automatically when present:

| Tool | Purpose |
|------|---------|
| `ccache` | Compiler launcher — near-zero cost cache hits on branch switches and whitespace edits |
| `/usr/bin/time` (GNU) | Records per-TU compile wall-time and peak RSS (`PROMEKI_BUILD_STATS`) |
| `doxygen` | Builds this documentation (`PROMEKI_BUILD_DOCS`) |
| CUDA Toolkit | Enables GPU MemSpaces and is required for NVENC / NVDEC |
| NVIDIA Video Codec SDK | Enables NVENC / NVDEC &mdash; see @ref nvenc "NVENC setup" |

---

## Quick Start {#building_quickstart}

```sh
git clone https://github.com/jthwho/libpromeki.git
cd libpromeki
cmake -B build
cmake --build build -j$(nproc)
```

This produces `libpromeki.so`, `libpromeki-tui.so`, the unit-test
binaries, the demos, and the utility programs under `build/` using
the default **DevRelease** build type.

`--recurse-submodules` on the `git clone` is optional — CMake
fetches the `thirdparty/*` submodules the active configuration
needs on first configure (see @ref building_submodules
"Submodule auto-init").  Cloning with `--recurse-submodules`
just front-loads the work.

For non-trivial configurations the recommended approach is a
@ref building_config_files "config file" rather than a long string
of `-D` flags:

```sh
cmake -B build -DPROMEKI_CONFIG_FILE=minimal           # core-only build
cmake -B build -DPROMEKI_CONFIG_FILE=asan              # Debug + AddressSanitizer
cmake -B build -DPROMEKI_CONFIG_FILE=cross-aarch64-linux   # Linux/ARM64 cross
```

---

## Build Types {#building_build_types}

libpromeki recognises three CMake build types. The build type
controls optimisation, debug symbols, and whether per-module debug
logging (`promekiDebug()`) is compiled in. See @ref debugging
"Debugging and Diagnostics" for the full discussion; the short
version is:

| Build type | Optimisation | Debug symbols | `promekiDebug()` | Typical use |
|------------|--------------|---------------|------------------|-------------|
| **Debug** | `-O0` | Yes | Compiled in | Step-through debugging, sanitizers |
| **DevRelease** (default) | `-O3` | Yes (`-g`) | Compiled in | Day-to-day development, CI, profiling |
| **Release** | `-O3` | No | Compiled out | Distribution / final packaging |

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

The `debug`, `release`, and `asan` @ref building_config_presets
"config presets" wrap these settings (plus, for `asan`,
`PROMEKI_SANITIZER=address,undefined`) so they can be combined with
a feature preset in a single `-DPROMEKI_CONFIG_FILE=` argument.

---

## Feature Flags {#building_feature_flags}

Feature flags control what gets compiled into `libpromeki.so`.
Disabling a feature removes its sources, its headers, and (when it is
the only consumer) its vendored third-party dependency. All flags
are exposed in the generated `include/promeki/config.h` so
consuming code can conditionally compile against a custom build.

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_ENABLE_NETWORK` | `ON` | Networking (sockets, RTP, SDP) |
| `PROMEKI_ENABLE_PROAV` | `ON` | Pro A/V (image, audio, color, pipeline) |
| `PROMEKI_ENABLE_MUSIC` | `ON` | Music / MIDI support |
| `PROMEKI_ENABLE_PNG` | `ON` | PNG image I/O (libspng) |
| `PROMEKI_ENABLE_JPEG` | `ON` | JPEG codec (libjpeg-turbo) |
| `PROMEKI_ENABLE_JPEGXS` | auto | JPEG XS codec (SVT-JPEG-XS; auto-enabled on x86-64 when nasm/yasm is present) |
| `PROMEKI_ENABLE_FREETYPE` | `ON` | FreeType font rendering |
| `PROMEKI_ENABLE_AUDIO` | `ON` | Audio file I/O (libsndfile) |
| `PROMEKI_ENABLE_SRC` | `ON` | Audio sample-rate conversion (libsamplerate) |
| `PROMEKI_ENABLE_CSC` | `ON` | SIMD color-space conversion (Highway) |
| `PROMEKI_ENABLE_CIRF` | `ON` | Compiled-in resource filesystem (`:/.PROMEKI/...`) |
| `PROMEKI_ENABLE_V4L2` | auto | V4L2 video capture + ALSA audio capture (Linux only; auto-enabled when the headers are present) |
| `PROMEKI_ENABLE_CUDA` | auto | CUDA support (device / pinned-host memspaces; prerequisite for NVENC / NVDEC) |
| `PROMEKI_ENABLE_NVENC` | auto | NVIDIA NVENC H.264 / HEVC encoder &mdash; see @ref nvenc "NVENC setup" |
| `PROMEKI_ENABLE_NVDEC` | auto | NVIDIA NVDEC H.264 / HEVC decoder &mdash; see @ref nvenc "NVENC setup" |
| `PROMEKI_ENABLE_NDI` | auto | NDI (Network Device Interface) media transport &mdash; see @ref ndi "NDI setup" |

Flags marked *auto* default `ON` when their prerequisites are
detected and `OFF` otherwise. Pass an explicit
`-DPROMEKI_ENABLE_X=ON` / `OFF` on the command line to override
the probe.

---

## Vendored vs. System Dependencies {#building_system_deps}

All third-party libraries are vendored as git submodules under
`thirdparty/` and linked statically with `-fPIC` into
`libpromeki.so`. This is the default and the recommended
configuration — a single shared library with no transitive
dependency surprises.

Each vendored dependency can individually be switched to a
system-installed copy:

| Option | Default | Library |
|--------|---------|---------|
| `PROMEKI_USE_SYSTEM_ZLIB` | `OFF` | zlib / zlib-ng |
| `PROMEKI_USE_SYSTEM_LIBSPNG` | `OFF` | libspng |
| `PROMEKI_USE_SYSTEM_LIBJPEG` | `OFF` | libjpeg-turbo |
| `PROMEKI_USE_SYSTEM_SVT_JPEG_XS` | `OFF` | SVT-JPEG-XS |
| `PROMEKI_USE_SYSTEM_FREETYPE` | `OFF` | FreeType |
| `PROMEKI_USE_SYSTEM_SNDFILE` | `OFF` | libsndfile |
| `PROMEKI_USE_SYSTEM_SAMPLERATE` | `OFF` | libsamplerate |
| `PROMEKI_USE_SYSTEM_NLOHMANN_JSON` | `OFF` | nlohmann/json |
| `PROMEKI_USE_SYSTEM_VTC` | `OFF` | libvtc |
| `PROMEKI_USE_SYSTEM_HIGHWAY` | `OFF` | Highway |
| `PROMEKI_USE_SYSTEM_CIRF` | `OFF` | cirf |

Vendored dependencies currently in the tree: zlib-ng, libspng,
libjpeg-turbo, SVT-JPEG-XS, FreeType, libsndfile, libsamplerate,
nlohmann/json, libvtc, Highway, cirf, doctest.

---

## Submodule auto-init {#building_submodules}

The first `cmake -B build` walks `thirdparty/*` and initializes
only the git submodules the active configuration actually needs.
A submodule is fetched when **both**:

- Its enabling feature flag is on
  (e.g. `PROMEKI_ENABLE_PNG` for `thirdparty/libspng`,
  `PROMEKI_ENABLE_NTV2` for `thirdparty/libajantv2`).
- Its corresponding `PROMEKI_USE_SYSTEM_*` opt-out is off
  (i.e. CMake will use the vendored copy rather than a system one).

Unconditional dependencies (`thirdparty/nlohmann-json`,
`thirdparty/pugixml`, `thirdparty/libvtc`) are pulled whenever
their `PROMEKI_USE_SYSTEM_*` opt-out is off.  Submodules that
aren't required are left untouched on disk — a `minimal` build
never clones libspng, libjpeg-turbo, FreeType, or anything else
it won't compile.

A submodule already populated on disk (either by a previous
configure, a manual `git submodule update`, or a source tarball
drop) is **not** re-fetched.  If a submodule fetch fails the
configure aborts with a `FATAL_ERROR` that includes the exact git
invocation to re-run by hand.

Source trees with no `.git` directory (release tarballs, archive
drops) skip the auto-init step entirely — the user / packager is
expected to have staged `thirdparty/` contents themselves.

The logic lives in `cmake/PromekiSubmodules.cmake`.  To add a new
vendored dependency, add a path to `_PROMEKI_SUBMODULE_PATHS` and a
matching branch in `_promeki_submodule_required()` describing the
flag combination that requires it.

### Mirror configuration {#building_submodule_mirrors}

To fetch the submodules from an internal git mirror instead of
their upstream URLs — useful on air-gapped networks, behind a
slow uplink, or when standing up a reproducible offline build —
point CMake at a mirrors config file:

```sh
cmake -B build -DPROMEKI_MIRRORS_FILE=/path/to/mirrors.cmake
```

The file is a small subset of CMake that sets one variable,
`PROMEKI_MIRRORS`, as a flat list of *(upstream-url, mirror-url)*
pairs.  Each pair becomes a
`git -c url.<mirror>.insteadOf=<upstream>` argument passed to
`git submodule update --init`, so git's longest-match rule
applies — a per-repo entry overrides a blanket prefix rewrite for
the same URL.

```cmake
# mirrors.cmake — example
set(BASE "ssh://git@gitlab.example.com:22")

set(PROMEKI_MIRRORS
    "https://github.com/nlohmann/json.git"
        "${BASE}/thirdparty/nlohmann-json.git"
    "https://github.com/libsndfile/libsndfile.git"
        "${BASE}/thirdparty/libsndfile.git"

    # Blanket prefix rewrite — catches everything else on github.com.
    "https://github.com/"
        "ssh://git@github.com/"
)
```

A fully-commented template ships at `cmake/mirrors.example.cmake`.

If `-DPROMEKI_MIRRORS_FILE=` is not given, CMake searches a list
of well-known locations and uses the first one that exists.
Search order (first hit wins):

1. The `$PROMEKI_MIRRORS_FILE` environment variable.
2. `<repo-root>/mirrors.cmake` — repo-local (gitignored).
3. Per-user config:
   - Linux: `$XDG_CONFIG_HOME/promeki/mirrors.cmake`
     (default `~/.config/promeki/mirrors.cmake`).
   - macOS: `~/Library/Application Support/promeki/mirrors.cmake`
     (XDG fallback `~/.config/promeki/mirrors.cmake`).
   - Windows: `%APPDATA%\promeki\mirrors.cmake`.
4. System-wide config:
   - Linux: `/etc/promeki/mirrors.cmake`.
   - macOS: `/Library/Application Support/promeki/mirrors.cmake`
     (or `/etc/promeki/mirrors.cmake`).
   - Windows: `%PROGRAMDATA%\promeki\mirrors.cmake`.

The same file feeds `scripts/mirror-thirdparty.py`, which
populates / refreshes the mirror end of the relationship: walks
`.gitmodules`, looks each submodule's upstream URL up in
`PROMEKI_MIRRORS`, optionally creates the matching GitLab project
via the API (when `PROMEKI_MIRROR_API` is set and a token is
available), and `git push --mirror`s the latest upstream refs to
it.  Run `scripts/mirror-thirdparty.py --help` for the full
flag set — the script auto-discovers the same config file CMake
does, so a single file drives both build-time fetch overrides and
mirror maintenance.

---

## Build Targets {#building_build_targets}

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_BUILD_TUI` | `ON` | Build the `promeki-tui` library |
| `PROMEKI_BUILD_SDL` | auto | Build the `promeki-sdl` library (auto-enabled when SDL3 is found) |
| `PROMEKI_BUILD_TESTS` | `ON` | Configure unit-test targets (excluded from the default `all` build — see [Running Tests](#building_tests)) |
| `PROMEKI_BUILD_UTILS` | `ON` | Build the utility applications (`promeki-info`, `mediaplay`, `imgtest`, ...) |
| `PROMEKI_BUILD_DEMOS` | `ON` | Build the demonstration applications |
| `PROMEKI_BUILD_BENCHMARKS` | `ON` | Build the `promeki-bench` driver |
| `PROMEKI_BUILD_DOCS` | `OFF` | Add the generated documentation to the `all` target |
| `PROMEKI_WARNINGS_AS_ERRORS` | `OFF` | Treat compile warnings as errors (`-Werror`) for our targets — flipped on by `scripts/precommit.sh` |

Three test executables are produced: `unittest-promeki`,
`unittest-tui`, and `unittest-sdl`. See [Running Tests](#building_tests).

---

## Build Performance Options {#building_perf_options}

These flags don't affect the produced binary — they trade disk
space and first-build time for faster incremental rebuilds.

| Option | Default | Effect |
|--------|---------|--------|
| `PROMEKI_USE_CCACHE` | `ON` | Use `ccache` as a compiler launcher when it is on `PATH`; silently skipped otherwise |
| `PROMEKI_USE_PCH` | `ON` | Precompile the hot stable headers (stdlib + promeki infrastructure) to skip re-parsing them on every TU |
| `PROMEKI_BUILD_STATS` | `ON` | Record per-TU compile wall-time and peak RSS to `build/promeki-build-stats.log` (requires GNU `/usr/bin/time`) |

The build-stats log can be summarised with
`scripts/build-stats-report.sh` for identifying the slowest /
heaviest translation units. The two options compose cleanly — with
ccache enabled, a cache hit logs near-zero wall time and a small
peak RSS so the report distinguishes real compiles from cached
ones.

---

## Configuration Files {#building_config_files}

Instead of listing every option on the command line, you can
preconfigure a build by pointing CMake at a config file:

```sh
cmake -S . -B build -DPROMEKI_CONFIG_FILE=<spec>
```

`<spec>` may be any of:

| Form                                  | Example                                                   |
| ------------------------------------- | --------------------------------------------------------- |
| Absolute path                         | `-DPROMEKI_CONFIG_FILE=/etc/promeki/site.cmake`           |
| Path relative to the source root      | `-DPROMEKI_CONFIG_FILE=etc/my-overrides.cmake`            |
| Path relative to `cmake/configs/`     | `-DPROMEKI_CONFIG_FILE=presets/headless.cmake`            |
| Bare name (with or without `.cmake`)  | `-DPROMEKI_CONFIG_FILE=minimal`                           |

The first match is used.  A bare name is resolved against
`cmake/configs/<spec>.cmake`, where several stock presets ship out
of the box.  The chosen config is registered as a configure-time
dependency, so editing it and re-running `cmake --build` triggers
an automatic reconfigure.

### Stock presets {#building_config_presets}

Feature presets — pick the component / codec / network mix:

| File                       | Use when…                                                                    |
|----------------------------|------------------------------------------------------------------------------|
| `default.cmake`            | The project's normal defaults written out explicitly (good copy-from base).  |
| `minimal.cmake`            | Smallest possible promeki: core library only, no codecs / UI / network.      |
| `headless-server.cmake`    | Server-side workloads (ingest, transcode, mux): network + proav, no UI.      |
| `media-workstation.cmake`  | Desktop development with the full set of codecs + TUI + SDL3 viewer.         |
| `full.cmake`               | Everything turned on, including normally probe-gated GPU / SDK backends.     |
| `docs-only.cmake`          | Build only the Doxygen `docs` target (equivalent of `-DPROMEKI_DOCS_ONLY=ON`). |

Build-type presets — pin `CMAKE_BUILD_TYPE` / sanitizer without
taking a position on features:

| File              | Effect                                                          |
|-------------------|-----------------------------------------------------------------|
| `debug.cmake`     | Forces `CMAKE_BUILD_TYPE=Debug` (-O0 -g, asserts on).           |
| `release.cmake`   | Forces `CMAKE_BUILD_TYPE=Release` (-O3 -DNDEBUG, no debug info). |
| `asan.cmake`      | Debug + `-fsanitize=address,undefined`.                         |

Cross-compile presets — wire up the toolchain and force off
host-only feature flags in one go:

| File                            | Target                                                              |
|---------------------------------|---------------------------------------------------------------------|
| `cross-aarch64-linux.cmake`     | Linux / aarch64 via Debian's `aarch64-linux-gnu-{gcc,g++}` toolchain. |

Build-type and feature presets compose with `include()` — start
from a feature preset and layer a build-type preset on top:

```cmake
# my-headless-asan.cmake
include(${CMAKE_CURRENT_LIST_DIR}/headless-server.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/asan.cmake)
```

Order matters: the later `include()` wins on overlapping settings.

### Writing your own {#building_config_writing}

Configs are plain CMake files.  Crucially, they are loaded
**before** `project()` — which means a config can set the
CMake-level toolchain, compiler, and build-type variables that
`project()` reads during compiler detection.  Use the helper
macros below so values land in the cache with `FORCE` — this makes
config edits propagate on reconfigure even when the cache already
has a previous value:

```cmake
promeki_config_option  (<NAME> ON|OFF)   # boolean options
promeki_config_string  (<NAME> "...")    # string options
                                         # (CMAKE_BUILD_TYPE, PROMEKI_SANITIZER, ...)
promeki_config_path    (<NAME> "...")    # directory paths
                                         # (PROMEKI_NDI_SDK_DIR, ...)
promeki_config_filepath(<NAME> "...")    # file paths
                                         # (CMAKE_TOOLCHAIN_FILE, CMAKE_C_COMPILER, ...)
```

Variables a config commonly sets:

| Category              | Variables                                                                  |
|-----------------------|----------------------------------------------------------------------------|
| Component builds      | `PROMEKI_BUILD_TUI`, `PROMEKI_BUILD_SDL`, `PROMEKI_BUILD_UTILS`, `PROMEKI_BUILD_DEMOS`, `PROMEKI_BUILD_TESTS`, `PROMEKI_BUILD_DOCS`, `PROMEKI_BUILD_BENCHMARKS`, `PROMEKI_BUILD_STATS` |
| Build hygiene         | `PROMEKI_WARNINGS_AS_ERRORS`, `PROMEKI_USE_PCH`, `PROMEKI_USE_CCACHE`, `PROMEKI_SANITIZER` |
| Feature flags         | every `PROMEKI_ENABLE_*` (see @ref building_feature_flags "Feature Flags") |
| Vendored vs system    | every `PROMEKI_USE_SYSTEM_*` (see @ref building_system_deps "Vendored vs. System Dependencies") |
| External SDK paths    | `PROMEKI_NVENC_SDK_DIR`, `PROMEKI_NDI_SDK_DIR`                             |
| CMake build type      | `CMAKE_BUILD_TYPE`                                                         |
| CMake toolchain / cross-compile | `CMAKE_TOOLCHAIN_FILE`, `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`, `CMAKE_SYSTEM_NAME`, `CMAKE_SYSTEM_PROCESSOR`, `CMAKE_FIND_ROOT_PATH`, `CMAKE_FIND_ROOT_PATH_MODE_*`, `PROMEKI_CROSS_TOOLCHAIN_PREFIX`, `PROMEKI_SYSROOT`, `PROMEKI_STAGING_PREFIX`, `PROMEKI_TARGET_ARCH`, `PROMEKI_TARGET_CPU`, `PROMEKI_TARGET_TUNE` |

Layer overrides onto an existing preset by `include()`-ing it:

```cmake
# my-site.cmake — headless server with NVENC pinned on
include(${CMAKE_CURRENT_LIST_DIR}/headless-server.cmake)
promeki_config_option(PROMEKI_ENABLE_NVENC ON)
promeki_config_path  (PROMEKI_NVENC_SDK_DIR "/opt/nvidia/video-codec-sdk")
```

### Precedence {#building_config_precedence}

Values written by the config file **override** any `-D` values
supplied alongside it on the same `cmake` invocation, because the
helpers use cache-`FORCE`.  This is deliberate — a config is meant
to be an authoritative preset.  To override one of its settings,
edit the file (or copy it and point at the copy) rather than
relying on command-line `-D`.

### Cross-compile caveat {#building_config_cross_caveat}

`CMAKE_TOOLCHAIN_FILE`, `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`,
and the rest of the toolchain variables are read by `project()`
**exactly once**, on the first configure of a given build
directory.  Changing them in the config after that is silently
ignored.  To switch toolchains, wipe the build dir first
(`rm -rf build/`) — this is a CMake limitation, not specific to
this loader.

See `cmake/configs/README.md` for the authoritative list of
variables a config may set and the complete set of shipped
presets.

---

## Common Configurations {#building_configurations}

The simplest path is to use one of the stock @ref building_config_presets
"config presets".  The recipes below show both the preset-based form
and the direct `-D` form for reference.

### Minimal core-only build {#building_config_minimal}

Drops Pro A/V, networking, and music for a lean core-only library
suitable for command-line tools that just need the container / I/O /
string infrastructure:

```sh
# Preset (recommended):
cmake -B build -DPROMEKI_CONFIG_FILE=minimal

# Equivalent ad-hoc form:
cmake -B build \
    -DPROMEKI_ENABLE_PROAV=OFF \
    -DPROMEKI_ENABLE_NETWORK=OFF \
    -DPROMEKI_ENABLE_MUSIC=OFF
```

### Headless server {#building_config_headless}

Networking + Pro A/V for ingest / transcode / mux, no terminal UI,
no SDL viewer:

```sh
cmake -B build -DPROMEKI_CONFIG_FILE=headless-server
```

### System dependencies {#building_config_system_deps}

Use distro-packaged versions of the third-party libraries instead of
the vendored copies:

```sh
cmake -B build \
    -DPROMEKI_USE_SYSTEM_ZLIB=ON \
    -DPROMEKI_USE_SYSTEM_LIBSPNG=ON \
    -DPROMEKI_USE_SYSTEM_FREETYPE=ON \
    -DPROMEKI_USE_SYSTEM_SNDFILE=ON \
    -DPROMEKI_USE_SYSTEM_LIBJPEG=ON
```

(For a repeated build with this profile, copy
`cmake/configs/default.cmake` to a new file, flip the
`PROMEKI_USE_SYSTEM_*` options, and reference it via
`-DPROMEKI_CONFIG_FILE=`.)

### Release packaging {#building_config_release}

Full optimisation, no debug symbols, no `promekiDebug()` overhead:

```sh
# Preset:
cmake -B build -DPROMEKI_CONFIG_FILE=release

# Equivalent ad-hoc form:
cmake -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc)
cmake --install build --prefix /opt/promeki
```

### AddressSanitizer build {#building_config_asan}

Debug build with `-fsanitize=address,undefined` applied to every
target.  PCH and ccache are auto-disabled because they're
incompatible with the sanitizer cache key.

```sh
cmake -B build-asan -DPROMEKI_CONFIG_FILE=asan
cmake --build build-asan --target check
```

### Cross-compile for Linux / aarch64 {#building_config_cross_arm64}

Targets Debian / Ubuntu's `aarch64-linux-gnu-{gcc,g++}` cross
toolchain (install with `apt install g++-aarch64-linux-gnu`).
Forces off the host-only feature flags (CUDA, NDI, JPEG XS) so the
configure step doesn't false-positive against the running x86_64
system:

```sh
cmake -B build-arm64 -DPROMEKI_CONFIG_FILE=cross-aarch64-linux
cmake --build build-arm64 -j$(nproc)
```

Note: CMake locks the toolchain choice into the cache on the first
configure of a build directory.  To switch targets, point at a new
build directory or `rm -rf` the old one.

#### Toolchain inputs at a glance {#building_config_cross_inputs}

The aarch64 toolchain file (and any future cross toolchain that opts
in) reads the following tunables, in precedence order CMake variable
(`-D` or config-file `promeki_config_*`) > cache > environment
variable.  All are optional; defaults give a Debian / Ubuntu host a
working build out of the box.

| Variable                          | Default                  | Purpose                                                                    |
|-----------------------------------|--------------------------|----------------------------------------------------------------------------|
| `PROMEKI_CROSS_TOOLCHAIN_PREFIX`  | `aarch64-linux-gnu-`     | Compiler binary prefix — appended `gcc`/`g++`/`ar`/`ranlib`/`strip`/`objcopy`/`nm`/`readelf`/`ld`. Set this to a Yocto / Buildroot / vendor SDK prefix to consume their compiler. |
| `PROMEKI_SYSROOT`                 | (unset → `/usr/aarch64-linux-gnu`) | Absolute path to a target sysroot.  See @ref building_config_cross_sysroot "Custom sysroot" below. |
| `PROMEKI_STAGING_PREFIX`          | (unset)                  | Absolute path used as `CMAKE_STAGING_PREFIX`.  See @ref building_config_cross_install "Installing the cross build" below. |
| `PROMEKI_TARGET_ARCH`             | (unset)                  | `-march=<value>` (e.g. `armv8-a+crc`).                                     |
| `PROMEKI_TARGET_CPU`              | (unset)                  | `-mcpu=<value>` (e.g. `cortex-a72`, `cortex-a78`, `neoverse-n1`).  See @ref building_config_cross_tuning "CPU / ABI tuning" below. |
| `PROMEKI_TARGET_TUNE`             | (unset)                  | `-mtune=<value>`.                                                          |

Each resolved value is written back to the cache with `FORCE` so that
ExternalProject sub-builds (vendored thirdparty libraries) re-include
the toolchain file with the same inputs without further plumbing.

#### Compiler from a vendor SDK {#building_config_cross_compiler}

When using a Yocto, Buildroot, Linaro, or vendor SDK whose compiler
isn't installed under the Debian `aarch64-linux-gnu-` triplet, point
`PROMEKI_CROSS_TOOLCHAIN_PREFIX` at the SDK's prefix:

```sh
cmake -B build-arm64 \
      -DPROMEKI_CONFIG_FILE=cross-aarch64-linux \
      -DPROMEKI_CROSS_TOOLCHAIN_PREFIX=/opt/poky/4.0/sysroots/x86_64-pokysdk-linux/usr/bin/aarch64-poky-linux/aarch64-poky-linux-
```

The toolchain appends `gcc` / `g++` / `ar` / `ranlib` / `strip` /
`objcopy` / `nm` / `readelf` / `ld` to the prefix and pins each of
those into CMake's `CMAKE_<TOOL>` variables — this avoids the
"silently picked up the host `ar` instead of the cross `ar`" class of
bug that bites static-library archive contents on the target.

A SDK's `environment-setup-...` script normally exports compiler /
sysroot / pkg-config variables; you can source it instead of passing
`-D` flags, since the toolchain reads `PROMEKI_*` from the environment
as well.

#### Custom sysroot {#building_config_cross_sysroot}

To build against a vendor BSP, Yocto SDK, Buildroot staging directory,
or any other non-Debian target rootfs, set `PROMEKI_SYSROOT` to the
absolute path of the sysroot.  The toolchain file reads it and:

- Passes it to the compiler/linker via `--sysroot=<path>` (CMake's
  `CMAKE_SYSROOT` mechanism — it applies to every C/C++/link invocation).
- Prepends it to `CMAKE_FIND_ROOT_PATH` so `find_library`,
  `find_path`, and `find_package` resolve against the target rootfs
  first, then against Debian's `/usr/aarch64-linux-gnu` fallback.
- Overrides `PKG_CONFIG_LIBDIR`, `PKG_CONFIG_SYSROOT_DIR`, and
  `PKG_CONFIG_PATH` so `pkg_check_modules()` returns sysroot-relative
  paths.  This is the **single biggest cross-compile gotcha**:
  without it, the host's `.pc` files leak in and the build either
  links against host libraries (wrong arch, late link failure) or
  fails with confusing missing-header errors deep in compile.
- Stores the value in the cache so every `ExternalProject_Add`
  sub-build (vendored thirdparty libraries) inherits the same sysroot
  without further plumbing.

When `PROMEKI_SYSROOT` is unset, the toolchain falls back to Debian's
`/usr/aarch64-linux-gnu` multi-arch directory — the same layout the
apt-installed cross packages populate.  A path that does not exist or
is not a directory triggers a fast `FATAL_ERROR` at configure time
rather than a confusing later link failure.

Three sources are honoured, in precedence order:

1. An explicit CMake variable — `-DPROMEKI_SYSROOT=<path>` on the
   command line, or `promeki_config_path(PROMEKI_SYSROOT "...")` in a
   `PROMEKI_CONFIG_FILE`.
2. A cache entry left by a previous configure of the same build
   directory.
3. The `PROMEKI_SYSROOT` environment variable.

##### Command-line form

```sh
cmake -B build-arm64 \
      -DPROMEKI_CONFIG_FILE=cross-aarch64-linux \
      -DPROMEKI_SYSROOT=/opt/my-bsp/sysroot
```

##### Environment-variable form

```sh
export PROMEKI_SYSROOT=/opt/my-bsp/sysroot
cmake -B build-arm64 -DPROMEKI_CONFIG_FILE=cross-aarch64-linux
```

##### Config-file form

For repeated builds against the same target, hard-code the sysroot
in a wrapper config that layers on top of the stock cross preset:

```cmake
# cmake/configs/my-bsp.cmake
include(${CMAKE_CURRENT_LIST_DIR}/cross-aarch64-linux.cmake)
promeki_config_path(PROMEKI_SYSROOT "/opt/my-bsp/sysroot")
```

```sh
cmake -B build-arm64 -DPROMEKI_CONFIG_FILE=my-bsp
```

The `cross-aarch64-linux.cmake` preset itself contains a
commented-out `promeki_config_path(PROMEKI_SYSROOT ...)` line as a
template — uncomment and edit in place if you would rather not
maintain a separate wrapper.

A config-file `promeki_config_path(PROMEKI_SYSROOT ...)` is
cache-`FORCE`d and therefore wins over a same-invocation
`-DPROMEKI_SYSROOT=...` — this matches the general
@ref building_config_precedence "config-file precedence rule".

Although the variable is documented under the aarch64 toolchain, the
mechanism lives in the toolchain file rather than the parent project,
so any future cross-compile toolchain shipped under
`cmake/toolchains/` can opt in by reading the same variable.

#### Installing the cross build {#building_config_cross_install}

By default `cmake --install build-arm64 --prefix /usr` would write
into the **host's** `/usr` — almost certainly not what you want for
a cross build.  `PROMEKI_STAGING_PREFIX` reroutes the on-disk
destination while leaving `CMAKE_INSTALL_PREFIX` (the path baked into
RPATHs, `promeki-config.cmake`, and the like) untouched, so the
target sees a normal `/usr` layout while the install actually lands
inside the staging area:

```sh
cmake -B build-arm64 \
      -DPROMEKI_CONFIG_FILE=cross-aarch64-linux \
      -DPROMEKI_SYSROOT=/opt/my-bsp/sysroot \
      -DPROMEKI_STAGING_PREFIX=/opt/my-bsp/sysroot
cmake --build build-arm64 -j$(nproc)
cmake --install build-arm64 --prefix /usr
# files land at /opt/my-bsp/sysroot/usr/{lib,include,...}
```

A typical pattern is to set `PROMEKI_STAGING_PREFIX` to the same
value as `PROMEKI_SYSROOT` — install promotes the freshly-built
artifacts into the sysroot so a subsequent cross build can link
against them via `find_package(promeki)`.  Point it elsewhere
(e.g. `/tmp/promeki-staging`) if you want to package without
mutating the sysroot.

`DESTDIR=` still works alongside `PROMEKI_STAGING_PREFIX` — useful
for packaging:

```sh
DESTDIR=/tmp/pkg-root cmake --install build-arm64 --prefix /usr
# files land at /tmp/pkg-root/opt/my-bsp/sysroot/usr/...
```

#### CPU / ABI tuning {#building_config_cross_tuning}

`PROMEKI_TARGET_ARCH`, `PROMEKI_TARGET_CPU`, and `PROMEKI_TARGET_TUNE`
forward to the compiler as `-march=` / `-mcpu=` / `-mtune=` via
`CMAKE_C_FLAGS_INIT` and `CMAKE_CXX_FLAGS_INIT`, so they apply to
every TU including vendored thirdparty libraries.  Generic aarch64
is the default; opt in to a SoC-specific tune for a measurable
speedup in the codec and DSP hot paths:

```sh
cmake -B build-rpi4 \
      -DPROMEKI_CONFIG_FILE=cross-aarch64-linux \
      -DPROMEKI_SYSROOT=/opt/rpi4-sysroot \
      -DPROMEKI_TARGET_CPU=cortex-a72
```

| Target SoC                    | Recommended `PROMEKI_TARGET_CPU` |
|-------------------------------|----------------------------------|
| Raspberry Pi 4 / CM4          | `cortex-a72`                     |
| Raspberry Pi 5                | `cortex-a76`                     |
| NVIDIA Jetson Nano / TX1      | `cortex-a57`                     |
| NVIDIA Jetson Orin            | `cortex-a78ae`                   |
| AWS Graviton 2                | `neoverse-n1`                    |
| AWS Graviton 3                | `neoverse-v1`                    |
| Apple M1 / M2 (Asahi Linux)   | `apple-m1` (gcc 14+ / clang 15+) |

Prefer `-mcpu=<soc>` over `-march=<isa>` on a known target — gcc /
clang derive a matching `-mtune` automatically and pick up SoC-
specific scheduling.  Mix in `PROMEKI_TARGET_ARCH` only when you
need an ISA extension the chosen `-mcpu` doesn't already imply
(e.g. `armv8.2-a+fp16` on a part with the optional half-precision
extension that the generic `-mcpu` profile leaves off).

The result of the tuning lands in `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS`
and is visible in `compile_commands.json` — useful for confirming
the flags reach every TU.

---

## Cleaning {#building_cleaning}

Two clean targets are provided:

`libclean` removes only the promeki library objects, test
executables, and utilities while preserving the third-party build
output. This is the right target for routine development — the
vendored dependencies rarely change and rebuilding them is slow:

```sh
cmake --build build --target libclean
```

The standard `clean` target removes everything, including the
third-party builds:

```sh
cmake --build build --target clean
```

---

## Running Tests {#building_tests}

Unit-test executables are configured but **not** part of the default
`cmake --build build` run — this keeps incremental rebuilds fast when
you're iterating on something unrelated to tests.  Two umbrella
targets give you the test build + run flow:

```sh
# Build all three test executables, no run:
cmake --build build --target tests

# Build them and execute the full ctest suite serially:
cmake --build build --target check
```

Equivalently, with the repo's `build` helper:

```sh
build tests   # build only
build check   # build + run
```

Or run them individually after building:

```sh
./build/bin/unittest-promeki
./build/bin/unittest-tui
./build/bin/unittest-sdl
```

All three are built on doctest. Per-test filtering is available via
doctest's `--test-case=` filter, e.g.
`./build/bin/unittest-promeki --test-case=String*`.

---

## Code Formatting {#building_format}

Every owned source file under `src/`, `include/promeki/`,
`tests/{unit,func}/`, `demos/`, and `utils/` is formatted with
`clang-format` against the `.clang-format` rules at the repo root.
Two CMake targets cover the lifecycle:

```sh
cmake --build build --target format-tree         # rewrite in place
cmake --build build --target format-check-tree   # dry-run, errors on diff
```

`format-check-tree` is the gate run by `scripts/precommit.sh`.  The
`-tree` suffix distinguishes these from the vendored libvtc / cirf
submodules, which expose their own `format` targets that cover only
their own subdirectories.  Vendored code under `thirdparty/` is
intentionally not reformatted by these targets.

---

## Pre-commit Verification {#building_precommit}

`scripts/precommit.sh` runs the full set of checks every commit
should pass before going in: format conformance, a fresh
configure-from-scratch with all auto-detected modules and
warnings-as-errors, the full default build, the unit-test suite, and
(when Doxygen is on `PATH`) a Doxygen build.  By default the build
directory is `build-precommit-<timestamp>` at the repo root, and it
is cleaned up on success.

```sh
scripts/precommit.sh                 # run all checks
scripts/precommit.sh --keep          # keep the build dir on success
scripts/precommit.sh --no-docs       # skip the Doxygen step
scripts/precommit.sh --build-dir DIR # use DIR instead of the default
```

Template-heavy C++ TUs in this codebase budget at roughly 1.5 GB of
resident memory per job, so the safe ceiling is `mem_avail / 1.5 GB`
parallel jobs and two simultaneous precommit runs will usually
overshoot that.  The script holds a non-blocking flock on
`.precommit.lock` at the repo root, so a second precommit invocation
while one is running exits immediately with a clear error rather
than queueing up.  The script exits non-zero on the first failed
step and prints a clear summary at the end.  See `CONTRIBUTING.md`
at the repository root for the full development workflow.

---

## Installing {#building_installing}

```sh
cmake --install build
# or to a custom prefix:
cmake --install build --prefix /opt/promeki
```

The install tree contains:

- `lib/libpromeki.so` (versioned, with SONAME), plus
  `libpromeki-tui.so` and `libpromeki-sdl.so` when enabled
- `include/promeki/` — all public headers, including the generated
  `config.h` that records which features were compiled in
- `include/promeki/thirdparty/` — bundled third-party headers (when
  the corresponding dependency was vendored)
- `lib/cmake/promeki/` — CMake package-config files for
  `find_package(promeki)`
- `share/doc/promeki/` — license and third-party attribution notices

---

## Using libpromeki in Your Project {#building_downstream}

After installing, add this to your project's `CMakeLists.txt`:

```cmake
find_package(promeki REQUIRED)

# Link against the main library:
target_link_libraries(myapp PRIVATE promeki::promeki)

# For TUI applications:
target_link_libraries(myapp PRIVATE promeki::tui)

# For SDL applications:
target_link_libraries(myapp PRIVATE promeki::sdl)
```

If you installed to a non-standard prefix, tell CMake where to find
it:

```sh
cmake -B build -DCMAKE_PREFIX_PATH=/opt/promeki
```

### Include Conventions {#building_include_conventions}

All promeki headers live under the `promeki/` namespace:

```cpp
#include <promeki/string.h>
#include <promeki/timecode.h>
#include <promeki/imagedesc.h>
```

Bundled third-party headers use their canonical include paths:

```cpp
#include <nlohmann/json.hpp>
#include <spng.h>
#include <ft2build.h>
```

Everything in promeki is in the `promeki` namespace:

```cpp
using namespace promeki;

String name("hello");
Timecode tc(1, 2, 3, 4, FrameRate::fps24());
UUID id = UUID::generate();
```

---

## Building the Documentation {#building_docs}

The documentation you are reading is generated from the sources by
Doxygen. To build it locally:

```sh
cmake -B build -DPROMEKI_BUILD_DOCS=ON
cmake --build build --target docs
```

The HTML output is written to `build/doxygen/html/`; open
`build/doxygen/html/index.html` in a browser. A hosted copy of the
`main` branch is available at
<https://jthwho.github.io/libpromeki/main/>.

The output directory can be overridden with
`-DPROMEKI_DOCS_OUTPUT_DIR=/some/path` at configure time.

---

## See Also {#building_see_also}

- @ref debugging "Debugging and Diagnostics" &mdash; build-type /
  debug-logging / crash-handler reference
- @ref nvenc "NVENC setup" &mdash; NVENC / NVDEC SDK setup and
  `PROMEKI_NVENC_SDK_DIR`
- @ref ndi "NDI setup" &mdash; NDI SDK setup and `PROMEKI_NDI_SDK_DIR`
