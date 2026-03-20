# ProAV Pipeline Framework Core

**Phase:** 4A
**Dependencies:** Phase 1 (Mutex, WaitCondition, Future, PriorityQueue, ThreadPool), Phase 2 (IODevice)
**Library:** `promeki-proav` (extends existing library)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Generalizes the existing source/sink pattern in AudioBlock. Default threading model: thread pool. Nodes can override via `setThreadingPolicy()`.

---

## Progress

**Completed:** MediaSink, MediaSource, MediaNodeConfig, MediaPipelineConfig, MediaNode (including node registry, NodeStats, BuildResult), MediaPipeline, EncodedDesc, Image::ensureExclusive()/isExclusive(), Audio::convertTo() (sample format conversion). See git history for details.

**Architecture note:** The original MediaPort/MediaLink/MediaGraph abstraction layer was replaced by a simpler MediaSink/MediaSource direct-connection model. MediaNode now owns its sinks and sources directly; MediaPipeline owns nodes and manages connections. All node configuration flows through MediaNodeConfig::build() (pure virtual, returns BuildResult). The property system and configure() virtual have been removed — all config goes through build().

**Remaining:**
- Audio::ensureExclusive() / Audio::isExclusive() — not yet implemented (Image has it)
- MemSpace::Stats — not yet implemented
- MemSpacePool — not yet implemented

Note: Individual checkboxes below have NOT been updated to reflect completion — refer to this progress section for current status.

---

### Design Constraint: JSON-Serializable Graphs

An eventual goal is defining media pipelines via a data structure that can be converted to/from JSON. This is **not** an initial deliverable, but it must influence how we build things now. Specifically:

**Node type registry:** Every concrete MediaNode subclass must be instantiable by type name string. MediaNode provides a static registration mechanism so that a graph builder can create nodes from a JSON description without knowing the concrete class at compile time.
- [ ] `static void registerNodeType(const String &typeName, std::function<MediaNode *()> factory)`
- [ ] `static MediaNode *createNode(const String &typeName)` — returns nullptr if type not registered
- [ ] `static List<String> registeredNodeTypes()`
- [ ] Macro `PROMEKI_REGISTER_NODE(ClassName)` — registers the class with its name at static init time (similar pattern to Variant type registration)
- [ ] Each concrete node must use `PROMEKI_REGISTER_NODE` in its .cpp file

**Node properties via Variant:** All configurable parameters on a node must be accessible through a uniform key-value interface so they can be driven from a data structure.
- [ ] `virtual Map<String, Variant> properties() const` — returns all configurable properties as key-value pairs
- [ ] `virtual Error setProperty(const String &name, const Variant &value)` — set a property by name. Returns error for unknown property or type mismatch.
- [ ] `Variant property(const String &name) const` — get a single property by name
- [ ] Each concrete node overrides `properties()` and `setProperty()` to expose its configuration (e.g., TestPatternNode exposes "pattern", "motion", "videoDesc", etc.)
- [ ] Property names should be stable, documented identifiers (not display strings)

**What this enables later (not now):**
- `MediaGraph::toJson()` / `MediaGraph::fromJson()` — serialize the full graph: node types, names, property values, and connections (by port name)
- A JSON document fully describes a pipeline that can be instantiated at runtime
- External tools (GUIs, web interfaces) can construct pipelines without C++ code
- Pipeline templates / presets stored as JSON files

**What we do NOT build now:**
- The actual `toJson()`/`fromJson()` methods
- JSON schema definition
- Any graph editor or external tooling
- Variant conversions for complex types (AudioDesc, VideoDesc, etc. — these can be added incrementally when JSON serialization is implemented)

The key discipline is: **if a node has a setter, it must also be reachable via `setProperty()`**. This is a convention enforced by code review, not by the type system.

---

### Design Constraint: Copy-on-Write Mutation Safety

In a pipelined system with queues and fan-out, multiple nodes may hold references to the same `Buffer::Ptr` simultaneously (e.g., a frame still in an upstream queue while the downstream node processes it, or a fan-out delivering the same frame to multiple paths). Mutating a shared buffer would corrupt data for other consumers.

**Solution: Leverage existing SharedPtr COW + `ensureExclusive()` on Image/Audio.**

`SharedPtr` already implements copy-on-write via `modify()` / `detach()` — when `referenceCount() > 1`, `modify()` clones the object; when `referenceCount() == 1`, it's a no-op. This is the foundation for mutation safety.

The gap is that `Image::Ptr::modify()` clones the Image object (descriptor + plane pointer list), but the underlying `Buffer::Ptr` plane data still points to the same memory. We need a "deep detach" that ensures the pixel buffers themselves are exclusively owned.

