# PROfessional MEdia toolKIt
<img src="docs/promeki_logo.jpg" alt="ProMEKI Logo" width="100">

A modern C++20 library for building professional audio, video, and
media applications.

> **Status: Work in Progress** — libpromeki is under active development.
> The core APIs, pro A/V subsystems, and networking stack are functional
> and in daily use, but interfaces may change without notice. Bug reports
> and feedback are welcome.

libpromeki provides the foundational building blocks that professional
media software needs: timecode, frame rates, image and audio
descriptors, pixel formats, color science, networking, memory
management, signal/slot event systems, and more. It is designed so
that you can focus on your application logic instead of reinventing
infrastructure.

**[API Documentation](https://jthwho.github.io/libpromeki/main/)**

## Feature Overview

### Core Library

General-purpose C++ infrastructure that the rest of the library is
built on.

- **Strings** — encoding-aware strings with copy-on-write semantics
- **Containers** — `List`, `Map`, `Set`, `HashMap`, `Deque`, `Queue`,
  `Stack`, `PriorityQueue`, `Array`
- **Smart pointers & memory** — `SharedPtr`, `Buffer` (reference-counted
  memory blocks), `MemPool`, `BufferPool`, `SecureMem`
- **I/O & serialization** — `DataStream` (binary), `TextStream`,
  `AnsiStream` (ANSI terminal), and a family of I/O devices (`File`,
  `BufferedIODevice`, `BufferIODevice`, `StringIODevice`)
- **File system** — `File`, `Dir`, `FileInfo`, `FilePath`, plus a
  compiled-in resource filesystem (CIRF) for embedding assets
- **Concurrency** — `Thread` (with built-in event loop), `Mutex`,
  `ReadWriteLock`, `WaitCondition`, `Atomic`, `Future`/`Promise`,
  `ThreadPool`, `Strand`
- **Event system** — Qt-style `ObjectBase` with parent/child hierarchy,
  `Signal`/`Slot` connections (including cross-thread dispatch),
  `EventLoop`, timers
- **Time & date** — `DateTime`, `Timestamp`, `Duration`, `ElapsedTimer`
- **JSON** — `JsonObject` / `JsonArray` via nlohmann/json
- **Hashing** — MD5, SHA-1, FNV-1a
- **Utilities** — `UUID`, `FourCC`, `Enum` (reflection), `Random`,
  `CommandLineParser`, `Logger`, `Process`, `Benchmark`,
  `VariantDatabase`, `ConfigOption`, `Histogram`
- **Math & geometry** — `Matrix`, `Point`, `Size2D`, `Rect`, `Line`,
  `Rational`

### Professional Audio / Video

Classes for working with professional media formats, images, audio,
color science, and timecode.

**Descriptors & formats:**
- `ImageDesc` — image dimensions, planes, and pixel layout
- `AudioDesc` — sample type, rate, and channel layout
- `MediaDesc` — combined frame rate, image layers, audio groups, and
  metadata for a complete media stream
- `PixelFormat` / `PixelDesc` — extensive pixel format registry with
  78 pixel formats and 132 pixel descriptors covering interleaved,
  planar, and semi-planar layouts (RGB, YCbCr, Bayer, DPX-packed, and
  more)

**Image processing:**
- `Image` — raster image container with multi-plane support
- Image file I/O for PNG, JPEG, Cineon (CIN), DPX, PNM, SGI, TGA, and
  raw YUV
- JPEG XS encode/decode (via SVT-JPEG-XS, x86-64)
- `PaintEngine` — software rasterization for interleaved and planar
  pixel formats
- `Font` / `FastFont` — FreeType-based font rendering with glyph caching
  and alpha compositing

**Audio processing:**
- `Audio` / `AudioBuffer` — audio sample containers
- `AudioFile` — audio file I/O (WAV, AIFF, OGG via libsndfile)
- `AudioGen` — signal generators (sine, square, sawtooth, noise, etc.)
- `AudioLevel` — level measurement
- `AudioTestPattern` — standard test audio patterns
- `LtcEncoder` / `LtcDecoder` — Linear Timecode (LTC) encode and
  decode on audio streams

**Color science:**
- `Color` — color values with multiple color model support
- `ColorModel` — color model descriptors (sRGB, Rec. 709, Rec. 2020,
  DCI-P3, CIE XYZ, and more)
- Color space conversion pipeline with SIMD acceleration via the
  Highway library: transfer function, matrix, chroma, alpha, pack/unpack
  stages

**Timecode:**
- `Timecode` — SMPTE and non-standard timecode (backed by libvtc)
- `TimecodeGenerator` — frame-accurate timecode generation
- `FrameRate` — frame rate descriptors with drop-frame support

**Video & frames:**
- `Frame` — unified image + audio + metadata at a single timestamp
- `VideoTestPattern` — color bars, grids, ramps, and other standard test
  patterns
- `ImgSeq` — image sequence handling with frame range and naming patterns
- `Metadata` — typed key-value metadata container

**Media I/O framework:**

An extensible pipeline for reading, writing, and transforming media,
built around `MediaIO` (registry/factory) and `MediaIOTask` backends:

- **TPG** — test pattern generator (video and audio)
- **ImageFile** — image file and image sequence I/O
- **AudioFile** — audio file I/O
- **QuickTime** — MOV/MP4 read and write
- **RTP** — network streaming via RTP/SDP
- **Converter** — pixel format and color space conversion
- Custom backends are registered with `PROMEKI_REGISTER_MEDIAIO()`

### Networking

BSD-socket abstractions and real-time media transport protocols.

- **Sockets** — `TcpSocket`, `TcpServer`, `UdpSocket`, `RawSocket`,
  `PrioritySocket`, `LoopbackTransport`
- **Addresses** — `SocketAddress`, `NetworkAddress`, `IPv4Address`,
  `IPv6Address`, `MacAddress`
- **RTP/SDP** — `RtpSession`, `RtpPacket`, `RtpPayload` (RFC 3550);
  `SdpSession`, `SdpMediaDescription`
- **Transport** — `PacketTransport`, `UdpSocketTransport`,
  `MulticastManager`, `MulticastReceiver`

### Music & MIDI

- `MidiNote` / `MidiNoteNames` — MIDI note representation and lookup
- `MusicalNote` — note with frequency and pitch
- `MusicalScale` — scale descriptors (major, minor, modes, custom)
- `NoteSequenceParser` — parse note strings (e.g. `"C4 D4 E4 C5"`)

### Terminal UI (promeki-tui)

An optional shared library providing a terminal widget toolkit.

- **Application & rendering** — `TuiApplication`, `TuiScreen`,
  `TuiPainter`, `TuiPalette`
- **Widgets** — `Label`, `LineEdit`, `TextArea`, `Button`, `CheckBox`,
  `ProgressBar`, `ListView`, `Frame`, `ScrollArea`, `Splitter`,
  `TabWidget`, `StatusBar`, `Menu`
- **Input** — keyboard and mouse event parsing

### SDL Graphics (promeki-sdl)

An optional shared library for SDL3-based applications (off by default).

- `SDLApplication`, `SDLWindow`, `SDLVideoWidget`, `SDLAudioOutput`
- `SDLPlayer` — MediaIO-backed player with video display and audio
  output

## Library Architecture

libpromeki is organized into one main shared library and two optional
UI libraries:

**promeki** — the core library. Networking, pro A/V, music, and other
feature areas can be independently enabled or disabled via CMake feature
flags.

**promeki-tui** — terminal UI widget library (optional).

**promeki-sdl** — SDL3-based GUI library (optional, off by default).

## No Dependency Hell

All third-party libraries that promeki depends on are vendored as git
submodules, built as static libraries with `-fPIC`, and absorbed
directly into `libpromeki.so`. Alternatively, each dependency can be
switched to use system-installed versions via `PROMEKI_USE_SYSTEM_*`
CMake options.

When you install libpromeki, you get a shared library and a set of
headers. Your application links against `promeki::promeki` — no
chasing down system packages, no version mismatches, no transitive
dependency surprises.

Vendored dependencies: zlib-ng, libspng, libjpeg-turbo, FreeType,
libsndfile, nlohmann/json, libvtc, SVT-JPEG-XS, Highway, CIRF.

## Included Utilities

| Program | Description |
|---------|-------------|
| `promeki-info` | Print library build information (version, date, build type) |
| `mediaplay` | CLI media player and pipeline builder with --in/--out/--converter stages |
| `cscbench` | Color space conversion benchmark with JSON output |
| `imgtest` | Image file I/O test utility |

## Building

Requirements: a C++20 compiler, CMake 3.22+, and git.  Some vendored
libraries (libjpeg-turbo, SVT-JPEG-XS) use hand-written SIMD assembly
and will build their accelerated code paths when `nasm` or `yasm` is
available.  They still build without an assembler, but without the
SIMD fast paths.

```sh
git clone --recurse-submodules https://github.com/jthwho/libpromeki.git
cd libpromeki
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### CMake Build Options

**Feature flags** (control what goes into `libpromeki.so`):

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_ENABLE_NETWORK` | `ON` | Networking (sockets, RTP, SDP) |
| `PROMEKI_ENABLE_PROAV` | `ON` | Pro A/V (image, audio, color, pipeline) |
| `PROMEKI_ENABLE_MUSIC` | `ON` | Music/MIDI support |
| `PROMEKI_ENABLE_PNG` | `ON` | PNG image I/O (libspng) |
| `PROMEKI_ENABLE_JPEG` | `ON` | JPEG codec |
| `PROMEKI_ENABLE_JPEGXS` | `OFF`* | JPEG XS codec (*auto-enabled on x86-64 with nasm/yasm) |
| `PROMEKI_ENABLE_FREETYPE` | `ON` | FreeType font rendering |
| `PROMEKI_ENABLE_AUDIO` | `ON` | Audio file I/O (libsndfile) |
| `PROMEKI_ENABLE_CSC` | `ON` | SIMD color space conversion (Highway) |
| `PROMEKI_ENABLE_CIRF` | `ON` | Compiled-in resource filesystem |

**Dependency source** (vendored submodule or system package):

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_USE_SYSTEM_ZLIB` | `OFF` | Use system zlib |
| `PROMEKI_USE_SYSTEM_LIBSPNG` | `OFF` | Use system libspng |
| `PROMEKI_USE_SYSTEM_LIBJPEG` | `OFF` | Use system libjpeg-turbo |
| `PROMEKI_USE_SYSTEM_SVT_JPEG_XS` | `OFF` | Use system SVT-JPEG-XS |
| `PROMEKI_USE_SYSTEM_FREETYPE` | `OFF` | Use system FreeType |
| `PROMEKI_USE_SYSTEM_SNDFILE` | `OFF` | Use system libsndfile |
| `PROMEKI_USE_SYSTEM_NLOHMANN_JSON` | `OFF` | Use system nlohmann-json |
| `PROMEKI_USE_SYSTEM_VTC` | `OFF` | Use system libvtc |
| `PROMEKI_USE_SYSTEM_HIGHWAY` | `OFF` | Use system Highway |
| `PROMEKI_USE_SYSTEM_CIRF` | `OFF` | Use system CIRF |

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
cmake -B build -DPROMEKI_USE_SYSTEM_ZLIB=ON -DPROMEKI_USE_SYSTEM_LIBSPNG=ON \
    -DPROMEKI_USE_SYSTEM_FREETYPE=ON -DPROMEKI_USE_SYSTEM_SNDFILE=ON \
    -DPROMEKI_USE_SYSTEM_LIBJPEG=ON
```

### Cleaning

The `libclean` target removes only the promeki library objects, test
executables, and utilities while preserving third-party builds. This
avoids rebuilding vendored dependencies which rarely change:

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

## Building the Documentation

To build the API documentation locally (requires Doxygen):

```sh
cmake -B build -DPROMEKI_BUILD_DOCS=ON
cmake --build build --target docs
```

## Debugging

If you build the library in Debug mode (`-DCMAKE_BUILD_TYPE=Debug`),
the `promekiDebug()` logging function is enabled. Each source file
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

## History

The bulk of the code that makes up libpromeki started out life as code
that existed across multiple libraries originally developed by my (now
defunct) company, [SpectSoft](https://en.wikipedia.org/wiki/SpectSoft),
for our video disk recorder product called RaveHD. During its run, it
was used in post production, VFX, animation, and other media related
sectors. At that time, the entire application stack was built on top of
the [Qt](https://www.qt.io/) framework. The Qt design patterns,
aesthetic, and general way of doing things has certainly informed much
of how this library was architected.

What you see now is a library that takes the good bits from that old
code base and has removed the Qt coupling and replaced it with modern
C++ native STL. Hopefully you'll find it as useful as I have over the
years. --jth

## License

libpromeki is licensed under the [MIT License](LICENSE).

Third-party library licenses and attribution notices are documented in
[THIRD-PARTY-LICENSES](THIRD-PARTY-LICENSES).
