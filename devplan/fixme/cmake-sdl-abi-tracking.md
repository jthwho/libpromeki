# CMake: incremental builds miss header layout changes across shared libraries

**File:** `CMakeLists.txt` (SDL library target)

**FIXME:** When a header in the core `promeki` library (e.g.
`include/promeki/audiodesc.h`) changes its struct layout, CMake's
incremental dependency tracking rebuilds `libpromeki.so` but does
not always rebuild `libpromeki-sdl.so`. The result is an ABI
mismatch at runtime — code in one library holds the new struct
layout, code in the other still has the old layout — and move/copy
operations corrupt memory (observed as a segfault in
`std::_Rb_tree::_M_move_data` when a `Metadata` move-assigned an
`AudioDesc` across the library boundary).

Workaround: run `make clean && build` after any header-level ABI
change.

## Tasks

- [ ] Tighten the SDL target's header dependency tracking so layout
  changes in `include/promeki/*.h` trigger a rebuild of
  `libpromeki-sdl.so`.
- [ ] Consider adding an ABI-check step to CI that links a
  known-clean test binary against the freshly-built libraries and
  runs a basic smoke test.
