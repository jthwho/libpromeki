# Data Object Categories {#dataobjects}

How data objects are categorized by ownership and sharing semantics.

Every data object in promeki falls into one of two categories based on
its size, usage patterns, and sharing semantics.

## Simple {#simple}

Small, lightweight value types that are always used directly. They have
no `RefCount` overhead and cannot be wrapped in `SharedPtr`.
Copying is cheap (a few words of memory), so there is no benefit to
reference counting.

| Class | Description |
|-------|-------------|
| `Point` | N-dimensional coordinate |
| `Rational` | Exact fraction (num/den) |
| `Size2Du32` / `Size2Di32` | 2D width/height pair |
| `Rect2Di32` | 2D rectangle (position + size) |
| `UUID` | 128-bit unique identifier |
| `Timecode` | SMPTE timecode (H:M:S:F) |
| `TimeStamp` | Steady-clock time point |
| `XYZColor` | CIE XYZ color triple |
| `Color` | Float[4] color value with ColorModel |
| `ColorModel` | Color model / color space descriptor (TypeRegistry) |
| `MemSpace` | Memory space descriptor (TypeRegistry) |
| `PixelMemLayout` | Pixel memory layout descriptor (TypeRegistry) |
| `PixelFormat` | Full pixel description: format + color model + ranges (TypeRegistry) |
| `Pair` | Typed pair of two values |
| `Result` | `Pair<T, Error>` for fallible operations |
| `Span` | Non-owning view over contiguous storage |
| `Stack` | LIFO stack (std::stack wrapper) |
| `PriorityQueue` | Priority queue (std::priority_queue wrapper) |
| `Duration` | Time duration value |
| `FrameNumber` | Absolute frame index along a media timeline |
| `FrameCount` | Frame count (with empty / unknown / infinity sentinels) |
| `MediaDuration` | Start frame plus a frame count (defines `FrameRange`) |
| `FrameRate` | Frame rate descriptor |
| `AudioLevel` | Audio level in dBFS |
| `InspectorEvent` | Per-frame measurement record produced by `InspectorMediaIO` |
| `InspectorSnapshot` | Aggregate accumulator state from `InspectorMediaIO` |
| `InspectorDiscontinuity` | One detected discontinuity in the inspector's frame stream |
| `ContentLightLevel` | HDR content light level (MaxCLL / MaxFALL, CTA-861.3) |
| `MasteringDisplay` | HDR mastering display color volume (SMPTE ST 2086) |

## Internally-CoW value-type handles {#cow_handles}

Plain value types that wrap an internal `SharedPtr<Data>` to provide
copy-on-write semantics.  Copying one is O(1) — an atomic refcount
bump on the shared storage — and any mutator detaches a private copy
on first write.  These types deliberately do **not** expose a `Ptr`
alias: a value is already as cheap to pass around as a raw shared
pointer would be, so adding an extra layer of pointer indirection
only obscures the model.

| Class | Description |
|-------|-------------|
| `String` | Text everywhere; rides through every API |
| `Buffer` | Cross-thread / cross-domain memory handoff |
| `VariantDatabase` | Typed key/value store; base of `Metadata` and friends |
| `Metadata` | Typed key-value metadata (CoW via `VariantDatabase`) |
| `Frame` | Pipeline-wide container of payloads + metadata |
| `JsonObject` | JSON object container wrapping `nlohmann::json` |
| `JsonArray` | JSON array container wrapping `nlohmann::json` |
| `XmlDocument` | XML document handle backed by pugixml |
| `XmlElement` | XML element subtree backed by pugixml (each handle is its own pugi document holding one root element) |

```cpp
// All of these are O(1) — no deep copy until someone mutates.
JsonObject obj = build();      // returned by value, refcount bump
JsonObject alias = obj;        // refcount bump
alias.set("k", "v");           // CoW: alias detaches; obj unchanged
```

## Shareable {#shareable}

Value types that store their data directly as member variables (no internal
`SharedPtr`). Each class includes `PROMEKI_SHARED_FINAL` so it can be
wrapped in `SharedPtr` when shared ownership is needed. Every shareable
class provides a `Ptr` typedef:

```cpp
// Explicit shared ownership at the call site
MediaPipelineConfig::Ptr cfg = MediaPipelineConfig::Ptr::create();
cfg.modify()->setName(String("MyPipeline"));
```

When used as a local variable or as a member of another class, no
heap allocation or reference counting occurs — the object is a plain value.

Shareable classes also provide convenience type aliases for collections:
- `List` — a `promeki::List` of plain objects (e.g. `Image::List`)
- `PtrList` — a `promeki::List` of `Ptr` (e.g. `Buffer::PtrList`)

