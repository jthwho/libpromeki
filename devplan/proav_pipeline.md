# ProAV Pipeline Framework Core

**Phase:** 4A
**Dependencies:** Phase 1 (Mutex, WaitCondition, Future, PriorityQueue, ThreadPool), Phase 2 (IODevice)
**Library:** `promeki-proav` (extends existing library)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Generalizes the existing source/sink pattern in AudioBlock. Nodes own their own threads; the pipeline manages topology and lifecycle.

---

## Progress

**Completed:** MediaSink, MediaSource, MediaNodeConfig (including typed `set<T>()`/`get<T>()` template overloads and string-literal overloads), MediaPipelineConfig, MediaNode (including node registry, NodeStats, BuildResult, Severity, NodeMessage, Delivery/DeliveryList, processFrame/process split, wake/waitForWork threading, PROMEKI_REGISTER_NODE macro, benchmark infrastructure, `defaultConfig()` virtual method), MediaPipeline (including BuildError, topological sort, cycle detection, build-from-config, start/stop/pause/resume, benchmarkSummary), Benchmark, BenchmarkReporter, EncodedDesc, Image::ensureExclusive()/isExclusive(), Audio::convertTo() (sample format conversion), concrete nodes (TestPatternNode, FrameDemuxNode, JpegEncoderNode, TimecodeOverlayNode, RtpVideoSinkNode, RtpAudioSinkNode — all implement `defaultConfig()`). UpperCamelCase key naming convention applied uniformly across all nodes and their config options, extendedStats(), and documentation. See git history for details.

**Architecture:** The pipeline uses a direct MediaSink/MediaSource connection model. MediaNode owns its sinks and sources; MediaPipeline owns nodes and manages connections. All node configuration flows through MediaNodeConfig → MediaNode::build() (pure virtual, returns BuildResult). Concrete nodes override `processFrame()` — the base class `process()` handles dequeuing, benchmark stamping, delivery, and timing. The old MediaPort/MediaLink/MediaGraph abstraction layer was removed.

**String key naming convention:** All string keys in `Map<String, Variant>` dictionaries use UpperCamelCase (CamelCaps), starting with an upper-case letter. This applies to config option keys (`Name`, `Type`, `FrameRate`, `PayloadType`), `extendedStats()` keys (`PacketsSent`, `BytesSent`, `FramesGenerated`), and any other string-keyed maps in the node API. Acronyms are treated as single words with only the first letter capitalised: `Dscp`, `RtpPayload`, `LtcChannel`.

**Remaining:**
- Audio::ensureExclusive() / Audio::isExclusive() — not yet implemented (Image has it)
- MemSpace::Stats — not yet implemented
- MemSpacePool — not yet implemented

---

### Design Constraint: JSON-Serializable Graphs

An eventual goal is defining media pipelines via a data structure that can be converted to/from JSON. This is **not** an initial deliverable, but it must influence how we build things now. The node type registry and MediaNodeConfig options map are already in place to support this.

**What this enables later (not now):**
- `MediaPipelineConfig::toJson()` / `MediaPipelineConfig::fromJson()` — serialize the full pipeline config
- A JSON document fully describes a pipeline that can be instantiated at runtime
- External tools (GUIs, web interfaces) can construct pipelines without C++ code
- Pipeline templates / presets stored as JSON files

**What we do NOT build now:**
- The actual `toJson()`/`fromJson()` methods
- JSON schema definition
- Any graph editor or external tooling
- Variant conversions for complex types (AudioDesc, VideoDesc, etc. — these can be added incrementally when JSON serialization is implemented)

---

### Design Constraint: Copy-on-Write Mutation Safety

In a pipelined system with queues and fan-out, multiple nodes may hold references to the same `Buffer::Ptr` simultaneously. Mutating a shared buffer would corrupt data for other consumers.

**Solution: Leverage existing SharedPtr COW + `ensureExclusive()` on Image/Audio.**

