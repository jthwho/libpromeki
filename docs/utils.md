# Utility Applications {#utils}

Utility applications that provide functional use beyond the library.

The `utils/` directory contains a suite of standalone utility applications
that use the promeki library. Unlike the [demonstration applications](demos.md)
(which exist to showcase library APIs), utilities are meant to have
functional use beyond the library itself. They serve two purposes:

- **Practical tooling** — Each utility provides a useful function built on
  top of the promeki library, such as querying build information.

- **Functional testing** — While the unit tests (in `tests/`) exercise
  individual classes in isolation, the utilities exercise the library as a
  whole, in the same way an actual client application would. This helps
  catch integration-level issues that unit tests may miss.

## Building {#utils_building}

Utilities are built by default as part of the normal library build.
The option `PROMEKI_BUILD_UTILS` (default `ON`) controls this:

```sh
cmake -DPROMEKI_BUILD_UTILS=OFF ..   # skip building utilities
```

When built, the utility executables are placed in the `bin/` directory of
the build tree alongside the unit test executables. They are also installed
as part of `make install`.

## Standalone Build {#utils_standalone}

The `utils/` directory has its own top-level `CMakeLists.txt` and can be
built independently against an installed copy of libpromeki, just like any
external project:

```sh
cd utils
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/path/to/promeki/install ..
make
```

This is exactly the workflow that any third-party consumer of the library
would follow, making the utilities a living reference for integration.

## Adding a New Utility {#utils_adding}

Each utility lives in its own subdirectory under `utils/`:

```
utils/
  CMakeLists.txt          # top-level, includes each subdirectory
  promeki-info/
    CMakeLists.txt         # add_executable + target_link_libraries
    main.cpp
  my-new-util/
    CMakeLists.txt
    main.cpp
```

To add a new utility:

1. Create a new subdirectory under `utils/` (e.g. `utils/my-new-util/`).
2. Add a `CMakeLists.txt` that creates the executable and links against the
   appropriate promeki target (`promeki::promeki`, or `promeki::tui` /
   `promeki::sdl` for UI libraries).
3. Include an `install(TARGETS ...)` rule so it gets installed.
4. Add an `add_subdirectory()` call in `utils/CMakeLists.txt`.

## Current Utilities {#utils_list}

| Utility        | Library          | Description                                                  |
|----------------|------------------|--------------------------------------------------------------|
| promeki-info   | promeki::promeki | Prints library build information                             |
| imgtest        | promeki::promeki | Exercises image format load/save round-trips                 |
| promeki-bench  | promeki::promeki | Unified benchmark driver; CSC pair matrix plus future suites |
| mediaplay      | promeki::sdl     | Config-driven MediaIO playback tool                          |
