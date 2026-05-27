# Downstream consumer example

A self-contained example showing how an application links against
libpromeki. It is not built by the libpromeki tree itself — point CMake
at this directory directly.

## find_package (recommended for packaged / cross / Yocto builds)

Consumes an already-built-and-installed libpromeki:

```sh
cmake -S . -B build                       # add -DCMAKE_PREFIX_PATH=/opt/promeki if installed there
cmake --build build
./build/promeki_example
```

The library's feature set is fixed when it is built; the application
just links the imported `promeki::promeki` target.

## add_subdirectory (build libpromeki from source, choose its features)

Use this when the application needs to control which libpromeki features
are compiled in:

```sh
cmake -S . -B build -DEXAMPLE_USE_SUBDIR=ON -DPROMEKI_SOURCE_DIR=/path/to/libpromeki
cmake --build build
```

The `CMakeLists.txt` documents the four rules for setting libpromeki's
feature flags from the parent build (set them *before*
`add_subdirectory`, a plain `set()` suffices, prefer a `cmake/configs/`
preset, and control size via the build type rather than per-target
compile options).

See `docs/building.md` → "Using libpromeki in Your Project" for the full
discussion.