`SharedPtr` already implements copy-on-write via `modify()` / `detach()`. The gap is that `Image::Ptr::modify()` clones the Image object but the underlying `Buffer::Ptr` plane data still points to the same memory. Image already has `ensureExclusive()` for deep detach; Audio still needs it (see remaining work below).

**Audio additions:**
- [ ] `bool isExclusive() const` — returns true if the buffer has `referenceCount() <= 1`
- [ ] `void ensureExclusive()` — calls `_buffer.modify()` to trigger COW detach if shared

**Convention (enforced by code review):**
- **Source nodes** allocate fresh buffers and render into them.
- **Read-only nodes** (RTP sinks, analyzers) never write to image/audio buffers.
- **Mutating nodes** call `ensureExclusive()` before modifying buffer data (no-op in linear pipelines).
- **Fan-out** delivers the same `Frame::Ptr` to all sinks — no copy. Mutating consumers detach via `ensureExclusive()`.

**Buffer allocation and the MemSpace layer:**

All buffer allocation (including COW clones) goes through the buffer's `MemSpace`. The Buffer copy constructor allocates from the source's MemSpace, so COW clones inherit the same allocation strategy. Buffer pooling is implemented as a `MemSpace` — not as a pipeline-level concept.

See the MemSpace Statistics and MemSpacePool sections below for the allocation-level enhancements.

---

### MemSpace Statistics

MemSpace currently provides allocation, release, copy, and fill operations but no visibility into allocation behavior. For pipeline debugging and performance analysis, per-space statistics are essential.

**Files:** existing `include/promeki/core/memspace.h`, `src/memspace.cpp`

**Statistics struct:**
- [ ] `struct MemSpace::Stats` — snapshot of allocation statistics for a memory space:
  - [ ] `uint64_t allocCount` — total allocations since reset
  - [ ] `uint64_t releaseCount` — total releases since reset
  - [ ] `uint64_t allocBytes` — total bytes allocated since reset
  - [ ] `uint64_t releaseBytes` — total bytes released since reset
  - [ ] `uint64_t activeCount` — currently active allocations (`allocCount - releaseCount`)
  - [ ] `uint64_t activeBytes` — currently active bytes (`allocBytes - releaseBytes`)
  - [ ] `uint64_t peakCount` — peak simultaneous active allocations
  - [ ] `uint64_t peakBytes` — peak simultaneous active bytes
  - [ ] `uint64_t cowDetachCount` — COW detach copies triggered (tracked by Buffer clone path)
  - [ ] `uint64_t cowDetachBytes` — bytes copied due to COW detach

**MemSpace API additions:**
- [ ] `Stats stats() const` — return current statistics for this memory space ID
- [ ] `static Stats stats(ID id)` — return statistics for a specific space
- [ ] `static void resetStats(ID id)` — reset counters for a space
- [ ] `static void resetAllStats()` — reset all spaces

**Implementation:**
- [ ] Per-ID atomic counters (alongside existing `StructDatabase<ID, Ops>`)
- [ ] `alloc()` increments allocCount, allocBytes, updates activeCount/activeBytes/peaks
- [ ] `release()` increments releaseCount, releaseBytes, decrements activeCount/activeBytes
- [ ] Peak tracking: `peakCount = max(peakCount, activeCount)` (atomic compare-exchange)
- [ ] COW tracking: Buffer's `_promeki_clone()` increments cowDetachCount/cowDetachBytes on the source MemSpace

**Doctest:**
- [ ] Allocate N buffers, verify allocCount/allocBytes
- [ ] Release some, verify releaseCount/activeCount
- [ ] Verify peak tracking across alloc/release cycles
- [ ] Reset stats, verify zeroed
- [ ] COW detach: create shared Buffer::Ptr, call modify(), verify cowDetachCount increments

---

### MemSpacePool — Pooling Memory Space

A MemSpace implementation that recycles fixed-size allocations instead of going to the system allocator on every alloc/release. Buffers allocated from a pool MemSpace are transparently recycled — Buffer, Image, and Audio never know the difference. COW clones of pool-allocated buffers also come from the pool (since Buffer's copy constructor uses the source's MemSpace).

