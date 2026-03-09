# libpromeki
<img src="./docs/promeki_logo.jpg" alt="ProMEKI Logo" width="100" height="100">

PROfessional MEdia toolKIt — A modern C++20 library that makes writing
audio, video, and media applications in C++ easier.

libpromeki provides the foundational building blocks that professional
media software needs: timecode, frame rates, image and audio descriptors,
pixel formats, color spaces, memory management, signal/slot event systems,
and more.  It is designed so that you can focus on your application logic
instead of reinventing infrastructure.

## Library Architecture

libpromeki is split into two shared libraries:

**promeki-core** — General-purpose C++ utilities with no media-specific
dependencies.  Includes strings with copy-on-write, shared pointers,
points/matrices, date/time, timecode, UUID, logging, JSON, signals/slots,
an object system, command-line parsing, file I/O, memory pools, and a
unit test framework.

**promeki-proav** — Professional audio/video classes built on top of
promeki-core.  Includes image and audio descriptors, pixel format
registry, image file I/O (PNG, JPEG), audio file I/O (WAV, AIFF, OGG),
font painting, color space conversion, codecs, and frame/video
descriptors.

You can use promeki-core on its own for non-media projects, or link
against promeki-proav to get the full media toolkit.

## No Dependency Hell

All third-party libraries that promeki-proav depends on (zlib, libpng,
FreeType, libjpeg-turbo, libsndfile) are vendored as git submodules,
built as static libraries with `-fPIC`, and absorbed directly into
`libpromeki-proav.so`.  Their headers are installed under
`promeki/thirdparty/` to avoid collisions with system versions.

When you install libpromeki, you get two shared libraries and a set of
headers.  Your application links against `promeki::core` and/or
`promeki::proav` — no chasing down system packages, no version
mismatches, no transitive dependency surprises.

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

| Option | Default | Description |
|--------|---------|-------------|
| `PROMEKI_BUILD_PROAV` | `ON` | Build the promeki-proav media library |
| `PROMEKI_BUILD_TESTS` | `ON` | Build unit tests |
| `PROMEKI_BUILD_DOCS` | `OFF` | Build Doxygen API documentation |

To build only the core library without the media components:

```sh
cmake -B build -DPROMEKI_BUILD_PROAV=OFF
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
- `lib/libpromeki-core.so` and `lib/libpromeki-proav.so` (versioned, with SONAME)
- `include/promeki/` — all public headers
- `include/promeki/thirdparty/` — bundled third-party headers
- `lib/cmake/promeki/` — CMake package config files
- `share/doc/promeki/` — license and third-party notices

## Using libpromeki in Your Project

After installing, add this to your project's `CMakeLists.txt`:

```cmake
find_package(promeki REQUIRED)

# For general-purpose utilities only:
target_link_libraries(myapp PRIVATE promeki::core)

# For media/audio/video functionality (includes core):
target_link_libraries(myapp PRIVATE promeki::proav)
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

Bundled third-party headers are under `promeki/thirdparty/`:

```cpp
#include <promeki/thirdparty/png.h>
#include <promeki/thirdparty/freetype2/ft2build.h>
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
[API Documentation](https://howardlogic.com/doxygen/libpromeki)

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