| Class | Description |
|-------|-------------|
| `AudioDesc` | Audio format descriptor |
| `ImageDesc` | Image format descriptor |
| `MediaDesc` | Media format descriptor |
| `List` | Generic list (std::vector wrapper) |
| `Map` | Ordered map (std::map wrapper) |
| `Set` | Ordered set (std::set wrapper) |
| `HashMap` | Unordered map (std::unordered_map wrapper) |
| `HashSet` | Unordered set (std::unordered_set wrapper) |
| `Deque` | Double-ended queue (std::deque wrapper) |
| `Array` | Fixed-size array (std::array wrapper) |
| `Image` | Image descriptor + pixel buffer planes (compressed images carry an attached `VideoPacket`) |
| `Audio` | Audio descriptor + sample buffer (compressed audio carries an attached `AudioPacket`) |
| `MediaPacket` | Abstract polymorphic base for encoded bitstream access units |
| `VideoPacket` | Encoded video access unit — attached to a compressed `Image` |
| `AudioPacket` | Encoded audio access unit — attached to a compressed `Audio` |
| `EncodedDesc` | Compressed/encoded media descriptor |

## Thread Safety and Cross-Thread Sharing {#dataobj_threading}

Data objects are not internally thread-safe. Concurrent reads and writes
to the same instance require external synchronization. The recommended
pattern for sharing data between threads depends on whether the object
already manages internal copy-on-write storage (`Frame`, `String`,
`VariantDatabase`, `Metadata`, `Buffer`, `JsonObject`, `JsonArray`,
`XmlDocument`, `XmlElement`) or exposes a `Ptr` type for explicit
shared ownership: once you pass a value or `Ptr` to another thread,
do not mutate the underlying object from the original thread.

### Pattern 1: Share an internally-CoW object by value {#thread_value_cow}

`Frame`, `Buffer`, `String`, `VariantDatabase` (and its `Metadata`
subclass), `JsonObject`, `JsonArray`, `XmlDocument`, and `XmlElement`
are value-type handles that wrap an internal `SharedPtr<Data>`.
Copying one is an atomic refcount bump, so a value-typed `Frame`
(for example) is just as cheap to pass between threads as a raw
`Frame::Ptr` was — and the mutators do copy-on-write internally so
each consumer gets its own private clone the first time it writes.

```cpp
// Producer thread
Frame frame;
// ... fill frame data ...
link.pushFrame(frame);  // value crosses thread boundary; refcount bump

// Consumer thread
auto [frame, err] = link.pullFrame();
// frame is now exclusively accessed by this thread; mutators CoW
```

### Pattern 1b: Share a `Ptr`-managed object {#thread_ptr}

For data objects that still expose a `Ptr` type (e.g.
`MediaPayload::Ptr`, `Image::Ptr`), wrap the object in its `Ptr` type
when handing it across threads. The atomic reference counting in
`SharedPtr` makes the pointer itself safe to copy across threads.

```cpp
// Producer thread
MediaPayload::Ptr payload = ...;
// ... fill payload data ...
sink.submit(payload);   // Ptr safely crosses thread boundary
```

### Pattern 2: Composite structure via Ptr {#thread_composite}

When you need to share multiple related values across threads, compose
them into a shareable data object and share that object via `Ptr`:

```cpp
class RenderJob {
        PROMEKI_SHARED_FINAL(RenderJob)
        public:
                using Ptr = SharedPtr<RenderJob>;
                using List = promeki::List<RenderJob>;
                using PtrList = promeki::List<Ptr>;
                // ... accessors ...
        private:
                ImageDesc       _desc;
                Timecode        _startTC;
                List<String>    _layers;
};

// Submit to thread pool
RenderJob::Ptr job = RenderJob::Ptr::create(desc, tc, layers);
pool.submit([job]() {
        // job->desc(), job->startTC() -- safe, no concurrent mutation
});
```

### Simple types: just copy {#thread_simple}

Simple data objects are small and cheap to copy. When sharing them
across threads, just capture or pass by value — no `Ptr` needed:

```cpp
Timecode tc = currentTimecode();
pool.submit([tc]() {
        // tc is a copy, safe to use
});
```

### Avoid: Mutex around data objects {#thread_antipattern}

Do not protect data objects with a Mutex as a general practice.
If you find yourself wrapping a data object in a mutex, restructure
so that each thread works on its own copy or uses the `Ptr` handoff
pattern. The exception is explicitly thread-safe classes like
`Queue` that manage their own internal synchronization.

**See also:**
- **Containers** module — List, Map, Set, Array, and other container types.
- **Media** module — Buffer for aligned memory storage.
- **ProAV Media** module — Audio, Image, Frame, and format descriptors.
- **Math** module — Point, Size, Rect, Rational, and other value types.
- **Time** module — Timecode, TimeStamp, and Duration.
- **Utilities** module — Variant, UUID, Metadata, Error, and other helpers.