**Files:**
- [ ] `include/promeki/core/memspacepool.h`
- [ ] `src/memspacepool.cpp`
- [ ] `tests/memspacepool.cpp`

**Implementation checklist:**
- [ ] `MemSpacePool(size_t bufferSize, size_t align = Buffer::DefaultAlign, int preallocate = 0)` — create a pool for buffers of a fixed size. Optionally pre-allocate `preallocate` buffers.
- [ ] `MemSpace memSpace() const` — returns the MemSpace that routes through this pool. Pass this to Buffer/Image/Audio constructors.
- [ ] Pool `alloc()`: return a recycled buffer if available (LIFO stack for cache locality), otherwise allocate from system. Size must match pool's bufferSize (assert on mismatch — pool is for fixed-size buffers).
- [ ] Pool `release()`: push buffer back to recycle stack instead of freeing. Optionally cap recycle stack size to bound memory.
- [ ] `void setMaxRecycled(int max)` — cap on recycled buffers held (0 = unlimited, default: unlimited). Excess buffers are freed to the system.
- [ ] `int recycledCount() const` — buffers currently in the recycle stack
- [ ] `size_t bufferSize() const` — the fixed buffer size this pool manages
- [ ] Thread-safe: lock-free recycle stack (atomic LIFO) for concurrent alloc/release
- [ ] Inherits MemSpace statistics automatically (alloc/release counters track pool hits and system fallbacks)
- [ ] Pool-specific statistics (in addition to base MemSpace::Stats):
  - [ ] `uint64_t poolHitCount` — allocations served from recycle stack
  - [ ] `uint64_t poolMissCount` — allocations that required system alloc (pool empty)

**MemSpace extensibility:**
- [ ] Add `MemSpace(const Ops *ops)` constructor — allows custom MemSpace implementations to provide their own Ops table without registering a global ID
- [ ] MemSpacePool creates its own Ops struct with custom alloc/release that route through the pool, and copy/fill/isHostAccessible that delegate to System

**Pipeline usage pattern:**
```cpp
// During pipeline build():
MemSpacePool videoPool(imageDesc.planeSize(0), Buffer::DefaultAlign, 4);  // pre-allocate 4
MemSpacePool audioPool(audioDesc.bytesPerFrame(), Buffer::DefaultAlign, 4);

// Source node allocates from pool:
Image img(desc, videoPool.memSpace());

// Mutating node's ensureExclusive() triggers COW → clone allocates from same pool
img->ensureExclusive();  // if copy needed, new buffer comes from videoPool
```

**Doctest:**
- [ ] Create pool, allocate buffer, release, allocate again — verify same pointer (recycled)
- [ ] Verify pool hit/miss statistics
- [ ] Exceed pool → verify system fallback works
- [ ] COW detach from pool buffer → verify clone comes from same pool
- [ ] maxRecycled cap: verify excess buffers are freed
- [ ] Concurrent alloc/release from multiple threads

---

## EncodedDesc

Data object describing compressed/encoded media. Analogous to ImageDesc/AudioDesc but for encoded bitstreams.

**Files:**
- [ ] `include/promeki/proav/encodeddesc.h`
- [ ] `src/encodeddesc.cpp`
- [ ] `tests/encodeddesc.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] `FourCC codec() const` — codec identifier (e.g., "JPEG", "H264", "HEVC")
- [ ] `void setCodec(const FourCC &codec)`
- [ ] `ImageDesc sourceImageDesc() const` — the uncompressed image format this was encoded from
- [ ] `void setSourceImageDesc(const ImageDesc &desc)`
- [ ] `int quality() const` — codec-specific quality parameter (e.g., JPEG quality 1-100). -1 if not applicable.
- [ ] `void setQuality(int q)`
- [ ] `Metadata metadata() const`, `setMetadata(const Metadata &)` — additional codec parameters
- [ ] `bool isValid() const`
- [ ] `operator==`, `operator!=`
- [ ] Doctest: construction, codec/quality set/get, equality
