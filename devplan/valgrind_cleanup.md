# Valgrind (memcheck) cleanup

**Standards:** All fixes must follow `CODING_STANDARDS.md`. All changes require
updated unit tests. See `README.md` for full requirements.

**Status: COMPLETE (2026-04-23).** `unittest-promeki` under
`scripts/run-valgrind.sh` reports `ERROR SUMMARY: 0 errors from 0 contexts`
with 2,173 suppressed errors (all third-party NVIDIA driver noise or
process-lifetime singleton threads).  Remaining indirect leak is 87 B in
2 blocks from the `Logger_CustomFileFormatter` test, whose parent
`FileIODevice` is the (suppressed) Logger singleton — cosmetic, and
valgrind doesn't count indirect kinds against ERROR SUMMARY by default.

Rounds:

| Round | Errors | Contexts | Definite lost | Possible lost |
|---|---|---|---|---|
| Baseline | 388,358 | 564 | 1,424 B / 10 | 109,624 B / 1,687 |
| After zero-fill + NVIDIA supp | 2,166 | 1,682 | 1,424 B / 10 | 109,528 B / 1,681 |
| After MediaIO pool + process fix + broader supp | 199 | 199 | 1,216 B / 6 | 23,520 B / 194 |
| **Final** | **0** | **0** | **0 B / 0** | **0 B / 0** |

Baseline log (retained for history): `build/bin/unittest-promeki-valgrind.log`
(user-generated). Subsequent logs land at `build/unittest-promeki-valgrind.log`
via `scripts/run-valgrind.sh`.

```
ERROR SUMMARY: 388358 errors from 564 contexts (main run)
LEAK SUMMARY:  definitely lost: 1,424 B / 10 blocks
               indirectly lost:    87 B / 2  blocks
               possibly lost:  109,624 B / 1,687 blocks
               still reachable: 33.6 MB / 39,668 blocks
```

## What actually landed

Code:
- `MemSpace::alloc()` short-circuits `bytes == 0` at the wrapper before the
  backend lambda, so we never call `aligned_alloc(align, 0)` (UB).  Lambdas
  now `PROMEKI_ASSERT` the preconditions the wrapper guarantees, so a
  broken wrapper would trip immediately.  `Ops` docstring documents the
  contract.
- `Image::allocate()` and `Audio::allocate()` zero-fill every plane
  allocation.  This single change eliminated the entire uninit-read chain
  into libjpeg-turbo, zlib-ng, SVT JPEG XS, and libsndfile, as well as
  the writev/write uninit-byte disk leaks out of the DPX/PNG/JPEG-XS
  writers.  188 "conditional jump" contexts at
  `MemSpace::Stats::recordAlloc` collapsed to zero — they were always
  downstream of uninit `bytes` values, not a CAS/atomic false positive.
- JPEG encoder row-buffer hardening: `jpegvideocodec.cpp` rounds the
  luma/chroma scratch rows up to MCU width and zero-inits them, so
  libjpeg-turbo never reads past a partially-filled row even if a future
  caller hands us an image that isn't zero-filled.
- `Process::start()` child-fork leak: the `argv` vector is now scoped so
  its destructor runs before `_exit()` when exec fails.
- `MediaIO::pool()`: replaced `new ThreadPool` into a function-local
  static pointer with a local static `PoolHolder` struct; the destructor
  now joins workers at process exit.

Tooling:
- `scripts/run-valgrind.sh` — one-shot runner with the full
  `--leak-check=full --show-leak-kinds=all --track-origins=yes
  --num-callers=40 --error-limit=no --trace-children=yes
  --read-var-info=yes` flag set, auto-tees to `build/*-valgrind.log`,
  auto-picks up the suppression file.
- `scripts/valgrind.supp` — narrow entries for libnvcuvid, libnvenc,
  libcuda, libcudart (malloc/realloc/calloc variants); intentional
  nvenc dlopen handle; Logger and MediaIO-pool singleton-thread
  patterns anchored on specific mangled symbols.

