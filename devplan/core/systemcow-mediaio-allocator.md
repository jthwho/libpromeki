# SystemCow Buffers + Buffer/MediaIO Allocator

**Phase:** 4 (MediaIO follow-up) + 1/2 (core Buffer extension)
**Dependencies:** Buffer / BufferImpl / BufferFactory (complete), MediaIO framework (complete), MemSpace registry (complete), `BufferPool` core utility (complete — `include/promeki/bufferpool.h`)
**Depends-on-this:** TPG per-frame burn-in optimisation, NDI DMA-direct write path, NVDEC device-direct read path, future per-NUMA RTP TX path
**Standards:** All code must follow `CODING_STANDARDS.md`. Every new class requires complete doctest unit tests; every modification to an existing class updates its tests. See `README.md` for full requirements.

Six related additions, planned together because TPG validates the full stack at once:

1. **`MemfdRegion`** — a standalone Linux memfd-backed shared region with `MAP_PRIVATE` clone views (per-page CoW, post-seal only), a producer view for population, an explicit `seal()` step, and a `MAP_SHARED PROT_READ` view for shared-reader IPC patterns. Usable on its own.
2. **`Buffer::seal()` + `BufferImpl::seal()` virtual + `BufferImpl::residentBytes()` virtual + `Buffer::isCowBacked()` / `isShared()` predicates** — generic API on `Buffer` so callers transition a buffer from "writable through this handle's backing" to whatever post-producer phase the backend supports, without knowing the backend type. `residentBytes()` exposes real RSS per buffer regardless of backend, so production telemetry can roll it up.
3. **`MemfdBufferImpl` + `MemSpace::SystemCow`** — a `BufferImpl` backed by `MemfdRegion`, registered with `BufferFactory` so `Buffer(size, align, MemSpace::SystemCow)` gets page-level CoW after `seal()`.
4. **`BufferAllocator` (core) + `MediaIOAllocator` (proav)** — `BufferAllocator` is the universal core seam (raw bytes, video plane, audio chunk) usable by anything in the library that needs to delegate buffer placement. `MediaIOAllocator` is a thin proav subclass adding `allocateVideoPayload` / `allocateAudioPayload` for the proav payload types, and is what every `MediaIO` vends.
5. **TPG proof of concept** — `TpgMediaIO`'s allocator hands out `MemSpace::SystemCow` for video; the cache populate path explicitly seals when populate completes. Per-frame burn-in stops paying for the full-frame `memcpy` inside `ensureExclusive`.
6. **Backend rollout roadmap** — concrete next-step migrations for NDI / NVDEC / RTP TX / RTP RX / BurnMediaIO once the abstraction is proved, plus the prerequisite `PinnedHost` / `NumaHost` `MemSpace` work those phases depend on.

---

## Motivation

### CoW gap

`Buffer::ensureExclusive()` → `BufferImplPtr::detach()` → `BufferImpl::_promeki_clone()`. For `HostBufferImpl` (`src/core/hostbufferimpl.cpp:56`) that's `aligned_alloc + memcpy(_allocSize)`. TPG, BurnMediaIO, and any caller that wants to mutate a small region of a cached payload pays for the full allocation on every detach.

For 1080p RGBA8 (8.3 MB) at 60 fps that's ~500 MB/s of avoidable host memory traffic per stream, just to decouple cached background from per-frame burn-in. 4K is 4× that.

### Allocator gap

Today `UncompressedVideoPayload::allocate(desc)` (`src/proav/uncompressedvideopayload.cpp:49`) and friends always construct `Buffer(size)` with `MemSpace::Default`. The only sites that ask for anything else are the ones that build `Buffer` instances by hand (NVDEC builds CUDA-resident Buffers explicitly). Every other backend silently lands in plain heap, even when its DMA / SDK / NIC has a strong preference (pinned host, page-aligned, NUMA-local, device-resident).

The fix is to put a thin allocator interface at *core* layer (so non-MediaIO consumers benefit too), with a proav extension that knows about MediaIO payload types. The *backend* — which is the only party that actually knows — sets the policy, while keeping today's `Payload::allocate(desc)` call sites unchanged.

---

## Phase A — `MemfdRegion` standalone class — **LANDED**

**Files:**
- [x] `include/promeki/memfdregion.h`
- [x] `src/core/memfdregion.cpp`
- [x] `tests/unit/memfdregion.cpp`
- [x] `Error::NotReady` added to `error.h` / `error.cpp`
- [x] `PROMEKI_ENABLE_MEMFD` CMake detection (`memfd_create` + `MFD_ALLOW_SEALING` probe; default ON for Linux, OFF elsewhere)
- [x] Wired into `config.h.in` and `buildinfo.cpp.in`

**Goal:** a small, self-contained class that owns a memfd, supports a single producer write phase, an explicit seal step, and vends three view kinds: producer (MAP_SHARED writable, pre-seal only), clone (MAP_PRIVATE, post-seal only), and read-only (MAP_SHARED PROT_READ, post-seal only). Usable as a complete general-purpose CoW shared-memory primitive.

**Cloning is post-seal only.** Pre-seal `MAP_PRIVATE` views share file-backed pages with the producer until the clone CoWs them, so producer writes propagate to clones for any pages the clone hasn't touched yet — almost never what callers want. We forbid the public form. `cloneView()` and `readOnlyView()` both return null + `Error::NotReady` before `seal()`. The one case that needs an atomic seal-then-clone (the `MemfdBufferImpl` seal transition, which has to maintain pointer stability across the seal boundary) is served by an out-parameter on `seal()` itself.

**API sketch:**

```
class MemfdRegion {
public:
    MemfdRegion() = default;
    MemfdRegion(size_t bytes, const String &debugName = String("anonymous"));
    ~MemfdRegion();

    MemfdRegion(const MemfdRegion &) = delete;       // owns an fd
    MemfdRegion &operator=(const MemfdRegion &) = delete;
    MemfdRegion(MemfdRegion &&) noexcept;
    MemfdRegion &operator=(MemfdRegion &&) noexcept;

    bool   isValid() const;
    bool   isSealed() const;
    size_t size() const;       // rounded up to page size
    int    fd() const;         // raw fd for advanced uses (sendfile, IPC)

    // Phase 1: producer (pre-seal only)
    void  *producerView();     // MAP_SHARED writable; null after first seal()
    /// Idempotent. On first successful call, unmaps the producer view and
    /// transitions to sealed. If @p outFirstClone is non-null, atomically
    /// allocates a fresh MAP_PRIVATE clone and returns it via the
    /// out-parameter — eliminates the unmap window for backends that need
    /// to maintain a pointer-stable mapping across the seal boundary.
    /// Subsequent calls (already-sealed) ignore outFirstClone, return Ok.
    Error  seal(void **outFirstClone = nullptr);

    // Phase 2: caller-owned views (post-seal only)
    void  *cloneView();        // fresh MAP_PRIVATE; null + Error::NotReady pre-seal
    void  *readOnlyView();     // MAP_SHARED|PROT_READ; null + Error::NotReady pre-seal
    Error  releaseView(void *p);

    // Production knobs (madvise wrappers).
    Error  adviseProducer(int madviceFlag);   // pre-seal only
    Error  adviseView(void *p, int madviceFlag);

    // Hugepage-backed variant deferred until a real call site needs it.
};
```

**Implementation notes:**

