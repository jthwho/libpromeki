# Demonstration Applications {#demos}

Applications that demonstrate specific aspects of the promeki library.

The `demos/` directory contains standalone demonstration applications that
showcase the capabilities and usage patterns of the promeki library. Unlike
the [utility applications](utils.md) (which provide functional use beyond
the library itself), demos exist purely to help users learn and understand
specific library APIs.

## Building {#demos_building}

Demos are built by default as part of the normal library build.
The option `PROMEKI_BUILD_DEMOS` (default `ON`) controls this:

```sh
cmake -DPROMEKI_BUILD_DEMOS=OFF ..   # skip building demos
```

When built, the demo executables are placed in the `bin/` directory of
the build tree. They are also installed as part of `make install`.

## Standalone Build {#demos_standalone}

The `demos/` directory has its own top-level `CMakeLists.txt` and can be
built independently against an installed copy of libpromeki, just like any
external project:

```sh
cd demos
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/path/to/promeki/install ..
make
```

This is exactly the workflow that any third-party consumer of the library
would follow, making the demos a living reference for integration.

## Adding a New Demo {#demos_adding}

Each demo lives in its own subdirectory under `demos/`:

```
demos/
  CMakeLists.txt          # top-level, includes each subdirectory
  tui-demo/
    CMakeLists.txt         # add_executable + target_link_libraries
    main.cpp
  my-new-demo/
    CMakeLists.txt
    main.cpp
```

To add a new demo:

1. Create a new subdirectory under `demos/` (e.g. `demos/my-new-demo/`).
2. Add a `CMakeLists.txt` that creates the executable and links against the
   appropriate promeki target (`promeki::promeki`, or `promeki::tui` /
   `promeki::sdl` for UI libraries).
3. Include an `install(TARGETS ...)` rule so it gets installed.
4. Add an `add_subdirectory()` call in `demos/CMakeLists.txt`.

## Current Demos {#demos_list}

| Demo            | Library       | Description                                    |
|-----------------|---------------|------------------------------------------------|
| tui-demo        | promeki::tui  | Interactive showcase of TUI widgets and layout |
| tui-event-test  | promeki::tui  | Event loop and input event visualization       |
