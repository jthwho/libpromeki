# QEMU User-Mode Cross Testing for CI/CD

**Phase:** cross-cutting (CI / cross-compile robustness)
**Status:** unstarted

Run libpromeki's full ctest suite against a cross-compiled `aarch64`
(or future Linux-cross) build on an x86_64 host, by registering
`qemu-<arch>-static` as `CMAKE_CROSSCOMPILING_EMULATOR`.  The same
binaries the BSP target will run get exercised against doctest /
ctest in CI before they're ever flashed.

## Motivation

The cross-compile path under `cmake/configs/cross-aarch64-linux.cmake`
+ `cmake/toolchains/aarch64-linux-gnu.cmake` reliably produces aarch64
ELFs and a sysroot-correct link.  But CMake's `ctest` won't try to
run aarch64 binaries on the x86_64 build host — without an emulator
the unit tests configure but never execute, so the cross build is
only validated at the "it linked" level.  That hides:

- Endianness assumptions baked into byte-stream / RTP / RTMP / AMF /
  CDP code paths (none expected — these were written endian-clean —
  but no current test proves it).
- Alignment-sensitive structs in Buffer / payload / SIMD paths that
  x86_64 hardware tolerates but aarch64 traps on (SIGBUS).
- Highway SIMD kernel correctness on ARM NEON / SVE targets — the
  Highway dispatcher selects a different backend than the scalar /
  AVX2 paths CI currently covers.
- libc / libstdc++ behavior differences on aarch64 (glibc threading,
  `<atomic>` lock-free guarantees on `long long`, etc.).

QEMU user-mode emulation transparently translates aarch64 syscalls
and instructions back to the host kernel, so an ELF binary "just
runs" — same stdout, same exit code, same ctest reporting.  This
makes it practical to gate cross-compile PRs on the same green-test
bar we apply to native builds.

## Approach

### 1. Toolchain integration

Auto-set `CMAKE_CROSSCOMPILING_EMULATOR` in the cross toolchain when
the appropriate `qemu-<arch>-static` is on `PATH`:

```cmake
# cmake/toolchains/aarch64-linux-gnu.cmake (additive)
find_program(_promeki_qemu_aarch64 qemu-aarch64-static)
if(_promeki_qemu_aarch64)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${_promeki_qemu_aarch64}")
    if(PROMEKI_SYSROOT)
        # qemu-user needs --sysroot to resolve the target's dynamic
        # linker (/lib/ld-linux-aarch64.so.1) and shared libraries.
        list(APPEND CMAKE_CROSSCOMPILING_EMULATOR "-L" "${PROMEKI_SYSROOT}")
    endif()
    message(STATUS "Cross-compile test runner: ${CMAKE_CROSSCOMPILING_EMULATOR}")
endif()
unset(_promeki_qemu_aarch64)
```

