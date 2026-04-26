# Building and Installing libpromeki {#building}

Complete build, install, and downstream-integration guide.

This page is the canonical reference for building libpromeki from
source, configuring its feature flags, running its tests, installing
it, and consuming it from a downstream CMake project. Every
user-facing CMake option is documented here.

## Requirements {#building_requirements}

- A C++20 compiler (GCC 11+ or Clang 14+ recommended)
- CMake 3.22 or newer
- `git` (required — the build clones vendored dependencies via
  submodules)
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
| NVIDIA Video Codec SDK | Enables NVENC / NVDEC — see [NVENC setup](nvenc.md) |

---

## Quick Start {#building_quickstart}

```sh
git clone --recurse-submodules https://github.com/jthwho/libpromeki.git
cd libpromeki
cmake -B build
cmake --build build -j$(nproc)
```

This produces `libpromeki.so`, `libpromeki-tui.so`, the unit-test
binaries, the demos, and the utility programs under `build/` using
the default **DevRelease** build type.

---

## Build Types {#building_build_types}

libpromeki recognises three CMake build types. The build type
controls optimisation, debug symbols, and whether per-module debug
logging (`promekiDebug()`) is compiled in. See [Debugging and
Diagnostics](debugging.md) for the full discussion; the short
version is:

| Build type | Optimisation | Debug symbols | `promekiDebug()` | Typical use |
|------------|--------------|---------------|------------------|-------------|
| **Debug** | `-O0` | Yes | Compiled in | Step-through debugging, sanitizers |
| **DevRelease** (default) | `-O3` | Yes (`-g`) | Compiled in | Day-to-day development, CI, profiling |
| **Release** | `-O3` | No | Compiled out | Distribution / final packaging |

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

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
| `PROMEKI_ENABLE_NVENC` | auto | NVIDIA NVENC H.264 / HEVC encoder — see [NVENC setup](nvenc.md) |
| `PROMEKI_ENABLE_NVDEC` | auto | NVIDIA NVDEC H.264 / HEVC decoder — see [NVENC setup](nvenc.md) |

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

## Common Configurations {#building_configurations}

### Minimal core-only build {#building_config_minimal}

Drops Pro A/V, networking, and music for a lean core-only library
suitable for command-line tools that just need the container / I/O /
string infrastructure:

```sh
cmake -B build \
    -DPROMEKI_ENABLE_PROAV=OFF \
    -DPROMEKI_ENABLE_NETWORK=OFF \
    -DPROMEKI_ENABLE_MUSIC=OFF
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

### Release packaging {#building_config_release}

Full optimisation, no debug symbols, no `promekiDebug()` overhead:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build --prefix /opt/promeki
```

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
step and prints a clear summary at the end.  See
[CONTRIBUTING.md](../CONTRIBUTING.md) for the full development
workflow.

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

- [Debugging and Diagnostics](debugging.md) — build-type /
  debug-logging / crash-handler reference
- [NVENC setup](nvenc.md) — NVENC / NVDEC SDK setup and
  `PROMEKI_NVENC_SDK_DIR`