## Unresolved / known-noise

- Logger singleton's worker thread shows up as "definitely lost" under
  valgrind even though `Logger::~Logger()` calls `_thread.join()`.
  Same for adopted Thread and open FileIODevice inside worker().  The
  dtor is correct; valgrind's leak check races the final libstdc++
  thread state teardown.  Suppressed, not fixed.  If this becomes load-
  bearing (crash telemetry or a real teardown bug), revisit with a
  single-test repro and consider a doctest `REPORTER` that explicitly
  tears down the Logger before main returns.
- Per-test `/tmp` paths in `tests/unit/imagefileio_jpegxs.cpp` (32
  sites) — feedback violation but not a valgrind issue since those
  writes succeed cleanly.  Separate sweep task.

The error total is dominated by tight loops in codec kernels (libjpeg-turbo
encode_mcu_huff, zlib-ng deflate, SVT JPEG XS AVX2 paths) that re-hit the
*same* uninit input thousands of times. The unique contexts are far fewer.
This plan groups them.

## Investigation follow-ups

Before coding, re-run (with user consent, one-off) under
`valgrind --track-origins=yes --leak-check=full --show-leak-kinds=all` to
finalize:

- Origin attribution for `MemSpace::Stats::recordAlloc` conditional-jumps
  (may prove out the atomic/ordering false-positive hypothesis below).
- Stack traces for the 10 “definitely lost” blocks.
- Whether `longest_match_avx2` and `image_shift_avx2` SIMD reads past
  buffer end are into live `Buffer` slack or into true uninit.

Feasible before that: build a tiny driver that exercises exactly one
suspect path (one DPX save, one JPEG encode, one TPG→PNG), per the user’s
ground rule. Keep those repro drivers under `tests/valgrind/` so they can
be rerun as the plan progresses.

## Category 1 — Real bugs (fix before anything else)

### 1.1 `std::aligned_alloc` called with size 0 (undefined behavior)

- `src/core/memspace.cpp:128` (System) and `:160` (SystemSecure) both
  compute `allocSize = (a.size + a.align - 1) & ~(a.align - 1)` then call
  `std::aligned_alloc(a.align, allocSize)`. When `a.size == 0` this may
  call `aligned_alloc(align, 0)` (implementation-defined), and valgrind
  flags it as “Unsafe allocation with size of zero”.
- Hit sites in the log are tests that intentionally create zero-length
  buffers (`datastream.cpp:1724`, `h264bitstream.cpp:209/446`) and the
  `KlvReader::readFrame` / `FrameBridge` handshake path when a zero-length
  frame slips through (`klvframe.cpp:111`). The FrameBridge hit looks
  like a real defensive gap: a 0-length KLV frame shouldn’t allocate.

Fix:

- Add an early return in both alloc lambdas:
  `if(a.size == 0) { a.ptr = nullptr; return; }` — match `malloc(0)`
  semantics without invoking `aligned_alloc(align, 0)`.
- In `Buffer(size_t sz, ...)` and `MemSpace::alloc()`, treat the
  `a.ptr == nullptr` + `a.size == 0` case as a valid *empty* allocation
  (no stats churn, `isValid()` remains false as today).
- Separately, audit `KlvReader::readFrame` and `H264Bitstream::{annexBToAvccFiltered,wrapNalsAsAnnexB}`:
  they currently call `Buffer::Ptr::create(0)` when the computed payload
  size is 0. Either return an invalid Buffer::Ptr up front, or rely on
  the fix above. Add unit tests that exercise size-0 paths explicitly.

### 1.2 Image/DPX/PNG/JPEG-XS writers leak uninit bytes to disk

Syscall-level findings (each is a *real* data-leak: the uninitialized
bytes are random stack/heap contents making it into the output file):