Opt-out via `-DPROMEKI_CROSS_TEST_EMULATOR=OFF` for users who want
the current "configure tests but don't run them" behavior (e.g.
slow CI minutes, or a target whose syscall surface qemu-user
doesn't cover — DRM, mmap-on-/dev/v4l, etc.).

### 2. CTest hookup

CMake's existing `add_test(... COMMAND <target>)` calls already
prepend `CMAKE_CROSSCOMPILING_EMULATOR` to the command line, so
`ctest`, `cmake --build build --target check`, and the
`build check` helper "just work" once the toolchain line lands.
**No per-test plumbing needed** — this is a single-place change.

### 3. Excluded suites

A handful of tests are inherently host-or-target-specific and
should be skipped under qemu-user.  Mark these with a CTest label
(`CROSS_SKIP_QEMU`) and exclude in the cross test recipe:

| Suite                              | Why skip                                       |
|------------------------------------|------------------------------------------------|
| `func/v4l2_*`                      | qemu-user doesn't emulate `/dev/video*` ioctls |
| `func/rtp_multicast_*`             | needs a real network interface, not loopback   |
| `func/ndi_*`                       | NDI runtime is x86_64 only                     |
| `unit/cuda_*`                      | no GPU through qemu-user                       |
| `bench/*` (`promeki-bench`)        | numbers from a translated binary are noise     |
| Any test touching `/proc/cpuinfo`  | qemu-user reports the host CPU, not the target |

Implementation: thread a `set_tests_properties(${target} PROPERTIES
LABELS "CROSS_SKIP_QEMU")` into the relevant `tests/CMakeLists.txt`
and `tests/func/CMakeLists.txt` entries.  CI invokes
`ctest -LE CROSS_SKIP_QEMU` for the cross run; native runs ignore
the label.

### 4. Dynamic linker paths

QEMU user-mode needs the target's `ld-linux-aarch64.so.1` and shared
libraries reachable at runtime.  Three configurations to support:

1. **Static binaries.**  Simplest: link `unittest-promeki` (and the
   handful of utilities CI exercises) statically with `-static
   -static-libgcc -static-libstdc++`.  No sysroot needed at runtime.
   Cost: bigger binaries; libpromeki itself is shared so any test
   that dlopens the lib needs more care.

2. **Dynamic + sysroot library path.**  Pass `-L <sysroot>` to qemu
   (see the toolchain snippet above).  qemu resolves
   `/lib/ld-linux-aarch64.so.1` relative to the sysroot.  This is
   the recommended path — matches what real running on the target
   sees, and uses the libraries the build linked against.

3. **`binfmt_misc` registration.**  On hosts with
   `qemu-user-binfmt` installed (`update-binfmts --enable
   qemu-aarch64`), the kernel transparently routes aarch64 ELF
   execve calls to qemu without prefixing.  Production CI should
   use this — `./build/bin/unittest-promeki` just runs.  Docker
   image: `multiarch/qemu-user-static --reset -p yes` once at
   container start.

Recommend (2) as the default toolchain wiring (no privileged
operations needed), with docs explaining when to opt into (1) or
(3).

## CI/CD integration

### GitHub Actions outline

```yaml
# .github/workflows/cross-aarch64.yml
jobs:
  cross-aarch64:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - name: Install cross toolchain + qemu-user
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            g++-aarch64-linux-gnu \
            qemu-user-static \
            binfmt-support
      - name: Configure
        run: |
          cmake -B build-arm64 \
                -DPROMEKI_CONFIG_FILE=cross-aarch64-linux \
                -DPROMEKI_WARNINGS_AS_ERRORS=ON
      - name: Build
        run: cmake --build build-arm64 -j$(nproc)
      - name: Build tests
        run: cmake --build build-arm64 --target tests -j$(nproc)
      - name: Test under qemu-user
        run: ctest --test-dir build-arm64 -LE CROSS_SKIP_QEMU \
                   --output-on-failure -j$(nproc)
```

This gives us a third lane alongside the existing native-Debug and
native-asan jobs:

- `native-debug`      — Debug, all tests, host arch
- `native-asan`       — `-fsanitize=address,undefined`, host arch
- `cross-aarch64-qemu` — DevRelease, all-but-`CROSS_SKIP_QEMU`, aarch64

Total CI overhead estimate: ~3-5 min for the cross build (cached
ccache hits on most TUs) + ~30-90s for the qemu test run.  Cheap
enough to run on every PR.

### Local invocation

Same single command:

```sh
sudo apt install qemu-user-static
cmake -B build-arm64 -DPROMEKI_CONFIG_FILE=cross-aarch64-linux
build check    # builds tests, runs ctest, qemu-user underneath
```

## Open questions / design notes

- **Timing-sensitive tests.**  Anything that does
  `std::this_thread::sleep_for(...)` and then asserts elapsed time
  is in a tight window will be flakey under qemu — translation
  adds ~5-10x overhead on emulation-heavy code.  Worth labeling
  `TIMING_SENSITIVE` and excluding from the qemu run, separately
  from `CROSS_SKIP_QEMU`.
- **`tests/func/` vs `tests/unit/`.**  The functional suite
  (`promeki-test`) wraps long-running pipelines and is the more
  likely place to hit qemu performance walls.  Initial scope: only
  `unittest-promeki` under qemu; bring `promeki-test` in once the
  per-case timeout knob handles a 10x scale factor cleanly.
- **Highway runtime dispatch.**  `PROMEKI_ENABLE_CSC=ON` cross-builds
  cleanly today, but Highway's runtime dispatcher will select an
  aarch64 NEON / SVE path under qemu that the host has never run.
  This is the *point* of qemu testing — make sure that path is
  correct — but expect the first qemu run to surface latent CSC
  bugs.  Phase the rollout: start with `PROMEKI_ENABLE_CSC=OFF` in
  the cross-CI config until the CSC suite passes.
- **Doctest output buffering.**  qemu-user occasionally drops
  partial lines on signal.  Use `ctest --output-on-failure` rather
  than parsing stdout mid-run.
- **Reverse direction (x86_64 binary on aarch64 host).**  Not on
  the roadmap — our build hosts are x86_64 and our targets are
  aarch64.  If we ever add a native-aarch64 CI runner (Graviton,
  Ampere), it would supplant qemu for the test step; the toolchain
  wiring stays as-is for non-CI cross builds.

## Future arches

The wiring generalizes — when a second cross toolchain lands
(`armv7-linux-gnueabihf`, `riscv64-linux-gnu`, ...), copy the
`find_program(qemu-<arch>-static)` block and the same CTest path
runs against the new arch.  The `CROSS_SKIP_QEMU` label is arch-
agnostic; the host/network/GPU-sensitive tests it covers are
unrunnable under any qemu-user, not just aarch64.

## References

- `cmake/toolchains/aarch64-linux-gnu.cmake` — toolchain file the
  qemu wiring would be added to.
- `cmake/configs/cross-aarch64-linux.cmake` — cross-compile preset.
- `docs/building.md` § "Cross-compile for Linux / aarch64" — user-
  facing build doc; needs a paragraph + CI example once shipped.
- CMake docs: `CMAKE_CROSSCOMPILING_EMULATOR`, `ctest --label-exclude`.
- `multiarch/qemu-user-static` container image — pre-built binfmt
  wrapper for Docker-based CI.
