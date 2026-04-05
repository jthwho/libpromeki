# PROfessional MEdia toolKIt
<img src="docs/promeki_logo.jpg" alt="ProMEKI Logo" width="100">

A modern C++20 library that makes writing audio, video, and media applications in C++ easier.

libpromeki provides the foundational building blocks that professional
media software needs: timecode, frame rates, image and audio descriptors,
pixel formats, color spaces, memory management, signal/slot event systems,
and more.  It is designed so that you can focus on your application logic
instead of reinventing infrastructure.

## Library Architecture

libpromeki is organized into one main shared library and two optional
UI libraries:

**promeki** — The core library containing general-purpose C++ utilities,
networking, professional audio/video classes, and music/MIDI support.
Includes strings with copy-on-write, shared pointers, containers
(List, Map, Set, HashMap), concurrency primitives (Mutex, ThreadPool,
Queue, Future/Promise), an event loop with signals/slots, points/matrices,
date/time, timecode, frame rates, UUID, logging, JSON, an object system,
command-line parsing, file I/O, typed serialization streams (DataStream,
TextStream), memory spaces, TCP/UDP/raw sockets, RTP/SDP, color models
and color science, pixel format/descriptor registries, image and audio
descriptors, image file I/O (PNG, JPEG), audio file I/O (WAV, AIFF, OGG),
font rendering (cached and alpha-compositing), codecs, a media processing
pipeline (MediaNode, MediaPipeline), test pattern generation, LTC
encoding/decoding, frame/video descriptors, MIDI notes, musical scales,
and a note-sequence parser.

**promeki-tui** — Terminal UI widget library (optional).

**promeki-sdl** — SDL3-based GUI library (optional, off by default).

Individual feature areas (networking, pro A/V, music, image formats,
font rendering, audio file I/O) can be independently enabled or disabled
via CMake feature flags.  Each external dependency can be sourced from
the vendored submodules or from system-installed packages.

## No Dependency Hell

All third-party libraries that promeki depends on (zlib, libpng,
FreeType, libjpeg-turbo, libsndfile) are vendored as git submodules,
built as static libraries with `-fPIC`, and absorbed directly into
`libpromeki.so`.  Alternatively, each dependency can be switched to use
system-installed versions via `PROMEKI_USE_SYSTEM_*` CMake options.

When you install libpromeki, you get a shared library and a set of
headers.  Your application links against `promeki::promeki` — no
chasing down system packages, no version mismatches, no transitive
dependency surprises.

## History