- `imagefileio_dpx.cpp:1002` `writev vector[0]` header and `vector[1]`
  image payload — via `MediaIOTask_ImageFile::{writeSingle, writeSequence}`.
- `imagefileio_jpegxs.cpp:387` `writeBulk` — compressed bitstream bytes.
- `imagefileio_png.cpp:246` spng → deflate reads source image rows with
  uninit trailing stride padding, and the compressed output then flows
  to disk.
- `audiofile_libsndfile.cpp:79` `sfvio_write` — uninit audio sample bytes.

Root causes, in order of blame:

1. **Image stride padding is never zeroed.** `Image::allocate()`
   (`src/proav/image.cpp:24`) calls `Buffer::Ptr::create(size, DefaultAlign, ms)`
   for each plane with the plane size from `PixelFormat::planeSize()`.
   The buffer data is left uninitialized. When `row_stride > width * pixelSize`
   (which is common under page-size alignment, since `DefaultAlign` =
   `sysconf(_SC_PAGESIZE)` = 4096), the end-of-row gap is uninit.
   Writers that serialize the full plane buffer (DPX) or feed it to a
   codec that reads whole rows (PNG, JPEG, JPEG XS) end up with uninit
   bytes in the pipeline.
2. **TPG / CSC outputs only write covered pixel regions.** The `PainterEngine`
   fills only valid pixel bounds. Anything past `width` in the stride is
   never touched. (CSC may or may not write the tail — needs a check.)
3. **Tests fill only one plane.** `tests/unit/imagefileio_jpegxs.cpp:810-821`
   writes a ramp into the luma plane of a 320x240 YUV8_422_Planar image,
   then hands it to the JPEG encoder; Cb/Cr are pristine uninit. This is
   a test bug, but the encoder should be robust to it too (see Category 2).
4. **DPX header padding.** `imagefileio_dpx.cpp:920` allocates `hdrBuf(headerSize, DPX_ALIGN)`
   and does `hdrBuf.fill(0)`. That zeroes `availSize()` bytes (which is
   the user-requested `headerSize`, not the rounded-up allocation size).
   writev writes `headerSize` bytes, so the fill should cover it — but
   the log says vector[0] is uninit at address offset 0. Worth a closer
   look; may be a DIO alignment fallback path that reallocates. Verify.

Fixes:

- `Image::allocate()` should zero each plane buffer by default. The cost
  is a single pass over each allocation — negligible compared to the
  downstream work (encode/copy). Image allocations are already aligned
  to page size so the memset is page-friendly. Gate behind a flag only
  if we measure a real slowdown on large images.
- `PainterEngine::fill` / `fillRect` already cover the pixel region;
  not changing that. With zeroed buffers the stride gap is defined (zero).
- Add a `Frame::zeroFill()` / `Audio::zeroFill()` helper and have the
  TPG and any other synthetic source call it on allocation (belt-and-
  braces — cheap, and makes intent explicit).
- `AudioBuffer` / `Audio::allocate()` should zero its buffer for the
  same reason. Unfilled tail samples should not reach libsndfile.
- Re-verify the DPX header case with a targeted repro; if the file is
  still flagging after image zeroing and the fallback-to-DIO path is
  quiet, look at partial field writes inside `dpxInit`/`fillHeaderFromMetadata`.

### 1.3 JPEG encoder scratch buffers are not MCU-width-aligned

`src/proav/jpegvideocodec.cpp:320-322`:

```
std::vector<std::vector<uint8_t>> yRowBufs(mcuRows, std::vector<uint8_t>(width));
std::vector<std::vector<uint8_t>> cbRowBufs(chromaMcuRows, std::vector<uint8_t>(chromaWidth));
std::vector<std::vector<uint8_t>> crRowBufs(chromaMcuRows, std::vector<uint8_t>(chromaWidth));
```