**Image additions:**
- [ ] `bool isExclusive() const` — returns true if all plane buffers have `referenceCount() <= 1`
- [ ] `void ensureExclusive()` — for each plane `Buffer::Ptr`, calls `modify()` which triggers COW detach if `referenceCount() > 1`. If all planes are already exclusive, this is a no-op.

**Audio additions:**
- [ ] `bool isExclusive() const` — returns true if the buffer has `referenceCount() <= 1`
- [ ] `void ensureExclusive()` — calls `_buffer.modify()` to trigger COW detach if shared

**When copies actually happen:**

| Pipeline topology | Buffer refcount at process() | Copy? |
|---|---|---|
| Linear chain (A → B → C) | 1 (upstream moved on) | No — zero cost |
| Fan-out, all read-only consumers | 1 per consumer (shared safely) | No |
| Fan-out, one mutator + N readers | >1 for the mutating path | Yes — mutator copies, readers share original |

In the common linear-chain case, `ensureExclusive()` is a no-op — the refcount is naturally 1 by the time the downstream node processes the frame. Only fan-out to a mutating node triggers an actual copy.

**Convention (enforced by code review, documented in CODING_STANDARDS):**
- **Source nodes** (TestPatternNode, etc.) allocate fresh buffers and render into them.
- **Read-only nodes** (RTP sinks, analyzers, monitors) read their input frame without modification. They never write to the image/audio buffers.
- **Mutating nodes** (TimecodeOverlayNode, color correction, etc.) call `ensureExclusive()` before modifying buffer data. This is a no-op in linear pipelines and only copies when actually shared.
- **Fan-out** delivers the same `Frame::Ptr` to all links — no copy. Read-only consumers share safely. Mutating consumers detach via `ensureExclusive()`.

**Example mutating node pattern:**
```cpp
void TimecodeOverlayNode::process() {
    Image::Ptr img = pullInput();
    img.modify();              // detach Image object if shared (cheap: metadata + ptr list)
    img->ensureExclusive();    // detach plane buffers if shared (no-op if refcount == 1)
    PaintEngine pe = img->createPaintEngine();
    pe.drawText(...);          // safe to draw — buffers are exclusively ours
    pushOutput(img);
}
```

**Buffer allocation and the MemSpace layer:**

All buffer allocation (including COW clones) goes through the buffer's `MemSpace`. The Buffer copy constructor allocates from the source's MemSpace (`o._alloc.ms.alloc()`), so COW clones inherit the same allocation strategy. This means buffer pooling, if desired, is implemented as a `MemSpace` — not as a pipeline-level concept. Nodes and Image/Audio never need to know about pooling; it's transparent at the allocation layer.

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
// During pipeline configure():
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

---

## MediaPort

Data object describing a node's input or output connection point.

Port types define what media a port carries. A Frame port carries the full Frame (image + audio + metadata). Image and Audio ports carry their respective sub-frame data plus metadata. This allows nodes to work at the level of abstraction they need — a test pattern generator outputs a complete Frame, while a JPEG encoder only cares about the image.

**Files:**
- [ ] `include/promeki/proav/mediaport.h`
- [ ] `src/mediaport.cpp`
- [ ] `tests/mediaport.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Direction { Input, Output }`
- [ ] `enum MediaType { Frame, Image, Audio, Encoded }`
  - [ ] **Frame** — carries full Frame (image + audio + metadata). Described by VideoDesc + AudioDesc.
  - [ ] **Image** — carries image + metadata. Described by ImageDesc (or VideoDesc for frame-rate context).
  - [ ] **Audio** — carries audio + metadata. Described by AudioDesc.
  - [ ] **Encoded** — carries compressed/encoded data + metadata. Described by `EncodedDesc` (codec FourCC, original ImageDesc/AudioDesc, bitrate, etc.). Used for JPEG, H.264, or other compressed data flowing between encoder and network sink nodes.