The bulk of the code that makes up libpromeki started out life as code
that existed across multiple libraries originally developed by my (now
defunct) company, [SpectSoft](https://en.wikipedia.org/wiki/SpectSoft),
for our video disk recorder product called RaveHD.  During its run, it
was used in post production, VFX, animation, and other media related
sectors.  At that time, the entire application stack was built on top of
the [Qt](https://www.qt.io/) framework.  The Qt design patterns,
aesthetic, and general way of doing things has certainly informed much
of how this library was architected.

What you see now is a library that takes the good bits from that old
code base and has removed the Qt coupling and replaced it with modern
C++ native STL.  Hopefully you'll find it as useful as I have over the
years.  --jth

## Building

Requirements: a C++20 compiler, CMake 3.22+, and git.

```sh
git clone --recurse-submodules https://github.com/user/libpromeki.git
cd libpromeki
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### CMake Build Options

**Feature flags** (control what goes into `libpromeki.so`):

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_ENABLE_NETWORK` | `ON` | Include networking support (sockets, RTP, SDP) |
| `PROMEKI_ENABLE_PROAV` | `ON` | Include pro A/V support (image, audio, pipeline) |
| `PROMEKI_ENABLE_MUSIC` | `ON` | Include music support (MIDI, scales) |
| `PROMEKI_ENABLE_PNG` | `ON` | Include PNG image I/O |
| `PROMEKI_ENABLE_JPEG` | `ON` | Include JPEG codec |
| `PROMEKI_ENABLE_FREETYPE` | `ON` | Include FreeType font rendering |
| `PROMEKI_ENABLE_AUDIO` | `ON` | Include audio file I/O (libsndfile) |

**Dependency source** (vendored submodule or system package):

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_USE_SYSTEM_ZLIB` | `OFF` | Use system zlib |
| `PROMEKI_USE_SYSTEM_LIBPNG` | `OFF` | Use system libpng |
| `PROMEKI_USE_SYSTEM_LIBJPEG` | `OFF` | Use system libjpeg-turbo |
| `PROMEKI_USE_SYSTEM_FREETYPE` | `OFF` | Use system FreeType |
| `PROMEKI_USE_SYSTEM_SNDFILE` | `OFF` | Use system libsndfile |
| `PROMEKI_USE_SYSTEM_NLOHMANN_JSON` | `OFF` | Use system nlohmann-json |
| `PROMEKI_USE_SYSTEM_VTC` | `OFF` | Use system libvtc |

**Build targets:**

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_BUILD_TUI` | `ON` | Build the promeki-tui library |
| `PROMEKI_BUILD_SDL` | `OFF` | Build the promeki-sdl library |
| `PROMEKI_BUILD_TESTS` | `ON` | Build unit tests |
| `PROMEKI_BUILD_UTILS` | `ON` | Build utility applications |
| `PROMEKI_BUILD_DEMOS` | `ON` | Build demonstration applications |
| `PROMEKI_BUILD_DOCS` | `OFF` | Build Doxygen API documentation |

To build a minimal core-only library:

```sh
cmake -B build -DPROMEKI_ENABLE_PROAV=OFF -DPROMEKI_ENABLE_NETWORK=OFF -DPROMEKI_ENABLE_MUSIC=OFF
```

To build using system-installed dependencies:

```sh
cmake -B build -DPROMEKI_USE_SYSTEM_ZLIB=ON -DPROMEKI_USE_SYSTEM_LIBPNG=ON \
    -DPROMEKI_USE_SYSTEM_FREETYPE=ON -DPROMEKI_USE_SYSTEM_SNDFILE=ON \
    -DPROMEKI_USE_SYSTEM_LIBJPEG=ON
```

### Cleaning

The `libclean` target removes only the promeki library objects, test
executables, and utilities while preserving third-party builds.  This
avoids rebuilding vendored dependencies (zlib, libpng, FreeType, etc.)
which rarely change:

```sh
cmake --build build --target libclean
```

To clean everything including third-party builds, use the standard CMake
`clean` target:

```sh
cmake --build build --target clean
```

### Running Tests

```sh
cd build
ctest --output-on-failure
```

### Installing

```sh
cmake --install build
# or to a custom prefix:
cmake --install build --prefix /opt/promeki
```

This installs:
- `lib/libpromeki.so` (versioned, with SONAME), plus `libpromeki-tui.so` and `libpromeki-sdl.so` if enabled
- `include/promeki/` — all public headers (including generated `config.h`)
- `include/promeki/thirdparty/` — bundled third-party headers (when using vendored deps)
- `lib/cmake/promeki/` — CMake package config files
- `share/doc/promeki/` — license and third-party notices

## Using libpromeki in Your Project

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

If you installed to a non-standard prefix, tell CMake where to find it:

```sh
cmake -B build -DCMAKE_PREFIX_PATH=/opt/promeki
```

### Include Conventions

All promeki headers live under the `promeki/` namespace:

```cpp
#include <promeki/string.h>
#include <promeki/timecode.h>
#include <promeki/imagedesc.h>
```

Bundled third-party headers use their canonical include paths:

```cpp
#include <nlohmann/json.hpp>
#include <png.h>
#include <ft2build.h>
```

Everything in promeki is in the `promeki` namespace:

```cpp
using namespace promeki;

String name("hello");
Timecode tc(1, 2, 3, 4, FrameRate::fps24());
UUID id = UUID::generate();
```

## Documentation

Build the API documentation (requires Doxygen):

```sh
cmake -B build -DPROMEKI_BUILD_DOCS=ON
cmake --build build --target docs
```

A hosted copy of the documentation is available at:
[API Documentation](https://jthwho.github.io/libpromeki/main/)

## Debugging

If you build the library in Debug mode (`-DCMAKE_BUILD_TYPE=Debug`),
the `promekiDebug()` logging function is enabled.  Each source file
that wants to use it must register a debug channel:

```cpp
PROMEKI_DEBUG(MyChannel)

// elsewhere in your code
promekiDebug("This is a debug log entry")
```

Then enable channels at runtime via the `PROMEKI_DEBUG` environment
variable (comma-separated):

```sh
export PROMEKI_DEBUG=MyChannel,AnotherChannel
./yourapp
```

## License

libpromeki is licensed under the [MIT License](LICENSE).

Third-party library licenses and attribution notices are documented in
[THIRD-PARTY-LICENSES](THIRD-PARTY-LICENSES).