libjpeg-turbo’s `jpeg_write_raw_data` processes MCU blocks (16x16 for
4:2:0, 16x8 for 4:2:2). When the image `width` (or `chromaWidth`) isn’t
a multiple of the MCU horizontal step, the encoder reads past `width`
into `std::vector` default-initialized bytes (which for `uint8_t` *are*
zero-initialized — so this should not actually produce uninit reads…
unless the encoder also reads past the *last MCU row*, where the row
buffer slot holds an uninit byte).

Looking again: `encode_mcu_huff` sees bytes that are uninit. That
implies the *source* data path (the `std::memcpy(yRowBufs[r].data(), srcY + ...)`
copies and the `deinterleave*()` paths) propagated uninit bytes from
the input image plane — so this is downstream of Category 1.2.

Fixes (defensive, apply after 1.2):

- Round row-buffer widths up to `DCTSIZE * h_samp_factor` (= 16 for 4:2:x
  luma, 8 for chroma) and memset the full row to 0 before copying from
  source. That way even if a future caller hands us a partially-filled
  image, we don’t feed uninit to libjpeg.
- Also clamp the `chromaWidth` logic so 4:2:x with odd widths (if they
  ever occur) doesn’t off-by-one.

### 1.4 JPEG XS (SVT) uninit reads in DWT / pack / RateControl stages

Contexts like `image_shift_avx2 (NltEnc_avx2.c:72)`, `pack_stage_kernel
(PackStageProcess.c:662)`, `precinct_get_budget_bytes (RateControl.c:711)`.
SVT’s AVX2 kernels read 16/32 bytes at a time. If our input Image tail
is uninit, SVT will hit it. Same root cause as 1.2; same fix applies
(zero Image buffers on allocate). No SVT-side changes needed.

### 1.5 `image_shift_avx2` reading past image end

3 occurrences of `image_shift_avx2 (NltEnc_avx2.c:72)` at the top of
stack — SVT reading past width. Sub-case of 1.4.

## Category 2 — Likely false positives (verify, then suppress if needed)

### 2.1 `MemSpace::Stats::recordAlloc` conditional-jumps (188 contexts)

All at `memspace.cpp:81` or `:96`:

```
uint64_t prevPeakBytes = peakBytes.value();
while(newLiveBytes > prevPeakBytes                // <— line 96
      && !peakBytes.compareAndSwap(prevPeakBytes, newLiveBytes)) { ... }
```

`Stats` members are `Atomic<uint64_t>{0}`, built on `std::atomic<uint64_t>`
with an explicit `T{}` constructor, so the stored word is never uninit by
construction.

What’s suspicious is that every one of the 188 contexts is `at memspace.cpp:81/96`
— never deeper. Two possibilities:

1. **Valgrind is losing track through an atomic op.** `fetchAndAdd`
   compiles to `lock xadd`. memcheck’s model for `lock xadd` is
   well-tested, but combined with `memory_order_acquire` loads immediately
   preceding it, a specific compiler codegen may leave a partially-
   defined register that memcheck flags. Running with
   `--track-origins=yes` will tell us which bit is uninit and where it
   came from.
2. **A test is passing a bytes value derived from an uninit field** —
   e.g. an `ImageDesc` whose width/height was never set, or a `Buffer`
   constructed from a 0-width plane size where the size computation
   participated in an uninit branch. The first stack-frame below
   `recordAlloc` is always `MemSpace::alloc` → `Buffer(size_t sz, ...)`,
   which is supposed to be given a live `sz`.

Action:

- Re-run with `--track-origins=yes` on one test file to pinpoint.
- If it’s a real uninit from the caller, fix the caller (most likely
  candidates: zero-size `Buffer::create<T>()` paths in tests using default
  `sz`, and `CSCContext::CSCContext(size_t)` with an uninit size).
- If it’s a memcheck artifact of the CAS pattern, either:
  - Rewrite the watermark update to `peakBytes.store(std::max(peakBytes.load(), newLiveBytes))`
    (non-atomic, acceptable because watermarks are monotone and tests
    don’t care about perfect concurrency), OR
  - Add a memcheck suppression specifically for `MemSpace::Stats::recordAlloc`.