- [ ] `String name() const`, `setName(const String &)` — human-readable port name
- [ ] `Direction direction() const`
- [ ] `MediaType mediaType() const`
- [ ] `AudioDesc audioDesc() const` — valid when mediaType == Audio or Frame
- [ ] `VideoDesc videoDesc() const` — valid when mediaType == Frame
- [ ] `ImageDesc imageDesc() const` — valid when mediaType == Image or Frame
- [ ] `EncodedDesc encodedDesc() const` — valid when mediaType == Encoded
- [ ] `void setAudioDesc(const AudioDesc &)`
- [ ] `void setVideoDesc(const VideoDesc &)`
- [ ] `void setImageDesc(const ImageDesc &)`
- [ ] `void setEncodedDesc(const EncodedDesc &)`
- [ ] Compatibility rules for `isCompatible()`:
  - [ ] Frame↔Frame: check VideoDesc + AudioDesc match
  - [ ] Image↔Image: check ImageDesc match
  - [ ] Audio↔Audio: check AudioDesc match
  - [ ] Encoded↔Encoded: check EncodedDesc match (codec + source format)
  - [ ] Frame→Image: compatible (image extracted from frame)
  - [ ] Frame→Audio: compatible (audio extracted from frame)
  - [ ] Image/Audio→Frame: not compatible (can't synthesize a full frame from a part)
  - [ ] Encoded is only compatible with Encoded (no implicit encode/decode)
- [ ] `bool isCompatible(const MediaPort &other) const`
- [ ] `bool isConnected() const`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: construction, compatibility checks, all media type combinations, desc assignment

---

## MediaNode

ObjectBase-derived base class for all pipeline processing nodes.

**Files:**
- [ ] `include/promeki/proav/medianode.h`
- [ ] `src/medianode.cpp`
- [ ] `tests/medianode.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `enum State { Idle, Configured, Running, Error }`
- [ ] `enum ThreadingPolicy { UseGraphPool, DedicatedThread, CustomPool }`
- [ ] `State state() const`
- [ ] `String name() const`, `setName(const String &)`
- [ ] Input/output ports:
  - [ ] `List<MediaPort::Ptr> inputPorts() const`
  - [ ] `List<MediaPort::Ptr> outputPorts() const`
  - [ ] `MediaPort::Ptr inputPort(int index) const`
  - [ ] `MediaPort::Ptr outputPort(int index) const`
  - [ ] `MediaPort::Ptr inputPort(const String &name) const`
  - [ ] `MediaPort::Ptr outputPort(const String &name) const`
  - [ ] `int inputPortCount() const`, `int outputPortCount() const`
- [ ] Protected: `addInputPort(MediaPort::Ptr)`, `addOutputPort(MediaPort::Ptr)`
- [ ] Threading:
  - [ ] `void setThreadingPolicy(ThreadingPolicy policy)`
  - [ ] `void setThreadingPolicy(ThreadPool *pool)` — CustomPool variant
  - [ ] `ThreadingPolicy threadingPolicy() const`
- [ ] Input queue (per-node FIFO):
  - [ ] Each node has a thread-safe input `Queue<Frame::Ptr>` fed by upstream MediaLinks
  - [ ] `void setIdealQueueSize(int size)` — target queue depth (default: 2). Pipeline uses this as a hint for back-pressure.
  - [ ] `int idealQueueSize() const`
  - [ ] `int queuedFrameCount() const` — current input queue depth
- [ ] Virtual lifecycle:
  - [ ] `virtual Error configure()` — validate ports, allocate resources. Transition Idle -> Configured.
  - [ ] `virtual Error start()` — begin processing. Transition Configured -> Running.
  - [ ] `virtual void stop()` — stop processing. Transition Running -> Idle.
  - [ ] `virtual void process() = 0` — process one cycle of data
  - [ ] `virtual void starvation()` — called when the node's input queue is empty and it needs data. Default: no-op. Override for nodes that need to handle starvation (e.g., log, insert silence/black, repeat last frame).
- [ ] Property interface (for future JSON serialization — see design constraint above):
  - [ ] `virtual Map<String, Variant> properties() const` — all configurable properties
  - [ ] `virtual Error setProperty(const String &name, const Variant &value)` — set by name
  - [ ] `Variant property(const String &name) const` — get by name (non-virtual, calls properties())
- [ ] Static node type registry:
  - [ ] `static void registerNodeType(const String &typeName, std::function<MediaNode *()> factory)`
  - [ ] `static MediaNode *createNode(const String &typeName)`
  - [ ] `static List<String> registeredNodeTypes()`
  - [ ] `PROMEKI_REGISTER_NODE(ClassName)` macro for .cpp files
- [ ] `PROMEKI_SIGNAL(stateChanged, State)`
- [ ] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [ ] Doctest: state transitions, port management, property get/set, node registry, via concrete test node subclass

---

## MediaLink

Connects an output port to an input port. Delivers frames from the source node to the sink node's input queue. MediaLink does not buffer — the buffering is in the sink node's input queue (see MediaNode).

When the source port is a Frame port and the sink port is an Image or Audio port, the link extracts the relevant sub-frame data automatically.

**Files:**
- [ ] `include/promeki/proav/medialink.h`
- [ ] `src/medialink.cpp`
- [ ] `tests/medialink.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `MediaPort::Ptr source() const` — output port
- [ ] `MediaPort::Ptr sink() const` — input port
- [ ] `MediaNode *sourceNode() const`
- [ ] `MediaNode *sinkNode() const`
- [ ] `Error deliver(Frame::Ptr frame)` — deliver a frame from source to sink node's input queue. Handles Frame→Image/Audio extraction if port types differ. Returns error if sink queue is full (back-pressure).
- [ ] Format negotiation: validate source/sink compatibility on connection (uses MediaPort::isCompatible)
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: deliver frames, format compatibility check, Frame→Image extraction, Frame→Audio extraction

---

## MediaGraph

DAG of MediaNodes connected by MediaLinks. Validates topology. An output port may be connected to multiple input ports (fan-out) — the same frame is delivered to all connected links.

**Files:**
- [ ] `include/promeki/proav/mediagraph.h`
- [ ] `src/mediagraph.cpp`
- [ ] `tests/mediagraph.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `Error addNode(MediaNode *node)` — takes ownership
- [ ] `Error removeNode(MediaNode *node)`
- [ ] `List<MediaNode *> nodes() const`
- [ ] `MediaNode *node(const String &name) const` — find by name
- [ ] `MediaLink::Ptr connect(MediaPort::Ptr output, MediaPort::Ptr input)` — create link
- [ ] `MediaLink::Ptr connect(MediaNode *source, int outputIndex, MediaNode *sink, int inputIndex)` — convenience
- [ ] `MediaLink::Ptr connect(MediaNode *source, const String &outputName, MediaNode *sink, const String &inputName)`
- [ ] `Error disconnect(MediaLink::Ptr link)`
- [ ] `Error disconnect(MediaPort::Ptr output, MediaPort::Ptr input)`
- [ ] `List<MediaLink::Ptr> links() const`
- [ ] `Error validate()` — check topology:
  - [ ] No cycles (DAG requirement)
  - [ ] All required ports connected
  - [ ] Format compatibility on all links
  - [ ] Returns descriptive error on failure
- [ ] `List<MediaNode *> topologicalSort()` — processing order
- [ ] `List<MediaNode *> sourceNodes()` — nodes with no inputs
- [ ] `List<MediaNode *> sinkNodes()` — nodes with no outputs
- [ ] `void clear()` — remove all nodes and links
- [ ] Doctest: build graph, validate, topological sort, cycle detection, format mismatch detection

---

## MediaPipeline

Owns MediaGraph + default thread pool. Orchestrates processing.

**Files:**
- [ ] `include/promeki/proav/mediapipeline.h`
- [ ] `src/mediapipeline.cpp`
- [ ] `tests/mediapipeline.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `enum State { Stopped, Starting, Running, Paused, Stopping, Error }`
- [ ] `MediaGraph *graph()` — access the managed graph
- [ ] `ThreadPool *threadPool()` — the default pool
- [ ] `void setThreadPool(ThreadPool *pool)` — override default
- [ ] `Error start()` — validate graph, configure all nodes, start processing
- [ ] `Error stop()` — stop all nodes, wait for completion
- [ ] `Error pause()` — pause processing
- [ ] `Error resume()` — resume from pause
- [ ] `State state() const`
- [ ] Processing model (back-pressure driven):
  - [ ] Each node tries to keep its input queue empty by calling `process()` whenever data is available
  - [ ] Source nodes (no inputs) run continuously, gated by downstream back-pressure — if the next node's queue is full, the source blocks
  - [ ] Back-pressure propagates naturally: if a sink node is slow, its input queue fills, which blocks the upstream node's `deliver()`, which blocks the upstream node's `process()`
  - [ ] Only nodes that care about real-time timing (e.g., RTP sink nodes) manage their own pacing. The pipeline itself does not enforce timing.
  - [ ] When a node's input queue is empty and it's scheduled to process, `starvation()` is called instead
  - [ ] End-of-stream is signaled via Frame metadata (e.g., `Metadata::EOS` flag). Nodes propagate EOS downstream. When all sink nodes have received EOS, the pipeline transitions to Stopped.
  - [ ] Scheduling:
    - [ ] Respects topological order from MediaGraph
    - [ ] Schedules `process()` calls to ThreadPool
    - [ ] Nodes with DedicatedThread policy get their own thread
    - [ ] Nodes with CustomPool policy use their specified pool
- [ ] `PROMEKI_SIGNAL(stateChanged, State)`
- [ ] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [ ] `PROMEKI_SIGNAL(started)`
- [ ] `PROMEKI_SIGNAL(stopped)`
- [ ] Error propagation: node errors propagate to pipeline level
- [ ] Doctest: simple source -> sink pipeline, start/stop, pause/resume
- [ ] Demo: audio passthrough demo (AudioSourceNode -> AudioSinkNode)