- Constructor: `memfd_create(debugName, MFD_CLOEXEC | MFD_ALLOW_SEALING)` → `ftruncate(bytes)`. Round size up to page size; `size()` returns the rounded value.
- **Construction failure leaves the region invalid.** `memfd_create` failure (`ENOSYS`, `EMFILE`) and `ftruncate` failure both produce an invalid region (`isValid()` false; `producerView()`/`cloneView()`/`readOnlyView()` all return null; `seal()` returns `Error::Invalid`). No exceptions; no partial state.
- `producerView()` lazily mmaps `MAP_SHARED|MAP_POPULATE` on first call; cached for repeated access. **Returns null on any call after the first successful `seal()`** (a `seal()` that returns non-Ok does not transition the region; the cached producer view may already be unmapped, see below).
- `seal()`:
  1. If already sealed: ignore `outFirstClone`, return `Ok` (idempotent).
  2. munmap the cached producer view.
  3. If `outFirstClone != nullptr`, allocate a fresh `MAP_PRIVATE` clone — at this point the producer view is gone and no clones exist yet, so the new clone is a stable snapshot of whatever the producer left in the file pages.
  4. `fcntl(F_ADD_SEALS, F_SEAL_WRITE|F_SEAL_SHRINK|F_SEAL_GROW)`. Mark sealed.
  - **Why MAP_PRIVATE before F_ADD_SEALS works.** `F_SEAL_WRITE` rejects only when a writable *shared* mapping exists. `MAP_PRIVATE | PROT_WRITE` is fine — writes are CoW into per-clone anonymous pages, never back to the file. So step 3 (MAP_PRIVATE clones) happily co-exists with step 4 (F_ADD_SEALS). Implementation must include a one-line comment noting this so future readers don't "fix" the ordering.
  - **Failure modes.** munmap failure → return `Error::Unknown`, leave state untouched (rare; usually a kernel/driver bug). Atomic clone allocation failure (step 3) → munmap the producer was already done; return `Error::OutOfMemory`; region transitions to a **dead** internal state (separate flag, not just "not sealed"). `F_ADD_SEALS` failure (typically `EBUSY`: another writable mapping exists outside the region's tracking) → return `Error::Busy`; same dead state. From the dead state: `producerView()`, `cloneView()`, and `readOnlyView()` all return null + `Error::Invalid` (not `NotReady` — the region will *never* become ready); `isValid()` returns false; subsequent `seal()` calls return `Error::Invalid`. Production callers should drop the surrounding `MemfdBufferImpl` and re-allocate.
- `cloneView()` mmaps `MAP_PRIVATE` post-seal only. Each call returns a fresh VA; clones are independent. Returns null + `Error::NotReady` pre-seal; null + `Error::Invalid` if the region is dead (post-seal-failure).
- `readOnlyView()` mmaps `MAP_SHARED | PROT_READ` post-seal only. Multiple readers can share one view (cheap, no per-reader CoW machinery). Released via `releaseView()` like any other caller-owned view.
- **Exactly one producer view, owned by the region.** Clones and read-only views are caller-owned and must be paired with `releaseView()`. The region tracks the producer view's lifetime so `seal()` can unmap it; everything else is the caller's problem.
- `releaseView(p)` munmaps unconditionally. `releaseView(nullptr)` returns `Error::Invalid` as a defensive null check (the rest of the library typically crashes on null; this API is one of the few that's tolerant because callers commonly forward an already-released pointer through unwind paths — turning that into a hard crash on an unwind path is worse than returning Invalid).
- `adviseProducer` / `adviseView` forward to `madvise(2)` on the relevant mapping. Production knobs: `MADV_HUGEPAGE` on the producer view for 4K/8K frames, `MADV_RANDOM` / `MADV_SEQUENTIAL` on read-only views, `MADV_DONTNEED` to drop pages. Stub on non-Linux; return `Error::NotSupported`.
- Destructor: closes the fd and munmaps the cached producer view if it survived a sealing failure. Outstanding clone / read-only views are the caller's problem.
- Move-constructible / move-assignable via fd rebinding. Not copy-constructible — owning two copies of an fd is too easy to misuse.
- **Linux-only.** Header guards everything in `#ifdef __linux__`. On non-Linux builds the class compiles to a stub that always reports invalid; callers are expected to fall back to `HostBufferImpl`. Add a portable abstraction later (macOS `vm_remap` / Windows section objects) only when there's a real port target.

**Standalone use cases the API now covers:**

- **CoW snapshot per consumer** (the `MemfdBufferImpl` use case). Producer fills, calls `seal(&firstClone)`, vends additional clone views per consumer; each consumer CoWs only the pages it dirties.
- **Broadcast read-only IPC.** Producer fills, seals, exposes `fd()` over a Unix socket; receivers `mmap` `MAP_SHARED|PROT_READ` (the equivalent of `readOnlyView()` in their address space) and share physical pages with the source.
- **Live trace / log dump.** Snapshot scratch: capture a memory region, hand a clone view to a writer thread for offline serialisation while the producer continues writing into a fresh region.

**Doctests:**

- [x] Construct + size round-up to page size; `isValid()` true; `isSealed()` false.
- [x] `producerView()` returns same pointer on repeated calls until `seal()`.
- [x] **Pre-seal `cloneView()` / `readOnlyView()` return null + `Error::NotReady`.** (Replaces the earlier "producer writes, cloneView reads back" test, which exercised the kernel's MAP_PRIVATE-alongside-MAP_SHARED behaviour and would have baked a footgun into the public contract.)
- [x] After `seal()`: `producerView()` returns null; `isSealed()` true; clone + read-only views work; second `seal()` is a no-op `Ok`.
- [x] **`seal(&out)` atomic-clone path:** populate, `seal(&firstClone)` returns `Ok` and `firstClone != nullptr`; that pointer reads back the populated content.
- [x] Two `cloneView()`s diverge: write a byte in clone A; clone B and a fresh clone C still see the original byte.
- [x] `readOnlyView()` shows the producer's pre-seal content; multiple read-only views over the same region behave like the kernel allows them to (don't assert pointer equality — the kernel may merge).
- [x] `releaseView(nullptr)` returns `Error::Invalid`.
- [x] Move construction: source becomes invalid, destination assumes the fd.
- [x] **Construction-failure path:** default-constructed region — `isValid()` false; `cloneView` / `readOnlyView` return null + `Error::Invalid`; `seal()` returns `Error::Invalid`. (`memfd_create` injection deferred — the public-API observable is what matters and the default-constructed path covers it.)
- [x] **Seal-failure path (dead state):** with an outstanding writable mapping forced via the test fixture (separate `mmap` of a `dup(fd)` of the region), `seal()` returns non-Ok; `isValid()` becomes false; `producerView()`, `cloneView()`, and `readOnlyView()` all return null + `Error::Invalid` (not `NotReady`); a second `seal()` returns `Error::Invalid`.
- [x] **Page-CoW measurement test (the property we're trading on).** Allocate 16 MiB, seal, take two clones, write a single byte at offset 0 in clone A. Sample `Private_Dirty` from `/proc/self/smaps` for each clone's VMA. Assert clone A has exactly one page-worth of private dirty bytes; clone B has zero. (`mincore(2)` is *not* used — for `MAP_PRIVATE` mappings it reports residency of the underlying file-cache page that *would* be returned on read, which says nothing about whether the user has already CoW'd. `Private_Dirty` is the correct kernel-reported measure of "private anonymous pages this clone has allocated." Same source Phase B's `residentBytes()` reads.) If the kernel ever silently changes to populate-on-clone, this test fires immediately rather than as a perf regression.

---

## Phase B — `Buffer::seal()` + `residentBytes()` + `MemfdBufferImpl` + `MemSpace::SystemCow` — **LANDED**

**Files:**
- [x] Modify `include/promeki/bufferimpl.h` — add `virtual Error seal() const` (default no-op `Ok`); add `virtual size_t residentBytes() const` (default `allocSize()`); add `virtual bool isCowBacked() const` (default false).
- [x] Modify `include/promeki/buffer.h` — add `[[nodiscard]] Error seal() const`; add `bool isShared() const`; add `bool isCowBacked() const`; add `size_t residentBytes() const`.
- [x] Modify `include/promeki/bufferview.h`, `src/core/bufferview.cpp` — add `[[nodiscard]] Error seal() const` walking unique buffers (mirrors `ensureExclusive`).
- [x] `include/promeki/memfdbufferimpl.h`, `src/core/memfdbufferimpl.cpp`
- [x] Modify `include/promeki/memspace.h` — add `SystemCow = 4` to the built-in `enum ID` (next sequential after `CudaHost = 3`); document its stricter concurrent-access contract.
- [x] Modify `src/core/bufferfactory.cpp` — register the SystemCow factory; `HostBufferImpl` fallback when `PROMEKI_ENABLE_MEMFD` is off.
- [x] Modify `src/core/memspace.cpp` — register `SystemCow` Ops; `MemSpace::Stats` adds `peakResidentBytes` (sampled at impl-destruction; CAS-loop). On-demand `residentBytesSnapshot()` deferred — see B.7.
- [x] Modify `include/promeki/hostbufferimpl.h` — `_hostPtr` is now `mutable` so `MemfdBufferImpl::seal() const` can swap it across the seal transition without `const_cast`.
- [x] `tests/unit/memfdbufferimpl.cpp` (10 SystemCow test cases)
- [x] Generic API tests added to `tests/unit/buffer.cpp` and `tests/unit/bufferview.cpp` (10 new test cases between them).

### B.0 — Prerequisite: `_hostPtr` mutability

`MemfdBufferImpl::seal() const` rewrites the base class's `_hostPtr` across the seal pointer-swap. The base member (`HostMappedBufferImpl::_hostPtr` in `include/promeki/hostbufferimpl.h:128`) must be `mutable` so the `const` seal can update it without `const_cast`. If it isn't already `mutable`, B.1 makes it so. Note this explicitly in the commit so the change is auditable; do **not** paper over it with `const_cast`.

### B.1 — `Buffer::seal()`, `residentBytes()`, `isCowBacked()`, `isShared()`

The `seal()` operation is *generic*: it tells a Buffer "I'm done writing through this backing; transition to whatever post-write phase you support." Most backends have nothing to seal, so the default is a trivial success. Backends with a producer→post-producer transition override:

- `MemfdBufferImpl` (Phase B.2): producer phase → CoW phase.
- *Future* read-only-snapshot backend (e.g. a NIC RX buffer that goes immutable post-reception) — seal would freeze the buffer for cache-line tracking, ECC verification, etc.
- *Future* "publish to GPU" backend — seal would do the host→device flush at producer-done time rather than at first-`mapAcquire(CudaDevice)`.

Document the API in those general terms; CoW is the first important consumer but not the defining one.

```
// In BufferImpl:
//
// Default: success no-op.  Backends with a seal concept override.
// const + mutable internal state to match setLogicalSize / fill / etc.
virtual Error seal() const { return Error::Ok; }

// Resident-memory accounting.  Default returns allocSize() (the
// allocation is fully resident).  Sparse backends (MemfdBufferImpl
// post-CoW, future swap-aware backends) override with the actual
// page-resident count.  Used by MemSpace::Stats for production RSS
// telemetry.
virtual size_t residentBytes() const { return allocSize(); }
```

```
// In Buffer:
//
// Idempotent.  After seal, the underlying backing transitions to whatever
// post-producer phase the backend supports (e.g. CoW for SystemCow,
// host→device flush for a future GPU-publish backend).  For backends
// without a seal concept this is a no-op success — call sites can issue
// seal() unconditionally without knowing the backend type.
//
// **Concurrency contract:** for backends that mutate internal state on
// seal (notably MemSpace::SystemCow), concurrent reads of data() on
// sibling Buffer handles during seal() / ensureExclusive() are unsafe.
// Backends where seal is a no-op (everything else today) accept
// concurrent reads.  Use isCowBacked() to query.
[[nodiscard]] Error seal() const {
    return _d.isValid() ? _d->seal() : Error::Invalid;
}

/// True if the underlying impl is a CoW backend (SystemCow today).
/// Callers that need to honour the stricter concurrent-access contract
/// query via this predicate.
bool isCowBacked() const;

/// True if this Buffer is the only handle to its impl (refcount == 1).
/// Backends that want to skip ensureExclusive when they're already the
/// sole holder branch on this.
bool isShared() const;
```

`seal()` does **not** detach. Multiple Buffer handles to the same impl all observe the seal — that's the correct semantics, since "the underlying backing transitioned" is a property of the impl, not of any single handle.

`BufferView::seal()` walks unique backing Buffers and calls `Buffer::seal()` on each (deduplicated identity, mirroring `ensureExclusive`). Default-allocator buffers no-op; SystemCow buffers transition to CoW.

`isCowBacked()` is implemented via a `BufferImpl::isCowBacked()` virtual returning false by default; `MemfdBufferImpl` overrides to return true. Cheap; no downcast needed.

### B.2 — `MemfdBufferImpl` class shape

```
class MemfdBufferImpl : public HostMappedBufferImpl {
public:
    PROMEKI_SHARED_DERIVED(MemfdBufferImpl)

    MemfdBufferImpl(const MemSpace &ms, size_t bytes, size_t align);
    ~MemfdBufferImpl() override;

    bool   canClone() const override;       // _region->isValid() AND not in a dead post-seal-failure state
    bool   isCowBacked() const override { return true; }
    Error  seal() const override;
    size_t residentBytes() const override;             // mincore over current view
    MemfdBufferImpl *_promeki_clone() const override;

private:
    SharedPtr<MemfdRegion> _region;     // shared across this impl + its clones
    mutable Mutex          _sealMutex;
    mutable bool           _sealed = false;
    // _hostPtr lives on HostMappedBufferImpl as a `protected void *`
    // (include/promeki/hostbufferimpl.h:128).  We update it directly
    // across the seal pointer-swap.  Buffer::data() reads via
    // mappedHostData() on every call (buffer.h:181, 195), so the swap
    // is observable to readers via re-fetch — no additional plumbing.
};
```

`_region` is a `SharedPtr<MemfdRegion>` so the same fd survives across all clone impls — every cloned `MemfdBufferImpl` references the same region; the region's destructor (and the fd close) only fires when the last clone goes away.

**Constructor flow:**

1. Call base constructor with `_hostPtr = nullptr` (we'll set it after constructing the region).
2. Allocate `_region = SharedPtr<MemfdRegion>::makeNew(bytes, debugName)`.
3. If `_region->isValid()`: write through to base's `_hostPtr = _region->producerView()`.
4. If invalid (memfd unavailable / fd-table exhausted): leave `_hostPtr` null. The factory's fallback path (B.8) is the documented graceful answer for *expected* unavailability; an in-construction-failure observable is the contract for callers who construct via factory after CMake-time `PROMEKI_ENABLE_MEMFD` was true but a runtime failure (e.g. `EMFILE`) bites.

**Invalid-construction observable:** `_region.isValid()` returns false; `canClone()` returns false; `_hostPtr` stays null; `Buffer::data()` returns null; `Buffer::ensureExclusive()` is a no-op (nothing to detach to). Document on `MemfdBufferImpl` so callers know what to look for.

**Post-seal-failure observable:** if `seal()` fails (region transitions to dead state), `canClone()` returns false from that point on, `_hostPtr` is set to null, and subsequent `ensureExclusive()` is a no-op (nothing to detach to). Same surface as construction-failure — callers can branch on a single predicate.

### B.3 — Lifecycle phases and the seal transition

Every `MemfdBufferImpl` is in one of two phases:

1. **Producer phase** (between construction and first seal). The impl holds a `MAP_SHARED|PROT_WRITE` "producer view" of the memfd via `_hostPtr`. Writes through `data()` / `fill` / `copyFrom` propagate to the memfd's pages and are visible to all Buffer handles sharing this impl.
2. **CoW phase** (after seal). The producer view is gone. The impl holds a `MAP_PRIVATE` source clone view via `_hostPtr`; subsequent `_promeki_clone()` calls produce sibling impls each with their own `MAP_PRIVATE` view of the same shared region. Writes through any view CoW pages privately to that view.

**The seal transition (`seal()` body):**

```
Error MemfdBufferImpl::seal() const {
    Lock lock(_sealMutex);
    if (_sealed) return Error::Ok;
    if (!_region || !_region->isValid()) return Error::Invalid;

    void *newView = nullptr;
    Error err = _region->seal(&newView);   // atomic seal-then-clone, no race window
    if (err != Error::Ok) {
        // Producer view already unmapped inside _region->seal() before failure;
        // _hostPtr now references unmapped memory and the impl is dead.
        _hostPtr = nullptr;
        return err;
    }
    _hostPtr = newView;     // protected member on HostMappedBufferImpl
    _sealed = true;
    return Error::Ok;
}
```

**Trigger paths into `seal()`:**
- Explicit `Buffer::seal()` / `BufferView::seal()` from the caller. Recommended for backends like TPG that have a clear "populate done" point on a single thread before any clones / readers happen — gives flame graphs a deterministic transition.
- Implicit auto-seal at the start of `_promeki_clone()` (safety net if the caller forgets to seal — guarantees the clone is valid).

Both paths reach the same `seal()` body; idempotent. The explicit path is purely an *observability* convention; correctness is independent.

### B.4 — Pointer-stability and concurrency contract

`Buffer::data()` always re-fetches from the impl's `mappedHostData()` (`buffer.h:181, 195`). The contract is **always re-fetch**; never cache `data()` across operations. With that contract honoured, the seal pointer-swap is invisible to correct call sites.

The constraint that *is* visible: **concurrent reads of `data()` (or memcpy from/to the buffer) on a sibling Buffer while another holder calls `seal()` / `ensureExclusive()` are unsafe.** The seal transition mutates `_hostPtr` under `_sealMutex`; readers don't take that mutex. Two-stage protection:

1. The contract is documented on `Buffer::seal()`, on `MemfdBufferImpl`, and on the `MemSpace::SystemCow` enum value.
2. `Buffer::isCowBacked()` lets generic code branch on the stricter contract without a downcast.

For TPG and the planned consumers this is satisfied trivially: TPG populates on the open strand, calls `Buffer::seal()` once at end-of-populate, and from then on all per-frame `ensureExclusive` calls happen on the runPhase strand. No cross-thread races.

### B.5 — `_promeki_clone()` body

1. If `!_sealed`, call `seal()` (with the same mutex-acquire-and-recheck pattern). Return early on seal failure.
2. Construct a sibling `MemfdBufferImpl` that shares `_region` (same `SharedPtr`).
3. Sibling acquires its own view via `_region->cloneView()` (post-seal — region is sealed by step 1 — so this is the safe public API), writes it into the sibling's `_hostPtr`, marks itself `_sealed = true` (no further seal needed; the region is sealed once and stays that way).
4. **Preserve modifications** — if `this->_dirty` is true, copy this view's current content into the sibling's clone view. This is the semantic equivalent of `HostBufferImpl::_promeki_clone()`'s deep copy: it preserves any writes the caller made through this view. Without it, the kernel's `MAP_PRIVATE`-from-sealed-file CoW would silently drop private modifications and the new clone would show only the sealed source's content.
   - **Per-page copy via `/proc/self/pagemap`**: instead of a full-frame `memcpy`, a `copyDirtyPages()` helper reads the source view's pagemap entries and copies only pages that are present and *not* file-page-mapped (i.e. anonymous pages that have already been CoW'd through this view). Untouched pages stay shared with the sealed file's page-cache pages via the new clone's `MAP_PRIVATE` — they don't get materialised. For TPG burn-in, this means each per-frame detach from a burn-modified clone pays only for the burn band's pages (~64 KiB), not the whole frame (~8.3 MiB at 1080p RGBA). The full-frame memcpy fallback runs only when the pagemap can't be read (sandboxes, stripped permissions, etc.).
   - Sibling clones are born `_dirty = true` — `ensureExclusive()` only calls `_promeki_clone` because the caller intends to write through the new clone, so the next detach from that clone needs to preserve writes.
   - The post-seal source stays `_dirty = false`, so the *first* detach from the cached payload (TPG's per-frame transition, the SystemCow win we're paying for) skips the copy path entirely and uses cheap kernel CoW from sealed file pages.

**Why this matters in practice — TPG burn-in regression:**
The first SystemCow / TPG integration shipped without this `_dirty` machinery and the burn text vanished from rendered frames.  The path: TPG's burn pass calls `ensureExclusive()` on the cached payload's plane, gets a `MAP_PRIVATE` clone, paints the burn band into it. `FastFont::_paintEngine` keeps a reference to that clone (so the engine can outlive the original payload — a long-standing PaintEngine_Interleaved invariant). The data-encoder pass that runs immediately after the burn calls `ensureExclusive()` again; refcount=2 (frame slot + FastFont's `_plane0`) so the buffer detaches a second time. Without the `_dirty` memcpy, the kernel returns a fresh `MAP_PRIVATE` clone of the *sealed file pages* — bars only, no burn — and the data encoder's writes go onto that clone. The frame that propagates downstream has bars + dataenc marks but no burn pixels. With the `_dirty` flag wired up, the second detach memcpys the burn-modified content into the new clone before the data encoder writes its band; everything composes correctly. `HostBufferImpl::_promeki_clone()` already had deep-copy semantics, which is why the same TPG path worked before SystemCow shipped.

### B.6 — Destructor

If `_hostPtr != nullptr`, `_region->releaseView(_hostPtr)`. The `SharedPtr<MemfdRegion>` drops; when the last clone goes away the region destructor closes the fd.

### B.7 — Stats accounting

`MemSpace::Stats` adds one production-grade piece alongside the existing virtual-byte counters:

- **`peakResidentBytes` (Atomic):** updated at `BufferImpl` destruction by sampling `residentBytes()` and bumping the peak via CAS. Approximate (only catches values at destruction) but near-zero cost; gives a useful lower bound on peak RSS for SystemCow. Exposed via `Stats::Snapshot::peakResidentBytes` and the per-MemSpace `statsReport()` line.

**Deferred:** A `residentBytesSnapshot()` that walks live impls and sums their `residentBytes()` was originally planned for v1 but is deferred — there is no per-MemSpace live-impl registry today, and the `peakResidentBytes` watermark already covers the operational use case (capacity planning, crash reports). When a real consumer wants on-demand precise current residency (rather than the destruction-time peak), add the live-impl registry then; the API surface (a `MemSpace::residentBytesSnapshot()` accessor) is trivially additive.

Document on `MemSpace::SystemCow` that `allocBytes` / `liveBytes` count virtual address space and `peakResidentBytes` counts physical pages — the gap is intentional and is the entire reason for this MemSpace.

### B.8 — Factory registration

`MemSpace::SystemCow = 4` (next built-in after `CudaHost = 3`). Register in the same place `System` and `SystemSecure` are registered (`src/core/bufferfactory.cpp`). The MemSpace's `MemDomain` is `Host`. No special `Ops` needed — `BufferImpl` carries everything.

If `PROMEKI_ENABLE_MEMFD` is unset (non-Linux build, or memfd unavailable at compile-time), the SystemCow factory falls back to constructing a plain `HostBufferImpl`. SystemCow allocations on those builds are correctness-equivalent (still host memory) but lose the CoW optimisation. `Buffer::seal()` on a fallback HostBufferImpl is the default no-op success; `Buffer::isCowBacked()` returns false. The factory still registers the SystemCow ID either way, so callers that don't care about the specific backend get correct behaviour transparently.

### B.9 — `Buffer::ensureExclusive` interaction

No changes to `Buffer` or `BufferView` beyond adding `seal()` / `isShared()` / `isCowBacked()`. The existing `_d.detach()` path calls `_promeki_clone()` which auto-seals if needed. `BufferView::ensureExclusive()` (`src/core/bufferview.cpp:177`) walks unique backing buffers — unchanged. Multi-plane payloads automatically get per-plane CoW. `BufferView::seal()` mirrors that walk so multi-plane payloads transition in one call.

### B.10 — Doctests

- [x] **API surface:** `Buffer::seal()` on a `HostBufferImpl`-backed Buffer returns `Ok` (no-op default). `Buffer::isCowBacked()` returns false; `BufferImpl::residentBytes()` returns `allocSize()`.
- [x] **`isShared()`:** single-handle Buffer reports `false`; after a copy, both handles report `true`; after one drops, the survivor reports `false`. Invalid Buffer returns false from both predicates.
- [x] **Producer phase:** `Buffer(size, align, MemSpace::SystemCow)` constructs valid; `data()` writable; `isCowBacked()` true; two handles to the same impl see each other's writes.
- [x] **Explicit seal:** populate, `seal()`, then `ensureExclusive()` on one of two handles — detached write doesn't show on the original; original is still readable; original's `data()` pointer may have changed (don't assert pointer equality across seal).
- [x] **Implicit seal-on-first-clone:** populate, *don't* call seal, immediately `ensureExclusive()` — works exactly the same as explicit.
- [x] **Idempotent seal:** call `seal()` three times, all `Ok`; observable state same.
- [x] **Multiple clones:** populate, seal, take three clones, write a different byte in each — all three diverge from each other and from the source.
- [x] **`BufferView` interaction:** multi-plane SystemCow payload, `BufferView::seal()` succeeds; mutate one plane through `ensureExclusive`, other planes still share with cache — confirm via impl-pointer identity.
- [x] **`residentBytes` end-to-end:** allocate 16 MB SystemCow, populate fully, seal, clone, write 1 byte in the clone — clone's `residentBytes()` is well below `bytes / 4`, exercising the `Private_Dirty`-from-smaps path. Linux-only test — guarded on `PROMEKI_ENABLE_MEMFD`.
- [x] **`residentBytes` on default backend:** equal to `allocSize()` (covered alongside the API-surface test).
- [ ] **`MemSpace::Stats::peakResidentBytes` regression check** — deferred. The watermark is wired (CAS update on impl destruction) but a dedicated test would race with other test cases populating the SystemCow stats; revisit if a real consumer of the value lands.
- [ ] **`canClone` behaviour after construction failure** — deferred. Construction-failure path is exercised at the `MemfdRegion` layer (Phase A), and the fallback `HostBufferImpl` path on non-memfd builds covers the "no-CoW backend" case. A specific fd-table-exhaustion test would need an injection seam in `MemfdRegion`; not built yet.
- [ ] **Seal failure on `MemfdBufferImpl`** — deferred for the same reason as above (would need an injection seam to reliably force `MemfdRegion::seal()` failure in the impl path; the dead-state observable is already covered by the Phase A region-level test).
- [x] **Fallback build:** with `PROMEKI_ENABLE_MEMFD` unset, `Buffer(…, SystemCow)` produces a `HostBufferImpl`-backed Buffer; `seal()` returns `Ok`; `isCowBacked()` false; `ensureExclusive` produces a deep-copy clone (the documented fallback). Test compiles in the `#else` branch — passes locally on non-memfd configurations.

---

## Phase C — `BufferAllocator` (core) + `MediaIOAllocator` (proav) — **LANDED**

**Files:**
- [x] `include/promeki/bufferallocator.h`, `src/core/bufferallocator.cpp`, `tests/unit/bufferallocator.cpp` (10 test cases / 22 assertions)
- [x] `include/promeki/mediaioallocator.h`, `src/proav/mediaioallocator.cpp`, `tests/unit/mediaioallocator.cpp` (9 test cases / 30 assertions)
- [x] Modify `include/promeki/mediaio.h`, `src/proav/mediaio.cpp` — `allocator()` accessor (never returns null) + `setAllocator(Ptr)` (null clears to default).
- [x] Modify `include/promeki/mediaioport.h`, `src/proav/mediaioport.cpp` — per-port read-only accessor delegating to MediaIO.
- [x] Modify `src/proav/uncompressedvideopayload.cpp` — `UncompressedVideoPayload::allocate(desc)` now routes through `MediaIOAllocator::defaultAllocator()->allocateVideoPayload(desc)`. Existing call sites unchanged in behaviour.
- [x] **API surface contract:** all `BufferAllocator` allocator methods are `const` so `SharedPtr<BufferAllocator, false>::operator->` reaches them without `modify()`. Pool-backed subclasses declare their internal pool / mutex / counters as `mutable` (same trick `BufferImpl::seal()` uses).
- [N/A] `src/proav/uncompressedaudiopayload.cpp` — there is no current `PcmAudioPayload::allocate(desc, samples)` static helper to route. New audio call sites use `MediaIOAllocator::allocateAudioPayload(desc, samples)` directly; existing call sites construct via `Ptr::create` and were never an allocator-routing target.

### C.1 — `BufferAllocator` (core)

The buffer-placement seam. Pure allocation primitives; no payload knowledge. Anything in the library that wants to delegate "where does this Buffer live?" to a backend takes a `BufferAllocator&`. Plain abstract class — *not* derived from `ObjectBase`, since allocators don't want event-loop affinity, the metadata system, or the ObjectBase pointer registry. Lighter API surface, fewer threading-model footguns.

```
class BufferAllocator {
public:
    using Ptr = SharedPtr<BufferAllocator>;

    virtual ~BufferAllocator() = default;

    /// Diagnostic name — appears in logs and tracing ("which allocator
    /// returned this Buffer?").  Subclasses override.
    virtual String name() const = 0;

    // Per-plane buffer; backends decide MemSpace + alignment from desc.
    virtual Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) = 0;

    // Audio chunk for `samples` frames in this AudioDesc layout.
    virtual Buffer allocateAudioChunk(const AudioDesc &desc, size_t samples) = 0;

    // Generic byte buffer (metadata, codec extra-data, packets, …).
    virtual Buffer allocateBytes(size_t bytes, size_t align = 0) = 0;

    /// Process-wide default — always returns the same instance.  Routes
    /// every method through Buffer(bytes, align, MemSpace::Default).
    static Ptr defaultAllocator();
};
```

`DefaultBufferAllocator` (concrete impl) is stateless; trivially thread-safe. Backends subclass and override.

**Singleton init.** `defaultAllocator()` uses a Meyers' singleton — `static const Ptr instance = SharedPtr<DefaultBufferAllocator>::makeNew(); return instance;`. C++11+ guarantees the static-local initialisation is thread-safe. Same pattern for `MediaIOAllocator::defaultAllocator()`. Do **not** use a heap-allocated singleton or a library-init function — the Meyers form is race-free, leak-free, and order-of-init-safe.

### C.2 — `MediaIOAllocator` (proav)

Extends `BufferAllocator` with full-payload helpers for proav payload types. This is what every `MediaIO` vends.

```
class MediaIOAllocator : public BufferAllocator {
public:
    using Ptr = SharedPtr<MediaIOAllocator>;

    String name() const override { return "DefaultMediaIOAllocator"; }
    Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) override;
    Buffer allocateAudioChunk(const AudioDesc &desc, size_t samples) override;
    Buffer allocateBytes(size_t bytes, size_t align = 0) override;

    // Full-payload allocators — default impls build from the per-plane /
    // per-chunk primitives above.  Override when a backend has a structural
    // reason to allocate the whole payload as one unit (e.g. NVDEC's single
    // contiguous device alloc covering all planes).
    virtual UncompressedVideoPayload::Ptr allocateVideoPayload(const ImageDesc &desc);
    virtual UncompressedAudioPayload::Ptr allocateAudioPayload(const AudioDesc &desc, size_t samples);

    /// Process-wide default.
    static Ptr defaultAllocator();
};
```

`MediaIO::allocator()` returns `MediaIOAllocator::Ptr`. Backends downcast freely or just use the proav-specific methods directly. External tools that only need raw buffer allocation work via the `BufferAllocator` base — no proav coupling.

### C.3 — `MediaIO` integration

```
class MediaIO {
    // ...
    /// Returns the assigned allocator, or MediaIOAllocator::defaultAllocator()
    /// when none has been set.  Never returns null.
    MediaIOAllocator::Ptr allocator() const;

    /// Set a per-MediaIO allocator override.  Pass null to clear and revert
    /// to defaultAllocator().  Typically called by the backend's
    /// executeCmd(Open) once it knows what placement policy it wants.
    void setAllocator(MediaIOAllocator::Ptr a);
};

class MediaIOPort {
    // ...
    /// Delegates to mediaIO()->allocator().  Per-port override deferred to
    /// v2 — no concrete consumer named today; trivially additive when one
    /// appears.
    MediaIOAllocator::Ptr allocator() const;
};
```

### C.4 — Routing default `Payload::allocate`

`UncompressedVideoPayload::allocate(desc)` becomes a thin call to `MediaIOAllocator::defaultAllocator()->allocateVideoPayload(desc)`. Existing call sites (file I/O, image codecs, CSC, etc.) keep working unchanged. Backends that *do* want a different policy fetch their own allocator: `port->allocator()->allocateVideoPayload(desc)`.

### C.5 — Threading and lifetime contracts

- **Thread safety.** Allocator implementations *must* be thread-safe — they're called from prefetch workers, write strands, CSC threads, etc. Document on the base class. The default is trivially safe (stateless). Backend overrides that hold pools take their own `Mutex`.
- **Allocator outlives vended Buffers.** Any future pooled backend that recycles Buffers back to a pool on Buffer destruction must keep the pool alive across every vended Buffer. The recommended contract is the same one we'll use elsewhere: the recycling `BufferImpl` subclass holds a `SharedPtr` to the pool / allocator state, so the allocator's lifetime is anchored by outstanding Buffers automatically; no explicit "release" hook on the allocator itself. No backend in this devplan needs that today — documented for future reference only.
- **Allocator failure modes.** If `allocateX` cannot satisfy the request (out of pool, OS allocation failure), it returns an invalid `Buffer` (or null `Ptr` for full-payload paths). Callers handle invalid returns the same way they handle a failed `Buffer(size)` today. No exceptions.

### C.6 — Doctests

**`BufferAllocator` (C.1):**
- [x] Default allocator round-trip through `allocateBytes` / `allocateVideoPlane` / `allocateAudioChunk`; shapes match `Buffer(size, …, MemSpace::Default)`.
- [x] `name()` returns the documented default; subclass override visible.
- [x] Concurrency smoke: two threads each calling `allocateBytes` from the default — no shared mutable state, no races.

**`MediaIOAllocator` (C.2):**
- [x] Default video/audio payload round-trip; shapes match today's `Payload::allocate` output.
- [x] Per-plane override: a tiny test allocator stamps a marker into every `allocateVideoPlane` Buffer; verify `allocateVideoPayload` (default impl, base class) picks up the override automatically.
- [x] Full-payload override: another test allocator returns a single-Buffer payload from `allocateVideoPayload`; verify the per-plane path is *not* called when the full-payload override is present.
- [x] `MediaIO::allocator()` returns the assigned allocator after `setAllocator`; null assignment falls back to default; **never returns null** (asserted explicitly).
- [ ] `Port::allocator()` delegates to MediaIO. (no dedicated test yet — covered by the `MediaIO::setAllocator` test indirectly; add a port-level round-trip test when a concrete MediaIOPort test fixture exists)
- [x] Failure path: invalid descriptor returns null payload from `allocateVideoPayload`; `allocateBytes(0)` returns invalid Buffer.

---

## Phase D — TPG proof of concept — **LANDED**

**Files:**
- [x] Modify `src/proav/tpgmediaio.cpp` — `TpgAllocator` (subclass of `MediaIOAllocator`) overrides `allocateVideoPlane` to hand out `Buffer(planeBytes, align, MemSpace::SystemCow)`; audio + bytes fall through. Installed via `setAllocator()` on both the MediaIO and the embedded `_videoPattern` at the end of `executeCmd(Open)`. The two install lines are the documented rollback point if SystemCow ever needs to be reverted in production.
- [x] Modify `include/promeki/videotestpattern.h`, `src/proav/videotestpattern.cpp` — added `setAllocator()` / `allocator()` accessors and a private `MediaIOAllocator::Ptr _allocator` member; `cachedPayload()` template now routes allocation through `_allocator` (or falls back to `UncompressedVideoPayload::allocate`) and calls `slot->data().seal()` after the builder finishes populating the slot. Default backends no-op; SystemCow flips into CoW phase.
- [x] `tests/unit/videotestpattern.cpp` — three new test cases:
  - `VideoTestPattern_Allocator_RoutesCachedPayload` — installed allocator gets called for every plane on the first hit and zero times on subsequent cache hits (impl-pointer identity).
  - `VideoTestPattern_Allocator_ChangeInvalidatesCache` — `setAllocator` drops every cache slot so the new policy is picked up immediately.
  - `VideoTestPattern_SystemCow_PerFrameDetachIsCheap` (Linux only, gated on `PROMEKI_ENABLE_MEMFD`) — payload's planes report `isCowBacked()` true; cached `residentBytes()` is 0 (Private_Dirty 0 on the source); per-frame payload detach + buffer `ensureExclusive` produces a fresh impl pointer, exactly the property TPG burn-in trades on.

**Behaviour change:**

- `TpgAllocator` (subclass of `MediaIOAllocator`) overrides `allocateVideoPlane` to hand out `Buffer(planeBytes, align, MemSpace::SystemCow)`. Audio + bytes fall through to default.
- `cachedPayload()` populates the cache through this allocator. After the builder lambda finishes filling the cached payload, `cachedPayload` calls `BufferView::seal()` on the payload's view. This is the deterministic "populate done" point. Calling `seal()` on non-SystemCow buffers (default allocator path) is a no-op success — callers don't need to know which allocator produced the slot.
- On the per-frame path, `slot->modify()` followed by `uvp->ensureExclusive()` (`src/proav/tpgmediaio.cpp:447–448`) triggers `_promeki_clone()` on each underlying `MemfdBufferImpl`. The impls are already sealed, so cloning is just the cheap `MAP_PRIVATE` mmap. Per-frame clones share the sealed memfd at zero physical-page cost until they dirty pages via `applyBurn`.
- `applyBurn` writes only the burn-in band → only those pages CoW.

**Verification:**

- [x] Existing TPG functional tests pass unchanged — the full unit-test suite (5286 cases / ~107k assertions) is green with the SystemCow path on; no regressions in `videotestpattern.cpp`, `tpg*.cpp`, or pipeline tests that build on top of TPG.
- [ ] **Per-frame profile** — flame-graph capture is a manual measurement; the unit test verifies the *property* (per-frame `ensureExclusive` produces a fresh impl, content matches, isCowBacked stays true) but doesn't replace a real perf run. Capture profile data under `promeki-test`'s TPG benchmark when you do the perf validation pass.
- [x] Burn-in pixel correctness unchanged (the existing `videotestpattern.cpp` test cases for ColorBars / AvSync / Burn coverage continue to pass with SystemCow installed).
- [x] **Resident-bytes regression check** — `VideoTestPattern_SystemCow_PerFrameDetachIsCheap` verifies the cached slot's `residentBytes()` is 0 (Private_Dirty 0 on the MAP_PRIVATE source view) immediately after seal. A multi-frame perf-style run that samples residency on in-flight clones is a follow-up `promeki-test` benchmark; the property the perf path trades on is locked in by the unit test and the Phase A `MemfdRegion` page-CoW measurement test.

**Wiring `cachedPayload` to use the allocator.** `VideoTestPattern::cachedPayload()` currently calls `UncompressedVideoPayload::allocate(desc)` directly. To inject the allocator without threading a parameter through every caller, add an optional `MediaIOAllocator::Ptr` member on `VideoTestPattern` (set by `TpgMediaIO`); `cachedPayload` uses it when valid, falls back to default otherwise. Same pattern likely fits any class that internally allocates payloads on behalf of a MediaIO (BurnMediaIO's compositor cache, InspectorMediaIO's scratch).

**Adding `BufferView::seal()`** ships in Phase B alongside the `Buffer::seal()` API (covered in B.1 / B.10), so TPG's "seal at end of populate" call is a one-liner.

---

## Phase E — Backend rollout roadmap

Each entry below is a separate follow-up landing — none of them block the SystemCow / TPG slice. Listed in priority order based on impact-to-effort ratio. Several entries depend on new `MemSpace` registrations that are themselves real pieces of infrastructure; those prerequisites are spelled out first.

### E.0 — Prerequisite MemSpace work

The allocator framework (Phase C) is the seam through which backends pick a `MemSpace`. The MemSpaces themselves are not free — each new ID needs a `BufferImpl` subclass, a factory registration, stats wiring, and a clean shutdown path. None of these block Phase D (TPG only needs `SystemCow`, which Phase B introduces). They block the corresponding E-phase below.

| MemSpace          | Backend impl                                    | Required by | Notes / Status                                          |
| :---------------- | :---------------------------------------------- | :---------- | :------------------------------------------------------ |
| `SystemCow`       | `MemfdBufferImpl` (Phase B)                     | E6 (Burn)   | LANDED. Linux only; falls back to `HostBufferImpl` elsewhere |
| `PinnedHost`      | `PinnedHostBufferImpl`; `mlock` + aligned alloc | E1 (NDI)    | **LANDED.** Runtime soft-fail on `RLIMIT_MEMLOCK` exhaustion (warning + unlocked allocation). CUDA escalation deferred — no consumer today. |
| `NumaHost`        | `NumaHostBufferImpl` + `NumaHost::forNode(int)`; lazy per-node MemSpace registration | E4, E5 (RTP TX/RX) | **LANDED.** Standalone `Numa` wrapper (no libnuma dep — direct `mmap` + `mbind` syscall); soft-fail on non-NUMA boxes (falls back to plain page-aligned host) and on `RLIMIT_MEMLOCK` exhaustion. |
| `CudaDevice`      | exists today                                    | E2 (NVDEC)  | No new MemSpace work; just allocator override            |
| `CudaPinnedHost`  | exists today (`MemSpace::CudaHost = 3`)         | E1 fallback | Already registered when CUDA is enabled                  |

Each prerequisite is itself a small but real chunk: a header + impl + factory registration + tests, plus careful failure handling (mlock can fail under RLIMIT_MEMLOCK; numa_alloc_onnode can return null on cgroup-restricted nodes). Treat these as their own landings, not as line items inside their dependent E-phase.

**E.0 NumaHost — landed files:**
- [x] `include/promeki/numa.h`, `src/core/numa.cpp` — standalone `Numa` static-method utility wrapping the kernel's NUMA syscalls + `/sys` topology lookups: `isAvailable()`, `nodeCount()`, `maxNode()`, `allocOnNode(bytes, node)`, `free(ptr, bytes)`, `nodeOfNic(iface)`, `nodeOfCpu(cpu)`, `currentNode()`. Linux uses `mmap` + raw `mbind` syscall; non-Linux platforms get a stub that always reports unavailable and falls back to `posix_memalign`. No libnuma dependency — total surface ~250 LoC.
- [x] `tests/unit/numa.cpp` — 12 test cases / 20 assertions: API surface (`isAvailable`, `nodeCount` / `maxNode` invariants), allocation round-trip with NodeAny, allocation on node 0 (always exists), zero-byte / null-ptr guards, NIC / CPU / current-node lookups (covers the UMA + NUMA paths via branching).
- [x] `include/promeki/numahostbufferimpl.h`, `src/core/numahostbufferimpl.cpp` — `NumaHostBufferImpl` subclass of `HostMappedBufferImpl`; `Numa::allocOnNode` + `secureLock` (mlock); `_promeki_clone()` re-runs the constructor for the clone so it lands on the same node. `NumaHost::forNode(int)` lazily registers a per-node `MemSpace::ID` (via `MemSpace::registerType`) with a factory closure that captures the node — first call per node creates the entry, subsequent calls return the cached MemSpace.
- [x] `include/promeki/memspace.h` — added `MemSpace::NumaHost = 6` (the default-node / `Numa::NodeAny` entry); per-specific-node IDs are dynamic and live above `UserDefined`.
- [x] `src/core/memspace.cpp` — registered the default `NumaHost` Ops (name + stats + host-to-host memcpy peer).
- [x] `src/core/bufferfactory.cpp` — registered the default `NumaHost` factory; **upgraded `BufferImplFactory` from a function pointer to `std::function`** so closure-capturing factories (the per-node NumaHost factories) work. Existing factory call sites (System, SystemSecure, SystemCow, PinnedHost, CudaDevice, CudaHost) are captureless lambdas that convert implicitly — no other code change required.
- [x] `tests/unit/numahostbufferimpl.cpp` — 8 test cases covering: default `NumaHost` MemSpace registration, alloc/write/read, copy-as-refcount + `ensureExclusive` deep-copy, `seal()` no-op (`isCowBacked` false), `forNode(negative)` → default ID collapse, `forNode(N)` per-node caching + IDs above `UserDefined` + names, `forNode(0)` round-trip, stats accounting.
- [x] CMakeLists wiring (header list, source list, test list).

**E.0 PinnedHost — landed files:**
- [x] `include/promeki/pinnedhostbufferimpl.h`, `src/core/pinnedhostbufferimpl.cpp` — `PinnedHostBufferImpl` subclass of `HostMappedBufferImpl`; `aligned_alloc` then `secureLock` (mlock + `MADV_DONTDUMP` on Linux, `VirtualLock` on Windows). On lock failure (typically `RLIMIT_MEMLOCK` exhausted or missing `CAP_IPC_LOCK`) the impl warns and keeps the unlocked allocation — the Buffer is still valid, just not pinned. `_promeki_clone()` deep-copies into a fresh pinned region.
- [x] `include/promeki/memspace.h` — added `MemSpace::PinnedHost = 5` (next sequential after `SystemCow = 4`); documents the soft-fail semantics and the distinction from `CudaHost` (mlock-pinned vs cuda-pinned + GPU-DMA-mapped).
- [x] `src/core/memspace.cpp` — registered the `PinnedHost` Ops (name + stats); real allocation lives on `PinnedHostBufferImpl` (mirrors the `SystemCow` Ops shape).
- [x] `src/core/bufferfactory.cpp` — registered the `PinnedHost` BufferImpl factory.
- [x] `tests/unit/pinnedhostbufferimpl.cpp` — 6 test cases / 31 assertions: registration, construct/write/read, copy-as-refcount + `ensureExclusive` deep-copy, `seal()` is a no-op (`isCowBacked == false`), zero-byte allocation surface, stats round-trip on alloc/release.
- [x] CMakeLists wiring (header list, source list, test list).

### E1 — NDI (DMA-pinned host) — **LANDED**

**Win:** producer feeding `writeFrame` pre-allocates into pinned host space; the NDI SDK's submit avoids a staging copy on ingress. Symmetrically, NDI's receive path now lands its frame copy in pinned host so downstream DMA consumers (NVENC, NVDEC, RTP TX) can DMA off the region without re-staging.

**Depends on:** `MemSpace::PinnedHost` (E.0) — landed.

**Files:**
- [x] `src/proav/ndimediaio.cpp` — `NdiAllocator` (private to the .cpp; subclass of `MediaIOAllocator`) overrides `allocateVideoPlane` / `allocateAudioChunk` / `allocateBytes` to vend `MemSpace::PinnedHost` buffers. Stateless; trivially thread-safe. Public factory `NdiMediaIO::makePinnedHostAllocator()` returns a fresh instance for callers that want to install the same policy on other MediaIOs.
- [x] `include/promeki/ndimediaio.h` — added `makePinnedHostAllocator()` static factory; pulled in `mediaioallocator.h`.
- [x] `src/proav/ndimediaio.cpp` `executeCmd(Open)` — installs the allocator at the end of open via `setAllocator(makePinnedHostAllocator())`. Documented as the rollback point for the policy if it ever needs reverting in production (comment-out one line).
- [x] `src/proav/ndimediaio.cpp` `captureLoop` — receive-side video allocation now routes through `allocator()->allocateBytes(totalBytes)` (was `Buffer(totalBytes)`). Allocation failure logs an error and increments `_droppedReceives` rather than crashing.
- [x] `src/proav/ndimediaio.cpp` `executeCmd(Read)` audio drain — same allocator routing for the audio drain Buffer; alloc failure logs and skips the drain for the current read (audio gap is recoverable on the next pop).
- [x] `tests/unit/ndimediaio.cpp` — 4 new test cases / 27 assertions covering: `NdiAllocator` per-plane PinnedHost placement (UYVY single-plane + NV12 dual-plane); audio chunk PinnedHost placement (size correctness, zero-sample handling); `allocateBytes` PinnedHost + logical-size correctness; full `UncompressedVideoPayload` via inherited `allocateVideoPayload` walks both planes and confirms each landed in PinnedHost.

**Allocator semantics:**
- `NdiAllocator::allocateVideoPlane` / `allocateAudioChunk` / `allocateBytes` all construct `Buffer(bytes, Buffer::DefaultAlign, MemSpace(MemSpace::PinnedHost))` and stamp the logical size to match the request (so audio chunks and byte buffers are "filled" by allocator convention).
- Inherited `MediaIOAllocator::allocateVideoPayload` walks plane indices and assembles the `BufferView` — same code path as the default allocator, just with PinnedHost backing.
- The allocator is installed unconditionally on every `NdiMediaIO::executeCmd(Open)`. Callers that want to opt out can `setAllocator(MediaIOAllocator::Ptr())` after open to revert to the default heap allocator.

**Downstream behaviour:**
- Sink mode: an upstream stage that asks for `port->allocator()` lands its planes in PinnedHost. The NDI SDK's `send_send_video_v2` reads directly out of pinned memory; no staging copy on ingress.
- Source mode: each received frame's bytes land in PinnedHost. Downstream consumers that DMA the host buffer (NVENC, RTP TX, anything Cuda-aware) skip the staging-pin step they'd otherwise need.
- On builds without working `mlock` (or under exhausted `RLIMIT_MEMLOCK`), `PinnedHostBufferImpl` falls back to plain heap with a warning (see E.0). Allocations still succeed; they just lose the DMA-pin benefit. No code change needed at any caller.

**Cross-stage negotiation (Open Q 1):** still out of scope. NDI's allocator is installed at open time; downstream stages don't drive it. When cross-stage negotiation lands later, NDI just becomes another participant.

**Verification:**
- [x] Existing NDI test paths continue to work — full unit-test suite (5308 cases / 108k assertions) is green with the new allocator on; no regressions in `ndimediaio.cpp`, `pcmaudiopayload.cpp`, or any pipeline tests that build on top of NDI.
- [ ] **Live end-to-end pinned-DMA validation** — manual measurement with a real NDI sender/receiver pair under load is a follow-up perf pass. The unit tests verify the *property* (each receive lands in PinnedHost; the allocator vends PinnedHost on every primitive), but a wire-rate copy-elimination measurement requires the SDK's perf instrumentation and a representative workload. Capture under `promeki-test`'s NDI benchmark when you do the perf run.

### E2 — NVDEC (device-resident video) — **LANDED**

**Win:** decoder output stays on device through downstream consumers; host bounce eliminated when the device-resident allocator is installed.

**Correction to earlier scoping:** the original plan claimed "Today `NvdecVideoDecoder` allocates CUDA Buffers explicitly" — that was wrong. Pre-E2, `nvdecvideodecoder.cpp:782` allocated *host* memory via `UncompressedVideoPayload::allocate(desc)` and copied each frame down with `cudaMemcpy2D(DeviceToHost)`. E2's actual job was twofold: introduce the allocator seam on `VideoDecoder` (it isn't a `MediaIO`, so there was no allocator hook), and add a device-resident option that callers opt into.

**Files:**
- [x] `include/promeki/videodecoder.h`, `src/proav/videodecoder.cpp` — add `MediaIOAllocator::Ptr allocator() const` (never returns null) + `virtual void setAllocator(MediaIOAllocator::Ptr)` on the base. Default behaviour unchanged: when nothing is installed, `allocator()` returns `MediaIOAllocator::defaultAllocator()` and existing call sites see no change.
- [x] `include/promeki/nvdecvideodecoder.h`, `src/proav/nvdecvideodecoder.cpp` — `NvdecAllocator` (private to the .cpp; subclass of `MediaIOAllocator`) overrides `allocateVideoPlane` to vend `MemSpace::CudaDevice` planes; audio + bytes fall through. Public factory `NvdecVideoDecoder::makeDeviceResidentAllocator()` returns a fresh instance for the caller to install.
- [x] `nvdecvideodecoder.cpp` — `Impl` now holds an `NvdecVideoDecoder &_outer` back-reference (UniquePtr ownership keeps the outer alive across the Impl). The `handleDisplay` path replaces `UncompressedVideoPayload::allocate(desc)` with `_outer.allocator()->allocateVideoPayload(desc)` and branches the per-plane `cudaMemcpy2D` between `DeviceToHost` (default System path) and `DeviceToDevice` (CudaDevice path) keyed on the plane's `MemSpace`. The `resolveCudaEndpoint(BufferView::Entry)` helper unifies host pointer / device pointer extraction (mirrors the cudaCopy idiom in `core/cuda.cpp` but accounts for slice offsets).
- [x] Header docstring updated — drops the "System-memory output" hard-limit line and documents the per-decoder placement choice via `setAllocator`.

**Allocator semantics:**
- `NvdecAllocator::allocateVideoPlane` constructs `Buffer(planeBytes, Buffer::DefaultAlign, MemSpace::CudaDevice)`. Inheriting `MediaIOAllocator::allocateVideoPayload` walks plane indices and assembles the `BufferView` — same code path as the default allocator, just with a different `MemSpace`.
- Audio/bytes fall through to the inherited default — NVDEC only cares about its emitted video planes.
- Stateless; trivially thread-safe.

**Downstream behaviour:**
- With the default allocator (no `setAllocator` call), the emitted `UncompressedVideoPayload` planes are System-memory and consumers (SDL, ImageFile writers, downstream CSC) work exactly as before.
- With `makeDeviceResidentAllocator()` installed, the emitted planes are CudaDevice. Consumers that need host memory hit the registered `MemSpace::CudaDevice → System` `cudaCopy` on first `Buffer::data()` / `Buffer::copyTo` access. Behaviour-preserving for callers that route through `Buffer::copyTo`; callers that read `Buffer::data()` directly without checking `memSpace()` will get `nullptr` (the current contract for non-host-mapped buffers) — they need to be updated, but only if they opt into the device-resident path.

**Cross-stage negotiation (Open Q 1):** still out of scope. The decoder's allocator is set by the caller / pipeline; downstream stages don't drive it. When cross-stage negotiation lands later, NVDEC just becomes another participant.

**Verification:**
- [x] Existing NVDEC test paths continue to work with the default allocator (host output preserved).
- [x] New unit-test cases in `tests/unit/nvdecvideodecoder.cpp` exercise both paths — default allocator producing System planes; `makeDeviceResidentAllocator` producing CudaDevice planes.

### E4 — RTP TX (NUMA-aligned pinned host)

**Win:** packet payload buffers live on the NIC's NUMA node; reduces cross-socket memory traffic on multi-socket boxes.

**Depends on:** `MemSpace::NumaHost` (E.0).

- [ ] `RtpTxAllocator` queries the NIC's NUMA node (already plumbed for `RtpPacingMode::TxTime` follow-up — see `proav/optimization.md`) and constructs `MemSpace::NumaHost` keyed on that node ID.
- [ ] Coordinates with the existing NUMA-aware threading work on the RTP TX path.

### E5 — RTP RX

**Win:** symmetric to E4. Receive scratch lives on the NIC's NUMA node; descriptor delivery to downstream stages avoids cross-socket reads.

**Depends on:** `MemSpace::NumaHost` (E.0); shipped together with E4.

- [ ] Same allocator class as E4, configured with the RX NIC's NUMA node.

### E6 — BurnMediaIO — **LANDED (no code change required)**

**Win:** the same CoW story as TPG when the upstream is SystemCow-aware. BurnMediaIO composites a per-frame band onto an upstream frame; the burn path runs `payloadPtr.modify()` + `uvp->ensureExclusive()` (`src/proav/burnmediaio.cpp:154,165`), which is exactly the chain that page-CoWs through `MemfdBufferImpl` when the upstream allocator placed the buffer in `MemSpace::SystemCow`.

**Resolution:** BurnMediaIO is a Transform-mode passthrough — it mutates an existing payload, it doesn't allocate the payload itself. The win comes from the *upstream* using SystemCow; BurnMediaIO doesn't need its own allocator override. The CoW benefit propagates automatically through `Buffer::ensureExclusive` whenever the upstream allocator is SystemCow-aware (today: `TpgMediaIO`'s default; tomorrow: any source that opts into SystemCow placement).

- [x] No allocator override on `BurnMediaIO`.
- [x] Doxygen "@par Allocator policy" section added to `include/promeki/burnmediaio.h` documenting (a) why no override is installed, (b) that the CoW win flows from the upstream, and (c) the operator-visible trade-off DMA-friendly upstream allocators force on the burn path (no page-level CoW benefit when the upstream picks pinned / page-aligned pool placement).
- [x] The trade-off "fast detach (SystemCow) vs fast DMA (pinned)" is documented at the upstream allocator's install site (i.e. NDI will document it on its `setAllocator` call when E1 lands); the BurnMediaIO doc points readers there rather than restating it.

### E7 — File backends (PNG / DPX / SGI / RAW YUV)

**Win:** none expected. File I/O is bandwidth-bound regardless of allocation strategy. Keep on default allocator.

- Documented as "no override planned" so we don't churn these later by mistake.

### E8 — InspectorMediaIO

**Win:** marginal. Inspector mostly observes; its allocations are short-lived scratch.

- Default allocator unless profile data motivates otherwise.

---

## Open design questions

These are explicitly *not* answered by this plan; flag them when revisiting.

1. **Cross-stage allocator negotiation.** Today the consumer's allocator decides allocation only when the consumer itself is doing the allocation. A pipeline graph that wants "CSC output lands in the sink's preferred space" needs the planner / scheduler to consult the downstream allocator. Out of scope for v1; document on `BufferAllocator` that v1 is consumer-allocates-its-own.
2. **Tiered pools.** `DefaultBufferAllocator` is stateless. A `PooledBufferAllocator` mixin could provide pool plumbing for any backend that wants it. Defer until two backends ask for the same shape — RTP TX/RX (E4/E5) might be the trigger.
3. **Cross-process CoW.** `MemfdRegion::fd()` and `readOnlyView()` cover the in-process side of IPC. The actual fd-passing wire protocol (Unix socket SCM_RIGHTS, attach helpers, lifetime arbitration) is not built — but the `MemfdRegion` API is shaped so it slots in cleanly when needed.
4. **Non-Linux `MemfdRegion`.** macOS could be added via `vm_remap(VM_INHERIT_COPY)` and Windows via section objects with `SEC_COMMIT`. Both are real options; both wait for a real port target.
5. **Allocator selection at the planner level.** Phase E6 (BurnMediaIO) raises the question of "fast detach (SystemCow) vs fast DMA (pinned)" when both axes apply. v1 picks one per backend. A future refinement could let the planner stitch stages together with explicit boundary copies when policies conflict; not designed yet.

(Resident-bytes telemetry was previously listed as deferred; B.7 now lands `peakResidentBytes` + `residentBytesSnapshot()` in v1.)

---

## Risks

- **Linux-only feature gating.** `PROMEKI_ENABLE_MEMFD` (matches the existing `PROMEKI_ENABLE_*` pattern in `include/promeki/config.h.in`) gates SystemCow's CoW backend. CMake auto-detects `memfd_create` + `MFD_ALLOW_SEALING` and defaults the flag ON for Linux, OFF elsewhere. Builds without memfd silently route SystemCow → `HostBufferImpl` (no CoW, just default heap). `Buffer::seal()` on the fallback is a no-op success; `isCowBacked()` returns false. Document the fallback behaviour clearly so callers aren't surprised when the optimisation evaporates on a non-Linux build.
- **fd exhaustion.** Each `MemfdRegion` owns one fd. Clones share via `SharedPtr<MemfdRegion>`, so the pressure scales with *distinct cached payloads*, not in-flight clones. A long-running media server with many cached backgrounds (multi-source TPG, BurnMediaIO chains, channel-routing matrices) can still brush against `RLIMIT_NOFILE`. Mitigation: per-allocator audit at design time ("how many distinct memfds at steady state?"), and an opt-in stats-tab that surfaces the live count via `MemSpace::Stats`.
- **Stale `data()` pointers across seal.** When `Buffer::seal()` (or the implicit seal-on-first-clone) runs, the source impl's `_hostPtr` is replaced. Callers that cached `data()` across the seal hold a dangling pointer. The contract is *always re-fetch `data()`*, which libpromeki call sites already follow. Document this prominently on `MemSpace::SystemCow` and on `Buffer::seal()`.
- **Concurrent reads during seal.** Stricter contract on SystemCow than other backends: readers MUST NOT race with `seal()` / `ensureExclusive()`. `Buffer::isCowBacked()` lets generic code branch on the constraint without a downcast. Document on the MemSpace, on `Buffer::seal()`, and on `MemfdBufferImpl`. The TPG / Burn use cases are single-threaded across the seal boundary.
- **`seal()` failure leaves the impl dead.** If `_region->seal()` fails (e.g. `Error::Busy` from outstanding writable mappings, or `OutOfMemory` on the atomic clone), `_hostPtr` is null and the impl is unusable. Documented as "discard the buffer." Production callers should treat any seal-error path as a re-allocate trigger; emit a high-severity log entry so operators see it.
- **Allocator scope creep.** Resist adding `allocateMetadata`, `allocateCaptions`, `allocateNalData`, `allocateSEI`. Three primitive kinds (video plane, audio chunk, generic bytes) plus the two full-payload conveniences are enough until a real consumer differentiates further.
- **Two backends wanting incompatible policies on the same MediaIO.** A hypothetical "burn pre-NDI-send" pipeline would want SystemCow (for the burn) *and* pinned (for the NDI submit). The allocator picks one. Practical answer: chain stages — burn into SystemCow, then a final stage copies into pinned for the NDI submit. The single copy on the boundary is unavoidable until cross-stage negotiation lands (Open Q 1); the wins still apply within each stage. **Set operator expectations:** SystemCow will not save you 500 MB/s if the next stage forces pinned — it'll save half.
- **Prerequisite MemSpace work hidden in E.0.** The `PinnedHost` and `NumaHost` MemSpaces look like one-line additions but are real infrastructure (mlock/RLIMIT_MEMLOCK handling, libnuma integration, factory keying). They have to land before the dependent backend phases. Don't conflate.
- **Rollback story.** If SystemCow regresses TPG in production, the smallest revert is to remove the `setAllocator(...)` call from `TpgMediaIO::executeCmd(Open)` — reverts placement to default heap without removing any of the new infrastructure. Same for any future backend: the allocator install line is the rollback unit. State this explicitly in the Phase D doc-comment so ops can act on it without reading this plan.

---

## Files affected (full list)

**New:**
- `include/promeki/memfdregion.h`, `src/core/memfdregion.cpp`, `tests/memfdregion.cpp`
- `include/promeki/memfdbufferimpl.h`, `src/core/memfdbufferimpl.cpp`, `tests/memfdbufferimpl.cpp`
- `include/promeki/bufferallocator.h`, `src/core/bufferallocator.cpp`, `tests/bufferallocator.cpp`
- `include/promeki/mediaioallocator.h`, `src/proav/mediaioallocator.cpp`, `tests/mediaioallocator.cpp`

**Modified — core (Phase B):**
- `include/promeki/bufferimpl.h` — add `virtual Error seal() const`, `virtual size_t residentBytes() const`, `virtual bool isCowBacked() const` (defaults: Ok, allocSize(), false).
- `include/promeki/buffer.h` — add `[[nodiscard]] Error seal() const`, `bool isShared() const`, `bool isCowBacked() const` forwarders.
- `include/promeki/bufferview.h`, `src/core/bufferview.cpp` — add `[[nodiscard]] Error seal() const` walking unique buffers (mirrors `ensureExclusive`).
- `include/promeki/memspace.h` — add `SystemCow = 4` to built-in `enum ID` (after `CudaHost = 3`); document concurrent-access contract; add `peakResidentBytes` to `Stats`; add `residentBytesSnapshot()` accessor.
- `src/core/memspace.cpp` — wire residentBytes peak update on impl destruction; implement `residentBytesSnapshot()`.
- `src/core/bufferfactory.cpp` — register SystemCow factory, gated on `PROMEKI_ENABLE_MEMFD`.
- `cmake/` — detect `memfd_create` + `MFD_ALLOW_SEALING`; set `PROMEKI_ENABLE_MEMFD` (default ON for Linux, OFF elsewhere).
- `include/promeki/config.h.in` — add `PROMEKI_ENABLE_MEMFD` line alongside the existing `PROMEKI_ENABLE_*` flags.

**Modified — MediaIO + payload (Phase C):**
- `include/promeki/mediaio.h`, `src/proav/mediaio.cpp` — `MediaIOAllocator::Ptr allocator() const` (never returns null) + `void setAllocator(Ptr)` (null clears).
- `include/promeki/mediaioport.h`, `src/proav/mediaioport.cpp` — read-only `allocator()` accessor delegating to MediaIO. Per-port setter deferred to v2.
- `src/proav/uncompressedvideopayload.cpp` — route `allocate(desc)` through `MediaIOAllocator::defaultAllocator()`.
- `src/proav/uncompressedaudiopayload.cpp` — same for audio.

**Modified — TPG (Phase D):**
- `src/proav/tpgmediaio.cpp` — install `TpgAllocator` in `executeCmd(Open)`; document the allocator install line as the rollback point for the SystemCow optimisation.
- `include/promeki/videotestpattern.h`, `src/proav/videotestpattern.cpp` — `MediaIOAllocator::Ptr` member; route `cachedPayload`; call `BufferView::seal()` at end of populate.

**Backend rollout (per-phase landings):**
- E.0 (PinnedHost) — **LANDED**: `include/promeki/pinnedhostbufferimpl.h` + impl + 6 tests; `MemSpace::PinnedHost = 5` registered in `memspace.h` / `memspace.cpp` / `bufferfactory.cpp`.
- E.0 (NumaHost) — **LANDED**: `include/promeki/numahostbufferimpl.h` + impl + 8 tests; `MemSpace::NumaHost = 6` registered; `Numa` static utility (no libnuma — direct `mmap` + `mbind`); `NumaHost::forNode(int)` lazy per-node MemSpace registry; `BufferImplFactory` upgraded to `std::function` for closure-capturing factories.
- E1 — **LANDED**: `src/proav/ndimediaio.cpp` — `NdiAllocator` + `makePinnedHostAllocator()` factory; receive-side video + audio routing through allocator; 4 new test cases.
- E2 — **LANDED**: `src/proav/nvdecvideodecoder.cpp` — `NvdecAllocator` + `makeDeviceResidentAllocator()` factory; `cudaMemcpy2D` direction branch on plane MemSpace.
- E4–E5 — **PENDING**: `src/proav/rtpmediaio.cpp` — `RtpTxAllocator` / `RtpRxAllocator` (depends on NumaHost).

---

## Suggested landing order

1. **Phase A** (`MemfdRegion`) — standalone, fully testable in isolation. Useful as a general-purpose CoW shared-memory primitive even if Phase B never lands.
2. **Phase B** in two sub-steps:
   - B.1 — `BufferImpl::seal()` / `residentBytes()` / `isCowBacked()` virtuals + `Buffer::seal()` / `isShared()` / `isCowBacked()` + `BufferView::seal()` + `MemSpace::Stats` resident-bytes accounting. All no-op success / `allocSize()` / false on existing backends; pure additive change.
   - B.2 — `MemfdBufferImpl` + `MemSpace::SystemCow` registration; the seal hook now does real work; `residentBytes()` returns mincore'd resident pages.
   Splitting B.1 from B.2 means the generic seal/residentBytes API ships first and is exercised by the existing test suite (every backend's no-op default gets a test pass; resident accounting starts surfacing in operator dashboards immediately).
3. **Phase C** in two sub-steps:
   - C.1 — `BufferAllocator` (core) + `DefaultBufferAllocator`. Independent of A/B; useful on its own for any backend that wants placement control without requiring SystemCow or sealing.
   - C.2 — `MediaIOAllocator` (proav) + integration with `MediaIO` + routing default `Payload::allocate`.
4. **Phase D** (TPG proof of concept) — end-to-end validation of A + B + C through one backend; locks in the `residentBytes` regression test.
5. **Phase E.0** prerequisites and **Phase E1+** backend rollout, each as its own independent landing in priority order.

Each phase is independently shippable: A is a standalone primitive; B.1 is purely additive (no-op everywhere existing); B.2 + Phase C give the allocator its first real consumer (D); Phase E phases are gated on whichever E.0 prerequisite they depend on.