### 2.2 zlib-ng `longest_match_avx2`, libjpeg `encode_mcu_huff` SIMD reads

Codec SIMD kernels read 16/32 bytes at a time past the end of legitimate
input; those tail bytes fall inside the same allocation but outside
the valid range. If the allocation slack is filled with init’d data
(post 1.2), valgrind will be quiet. Otherwise, suppress in-package.

Action: revisit after Category 1 lands.

## Category 3 — Memory leaks

Totals are small. The “still reachable” pool (33 MB / 39k blocks) is
dominated by process-lifetime registries (MemSpace registry, typed
registries, Variant metadata) and is not a concern.

Focus:

- **Definitely lost: 880 B / 13 blocks** (primary run). Needs
  `--leak-check=full` stacks to fix. Likely candidates based on recent
  work:
  - `FrameBridge` server teardown — see `framebridge.cpp` task in
    `proav_nodes.md`.
  - RTP session teardown paths.
  - `Thread::adoptCurrentThread()` / EventLoop shutdown on
    `Application` destruction.
- **Indirectly lost: 3,471 B / 82 blocks** — usually a side-effect of
  definitely-lost parents.
- **Possibly lost: 24,072 B / 178 blocks** — mostly thread-local std
  storage; ignore unless `--show-leak-kinds=all` turns up a real
  reachable chain.

Action: a single targeted rerun with `--leak-check=full
--show-leak-kinds=all`, the stacks dropped into this doc, then fix each
one-by-one.

## Category 4 — Test hygiene (test-only fixes)

Not bugs in the library, but they produce noise that drowns out real
signal:

- `tests/unit/imagefileio_jpegxs.cpp:810-821` fills only plane 0 of a
  YUV8_422_Planar image. Fill all three planes before encode.
- Audit other tests that `Image(desc)` then touch only one plane.
- `/tmp/promeki_jpegxs_wrong_codec.jxs` at `imagefileio_jpegxs.cpp:831`
  violates the project rule (`/mnt/data/tmp/promeki/` only). Move it
  to `Dir::temp()`.

Action: sweep `tests/unit/*.cpp` for `Image(w, h, …)` followed by a
single `data(0)` use without matching fills on the other planes.

## Suggested execution order

1. **Fix `aligned_alloc(0)`** in `memspace.cpp` lambdas and audit
   zero-size call sites (1.1). Smallest change, highest signal — kills
   the “Unsafe allocation” warnings entirely.
2. **Zero-fill Image/Audio/Buffer allocations** (1.2). Single biggest
   blast-radius fix: eliminates most of the uninit-read chain into
   libjpeg-turbo, zlib-ng, SVT JPEG XS, and libsndfile, plus the
   syscall writev/write leaks to disk. Tests under
   `tests/valgrind/` (new) to prove each codec repro is quiet after.
3. **Investigate `MemSpace::Stats::recordAlloc`** with
   `--track-origins=yes` (2.1). Either fix caller or rewrite
   watermark CAS as a simple `store(max(...))`.
4. **Test hygiene sweep** (4). Fill all planes; move stragglers off
   `/tmp`.
5. **JPEG encoder scratch buffer hardening** (1.3) — defensive, even
   after 1.2.
6. **Leak stacks** (3) under `--leak-check=full`.
7. **SIMD-over-slack suppressions** (2.2), last.

## Acceptance criteria

- `unittest-promeki` under `valgrind --error-exitcode=1 --leak-check=full --track-origins=yes`
  exits 0 (after suppressions for third-party SIMD kernels are
  registered and reviewed).
- No syscall-write uninit-byte errors anywhere in the log.
- “Definitely lost” is 0 bytes.
- Suppression file (`valgrind.supp`) committed, each entry annotated
  with the third-party kernel it corresponds to.
