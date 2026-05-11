# RTMP (Real-Time Messaging Protocol)

**Phase:** 3F
**Library:** `promeki` (feature flag `PROMEKI_ENABLE_RTMP` — depends on
`PROMEKI_ENABLE_NETWORK`; `RTMPS` interop additionally depends on
`PROMEKI_ENABLE_TLS`)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every new
class requires complete doctest unit tests; every Phase has a
matching `utils/promeki-test/` matrix entry. See the project root
`README.md` for full test/build expectations and
`feedback/feedback_*.md` for active style preferences (Variant-first
config, library-first helpers, `Error *err` reporting,
`PROMEKI_OBJECT` for active objects, value semantics for data, no
backwards-compatibility shims).

## Scope

Production-grade Adobe RTMP 1.0 support sufficient to publish to and
subscribe from any modern streaming destination (YouTube Live, Twitch,
Facebook Live, Mux, AWS IVS, Restream, custom origins). Modeled on
the existing RTP work but tailored to RTMP's TCP-based, single-socket,
chunk-multiplexed shape.

Scope decisions (confirmed up front; everything else is deferred —
see [Out of scope / deferred](#out-of-scope--deferred)):

| Variant | In v1? | Notes |
|---------|--------|-------|
| **RTMP** (plain, port 1935)        | yes | Simple + Adobe complex (HMAC-SHA256) handshakes both supported. Most ingest endpoints accept the simple form, but a non-trivial set (notably some Wowza / nginx-rtmp builds and historically Twitch) reject it; we always offer complex first and fall back. |
| **RTMPS** (TLS, port 443)          | yes | TLS-wrapped RTMP via existing `SslSocket`. Required for YouTube / Facebook / Twitch publish endpoints. |
| RTMPE (RC4 obfuscation)            | no  | Adobe-proprietary; only consumed by Flash-era stacks; modern services neither require nor accept it. Easy to add later. |
| RTMPT / RTMPTE (HTTP-tunneled)     | no  | Legacy firewall-traversal layer; rarely encountered today. |
| RTMFP                              | no  | UDP/IP, completely different protocol; out of scope. |

| Role                                | In v1? | Notes |
|-------------------------------------|--------|-------|
| Client publisher (Sink)             | yes    | Connect → `releaseStream` / `FCPublish` → `createStream` → `publish` → media. The dominant production use case. |
| Client subscriber (Source)          | yes    | Connect → `createStream` → `play` → receive media. Needed for pulling from RTMP relays / inboxes. |
| Server ingest                       | no     | Listen / accept / dispatch `connect` / `publish` to MediaIO. Designed-for, not built. |
| Server origin (server-side play)    | no     | Server-side `play` handler. Designed-for, not built. |

| Codec                               | In v1? | Notes |
|-------------------------------------|--------|-------|
| **H.264 / AVC** (video)             | yes    | Universal in RTMP. `AVCDecoderConfigurationRecord` (sequence header) + length-prefixed NALs (NALU packet type). Library already ships `AvcDecoderConfig` + `H264Bitstream::annexBToAvcc`. |
| **AAC** (audio)                     | yes    | `AudioSpecificConfig` sequence header + raw AAC frames. Need a new `AacDecoderConfig` parallel to `AvcDecoderConfig`. |
| **HEVC / H.265** via Enhanced RTMP  | yes    | enhanced-rtmp.org spec (FFmpeg / OBS / YouTube / Facebook). FourCC-based extension to the FLV `VIDEODATA` framing; uses `'hvc1'` payload + `HEVCDecoderConfigurationRecord`. Library already has `HevcDecoderConfig` (struct in `hevcbitstream.h`) + `HevcBitstream`. |
| MP3 / Speex / G.711 / Nellymoser    | no     | Other audio codecs the FLV spec defines. Low value vs effort. |

| Encoding              | In v1?                          | Notes |
|-----------------------|---------------------------------|-------|
| AMF0                  | yes                             | Covers every command / `onMetaData` / `onStatus` exchange in normal RTMP. |
| AMF3                  | no (recognized + rejected)      | Triggered only when a peer signals `objectEncoding=3` in `connect`; extremely rare in publish/play. Reader detects the `0x11` switch marker and surfaces `Error::NotSupported` with a log hint, so a misconfigured client doesn't silently mis-parse. |

The plan delivers two layers, mirroring how RTP shipped:

1. **Standalone RTMP layer** — protocol classes that operate over a
   caller-supplied byte stream (`TcpSocket` / `SslSocket` / any
   `IODevice`). Useful for embedding RTMP into a larger application
   without taking the MediaIO indirection. Mirrors how `RtpSession` /
   `RtpPayload` / `RtpPacket` are usable without `RtpMediaIO`.
2. **`RtmpMediaIO` backend** — derived from `DedicatedThreadMediaIO`,
   wraps the standalone layer behind the `MediaIO` async-command
   interface. Source and sink modes, planner-driven encoder insertion,
   `MediaIOStats` counters, full `promeki-test` coverage. Mirrors
   `RtpMediaIO`.

## Usage at a glance

The two layers are independently useful. Pick `RtmpClient` when you
want direct control over a single connection; pick `RtmpMediaIO` when
the publish / play is one stage in a Pipeline.

**Publish (standalone client):**

```cpp
RtmpClient client;
RtmpConnectOptions opts;                // sensible defaults; tweak as needed
Error err = client.open(Url("rtmps://live.example.com/app/streamKey"), opts);
if (err) { /* connect / TLS / handshake failed */ return err; }

err = client.publish("streamKey");      // blocks until NetStream.Publish.Start
if (err) return err;

// Per encoded access unit:
client.sendVideo(videoTag, timestampMs);
client.sendAudio(audioTag, timestampMs);
```

**Publish (MediaIO sink, hooked into a Pipeline):**

```cpp
MediaConfig cfg;
cfg.set(MediaConfigKey::RtmpUrl, Url("rtmps://live.example.com/app/streamKey"));
cfg.set(MediaConfigKey::RtmpVideoBitrate, 6'000'000);
auto sink = MediaIOFactory::create(cfg);     // returns an RtmpMediaIO sink
sink->executeCmd(MediaIOCommand::Open);
// ... feed frames via the planner; sink handles encode + framing internally.
```

**Play (subscribe):**

```cpp
RtmpClient client;
client.open(Url("rtmp://relay.example.com/app/streamKey"));
client.play("streamKey");
while (auto v = client.takeVideo(/*timeoutMs=*/100)) { /* ... */ }
```

The `RtmpClient` API stays useful even outside MediaIO (one-shot
ingest probes, command-line tools, test fixtures). The `RtmpMediaIO`
layer is the supported entry point for production pipelines.

## Dependencies

- **In tree, ready to use:** `TcpSocket`, `SslSocket` / `SslContext`,
  `IODevice`, `Buffer` / `BufferView` / `BufferIODevice`, `Frame`
  (CoW value handle) / `Metadata` / `MediaPayload` family
  (`UncompressedVideoPayload` / `CompressedVideoPayload` /
  `UncompressedAudioPayload` / `CompressedAudioPayload`),
  `MediaIO` family, `H264Bitstream` + `AvcDecoderConfig` (struct in
  `h264bitstream.h`), `HevcBitstream` + `HevcDecoderConfig` (struct
  in `hevcbitstream.h`), `VideoCodec` / `AudioCodec` registries +
  `VideoEncoder` / `AudioEncoder` factories, `MediaConfig` Variant
  registry, `Url` parser, `Cadence` helper, `Queue<T>` (with
  `pushBlocking` / `popBlocking` returning `Error::Timeout` on full
  / empty within timeout, `Error::Cancelled` after `cancelWaiters`),
  `Thread`, `EventLoop`, `Histogram`, `MediaIOFactory`, vendored
  `mbedTLS` (gated behind `PROMEKI_ENABLE_TLS=ON`; default ON).
- **Library expansions (Phase 0):** `sha2.h` + `hmac.h`,
  `Amf0Value` / `Amf0Reader` / `Amf0Writer`, `FlvTag` +
  `FlvVideoTag` / `FlvAudioTag` / `FlvScriptTag`, `AacDecoderConfig`,
  rtmp-aware `Url` scheme registration, RTMP `MediaConfig` keys.
- **External backend that doesn't exist yet:** an AAC encoder
  registered against `AudioCodec::AAC`. The library doesn't ship one
  today. We choose a backend in Phase 0 (currently leaning on
  vendored **`fdk-aac`** — license-permissive enough for the project
  and the de facto live-AAC encoder used by FFmpeg / OBS / nginx-rtmp).
  Picking and wiring this is itself a small project — see Phase 0 §6.
- **Existing video encoder backends are sufficient:** the NVENC H.264
  / HEVC backend already in tree produces Annex-B byte streams that
  feed straight into `H264Bitstream::annexBToAvcc` / a HEVC analogue,
  exactly the same path RtpMediaIO uses today. No new video encoder
  work is required; we only add the AAC audio side.

## Architecture overview

RTMP is a single TCP / TLS connection that multiplexes typed
"messages" (audio chunk / video chunk / AMF0 command / control /
metadata) onto a stream of "chunks" — small framed packets that
share a small set of "chunk stream IDs". Compared to RTP this is
inverted: RTP is "many UDP datagrams, one logical stream"; RTMP is
"one TCP byte stream, many logical streams interleaved at chunk
granularity".

Topology of an `RtmpMediaIO` writer (publish) instance:

```
                 ┌─────────────────────────────────────────────────┐
strand           │   PacingGate (video) — sleep until next deadline│
(executeCmd      │   from external clock (setClock) or internal    │
 Write)  ────────┼──►wall clock @ FrameRate; Skip = drop frame.    │
                 │                                                 │
                 │   Frame (CoW handle) — push-by-value to per-    │
                 │   kind PayloadQueue, return immediately         │
                 │                                                 │
                 │   ┌─ VideoPacketizerThread ┐                    │
                 │   │  encode (if needed)    │                    │
                 │   │  Annex-B → AVCC        │                    │
                 │   │  build FLV VIDEODATA   │                    │
                 │   │  → MessageQueue        │                    │
                 │   └────────────────────────┘                    │
                 │   ┌─ AudioPacketizerThread ┐                    │
                 │   │  encode (if needed)    │                    │
                 │   │  AAC raw frame         │                    │
                 │   │  build FLV AUDIODATA   │                    │
                 │   │  → MessageQueue        │                    │
                 │   └────────────────────────┘                    │
                 │   ┌─ DataPacketizerThread ┐ (optional)          │
                 │   │  Metadata → AMF0       │                    │
                 │   │  build SCRIPTDATA      │                    │
                 │   │  → MessageQueue        │                    │
                 │   └────────────────────────┘                    │
                 │            │                                    │
                 │            ▼                                    │
                 │   ┌─ RtmpWriterThread ─────────────────────────┐│
                 │   │  one wire-side worker (TCP fairness rules) ││
                 │   │  pop next message from MessageQueue        ││
                 │   │  RtmpChunkStream::writeMessage             ││
                 │   │   → chunk + send via TcpSocket / SslSocket ││
                 │   │  drives Window-Ack-Size / SetPeerBandwidth ││
                 │   │  drives Acknowledgement on RX byte count   ││
                 │   └────────────────────────────────────────────┘│
                 └─────────────────────────────────────────────────┘
```

Topology of an `RtmpMediaIO` reader (play) instance:

```
                 ┌─────────────────────────────────────────────────┐
                 │  ┌─ RtmpReaderThread ─────────────────────────┐ │
TCP / TLS ───────┼─►│  recv → RtmpChunkStream::readMessage       │ │
                 │  │  dispatch on RtmpMessage::Type             │ │
                 │  │   ├─ Audio   → AudioDepacketizerQueue      │ │
                 │  │   ├─ Video   → VideoDepacketizerQueue      │ │
                 │  │   ├─ Data    → DataDepacketizerQueue       │ │
                 │  │   └─ Control → SessionControlState         │ │
                 │  └────────────────────────────────────────────┘ │
                 │   ┌─ VideoDepacketizerThread ┐                  │
                 │   │  parse FLV VIDEODATA     │                  │
                 │   │  AVCC → Annex-B (option) │                  │
                 │   │  → CompressedVideoPayload│                  │
                 │   └──────────────────────────┘                  │
                 │   ┌─ AudioDepacketizerThread ┐                  │
                 │   │  parse FLV AUDIODATA     │                  │
                 │   │  AAC raw                 │                  │
                 │   │  → CompressedAudioPayload│                  │
                 │   └──────────────────────────┘                  │
                 │   ┌─ DataDepacketizerThread ┐                   │
                 │   │  parse FLV SCRIPTDATA    │                  │
                 │   │  AMF0 → Metadata         │                  │
                 │   └──────────────────────────┘                  │
                 │            │                                    │
                 │            ▼                                    │
                 │   ┌─ RtmpAggregatorThread ─────────────────────┐│
                 │   │  merge per-kind output into Frame stream   ││
                 │   │  → bounded _readerQueue                    ││
                 │   └────────────────────────────────────────────┘│
                 │            │                                    │
strand           │            ▼                                    │
(executeCmd ◄────┼────  drain _readerQueue                         │
 Read)           │                                                 │
                 └─────────────────────────────────────────────────┘
```

Why a single `RtmpWriterThread` (versus the RTP per-stream TX
threads): RTMP is one ordered TCP stream where chunks from different
chunk-stream-IDs interleave at the chunk-size granularity. Letting
multiple writer threads share that socket would require an internal
mutex around every chunk emission and lose the fairness control that
the chunk-size knob provides. Per-kind packetizer threads still
isolate the encoding/framing cost (a heavy AVC encode never blocks an
audio packet's framing), but the wire writer is single-threaded and
uses a single bounded `Queue<RtmpMessage>` to serialize.

Why a single `RtmpReaderThread` (versus per-stream RX): RTMP de-mux
happens at the chunk layer regardless of message type, so one reader
must own the socket. After dispatch each kind drains its own queue
independently, mirroring the writer side.

Why a strand-side `PacingGate` (versus relying on bounded-queue
backpressure alone): unlike RTP there is no sub-frame pacing layer
under RTMP — TCP has no kernel `fq` analog and we don't run a
per-stream `Cadence` between packetizer and writer.  Without an
explicit gate the strand drains a synthetic source (TPG, file relay)
into the per-kind PayloadQueues as fast as the encoder can keep up,
which then floods the MessageQueue, the TCP send buffer, and finally
the destination's input buffer in one burst before the bounded queue
clamps the pump.  At default queue depths (64 messages × 2 kinds)
that's ~1–2 s of stream pushed in well under a real-time second per
GOP — most live destinations (mediamtx LL-HLS, Twitch, YouTube) will
log warnings or refuse the publish outright.  Mirroring RTP's
`PacingGate` model fixes this and gives capture-card pipelines a
clean way to feed an upstream clock through `setClock`.

## Library expansions (Phase 0)

These land first per the project's "foundation work before visible
results" feedback. No `RtmpMediaIO` work begins until everything
here is in tree, unit-tested, and warning-free.

**Status (2026-05-10):** Phase 0 is **complete**. All nine
sub-sections landed in tree, unit-tested, warning-free.  137 new
test cases / 1178 assertions; full build + ctest green.  Notable
deviations from the plan as written are called out per-section
below.

### 1. `sha2.h` (new file)

Public API (matches the existing `sha1.h` / `md5.h` shape — single
free function over `const void *`, plus a streaming class because the
RTMP complex handshake walks the C1 / S2 byte map in two regions
around the digest field and benefits from incremental update):

```cpp
using SHA256Digest = ByteArray<32>;
SHA256Digest sha256(const void *data, size_t len);

class Sha256 {                  // streaming variant for the chunked handshake
        public:
                Sha256();
                ~Sha256();
                void update(const void *data, size_t len);
                SHA256Digest finalize();
        private:
                struct Impl;
                UniquePtr<Impl> _d;
};
```

Implementation: hand-rolled (RFC 6234), mirroring the existing
`sha1.cpp` exactly. We deliberately do **not** wrap mbedTLS for
this — the existing `sha1` is hand-rolled in core for a reason: it
keeps the digest helper available even when `PROMEKI_ENABLE_TLS=OFF`
without dragging in a vendored TLS stack just for hashing. The
implementation cost is one ~100-line file and matches the rest of
the core crypto helpers' shape. Always available (no feature flag);
`Sha256` can be used outside RTMP for any future hashing need.

Files:
- [x] `include/promeki/sha2.h`
- [x] `src/core/sha2.cpp`
- [x] `tests/unit/sha2.cpp` (NIST vectors, streaming-vs-one-shot
      equality, RFC 6234 vectors).

**Deviation:** to keep the HMAC implementation from duplicating
SHA-1's transform internally, `sha1.h` / `sha1.cpp` were also
extended with a streaming `Sha1` class (mirroring `Sha256`).  The
plan called out only the SHA-256 streaming class, but the extension
to SHA-1 was a small additive change with positive maintenance
impact and is exercised by new `Sha1: streaming*` tests in
`tests/unit/sha1.cpp`.

### 2. `hmac.h` (new file)

```cpp
SHA256Digest hmacSha256(const void *key,  size_t keyLen,
                        const void *data, size_t dataLen);

class HmacSha256 {              // streaming variant for the chunked handshake
        public:
                HmacSha256(const void *key, size_t keyLen);
                ~HmacSha256();
                void update(const void *data, size_t len);
                SHA256Digest finalize();
        private:
                struct Impl;
                UniquePtr<Impl> _d;
};
```

The streaming class is what the RTMP complex handshake uses — it
walks the 1504 bytes outside the digest field as two regions, and
streaming `update()` avoids materializing the concatenation in a
scratch buffer.

A SHA-1-based HMAC counterpart can be added at the same time; we
don't need it for RTMP but it's trivial alongside SHA-256 and we
already have `sha1.h`.

Files:
- [x] `include/promeki/hmac.h`
- [x] `src/core/hmac.cpp`
- [x] `tests/unit/hmac.cpp` (RFC 4231 vectors).

Implementation note: both `hmacSha256` and `hmacSha1` share a
single templated `hmacOneShot<Traits>` helper inside `hmac.cpp`,
parameterized on a `Sha1Traits` / `Sha256Traits` adapter — the
RFC 2104 construction is identical for both.

### 3. `Amf0Value` / `Amf0Reader` / `Amf0Writer` (new files)

AMF0 — Action Message Format 0, from the legacy Adobe Flash spec —
is RTMP's command-message encoding. The full grammar is small (10
type markers + length-prefixed arrays / objects) but the strict-mode
quirks (long string ≥ 64 KiB switching marker, ECMA-array `count`
hint, end-of-object `\0\0\x09` sentinel) are the kind of thing that
silently breaks interop unless we cover every case from the start.

Public API (Variant-first per the feedback memory). `Amf0Value` is
an **internally-CoW value-type handle** that wraps a private
`SharedPtr<Amf0Data>` — same shape as `JsonObject` / `JsonArray`
(reference 2026-05-07 conversion). Copies are refcount bumps; mutators
detach. The `::Ptr` alias is **deleted outright**, in line with the
post-2026-05-07 convention; callers that need shared ownership pass
the value by reference or move it.

The **field container is order-preserving** — AMF0 objects are
insertion-ordered on the wire and FMS-clone servers (Wowza,
nginx-rtmp) reject `connect` bodies whose fields arrive in unexpected
order, so we cannot back the field map with `Map<>` (which is sorted).
Internally `Amf0Data` stores fields as a
`List<std::pair<String, Amf0Value>>` plus an opportunistic
`HashMap<String, size_t>` index for O(1) lookup. `Map<>` is fine for
the *stat block* fields under `onStatus` (where order is irrelevant)
but for object / ecma-array bodies the list-of-pairs is mandatory.

```cpp
class Amf0Value {
        public:
                using List = ::promeki::List<Amf0Value>;
                using Field = std::pair<String, Amf0Value>;
                using FieldList = ::promeki::List<Field>;

                enum Type {
                        Null, Undefined, Boolean, Number, String,
                        Object, EcmaArray, StrictArray, Date,
                        XmlDocument, TypedObject, Reference,
                        Unsupported
                };

                Amf0Value();                                            // Null
                Amf0Value(bool);
                Amf0Value(double);
                Amf0Value(int);                                         // promotes
                Amf0Value(const String &);
                Amf0Value(const char *);

                // Value semantics: copy = refcount bump on internal data;
                // mutators below detach if shared (CoW).
                Amf0Value(const Amf0Value &) = default;
                Amf0Value(Amf0Value &&) noexcept = default;
                Amf0Value &operator=(const Amf0Value &) = default;
                Amf0Value &operator=(Amf0Value &&) noexcept = default;

                Type    type() const;
                bool    isObject() const;
                bool    isNumber() const;
                // ... predicate accessors per type ...

                bool    asBool() const;
                double  asNumber() const;
                String  asString() const;

                // Object / EcmaArray (insertion-ordered):
                const FieldList &fields() const;
                FieldList       &fields();                              // mutating, CoW-detaches; preserves insertion order
                const Amf0Value *find(const String &key) const;
                Amf0Value       *find(const String &key);               // CoW-detaches
                void             setField(const String &key, Amf0Value v); // append-or-replace, preserves position

                // StrictArray:
                const ::promeki::List<Amf0Value> &items() const;

                static Amf0Value object(std::initializer_list<Field> kv);

        private:
                class Amf0Data;
                SharedPtr<Amf0Data> _d;                                 // CoW; never null
};
```

Explicitly **not** declared: `using Ptr = SharedPtr<Amf0Value>`. Per
the project's post-2026-05-07 cleanup of CoW value-type handles, no
compat shim. Existing helpers that want shared ownership of an AMF0
tree (e.g., for caching the last `onMetaData`) just store an
`Amf0Value` by value — copying is cheap, mutating performs CoW.

`Amf0Reader` walks a `BufferView` returning `Result<Amf0Value::List>`
(an AMF0 message body is conceptually a list of values), with
`Error::CorruptData` on malformed input and `Error::OutOfRange` on
truncation. `Amf0Writer` owns a `Buffer &` and exposes
`writeNumber(double)` / `writeString(String)` / etc. for the cases
where building a value tree would be wasteful. Both must round-trip
losslessly against every well-known RTMP command shape (`connect`,
`createStream`, `publish`, `play`, `releaseStream`, `FCPublish`,
`FCUnpublish`, `FCSubscribe`, `deleteStream`, `_result`, `_error`,
`_checkbw`, `onStatus`, `onMetaData`, `onBWDone`).

Specifically required for interop (verified against ffmpeg /
nginx-rtmp / OBS captures):

- AMF0 `LongString` (`0x0C`) for strings ≥ 64 KiB.
- ECMA-array (`0x08`) with the "count hint" preceding the field map.
- Strict-array (`0x0A`) round-trip preserving order.
- `Date` (`0x0B`) — milliseconds-since-epoch double + 2-byte zero
  timezone (FMS-mandated).
- The `\0\0\x09` end-of-object sentinel emitted at the end of every
  object / ecma-array.
- Reference (`0x07`) — readable but never emitted by us. AMF0
  Reference is genuinely rare on the wire (most servers only emit
  References under AMF3); we cover it for completeness.
- AMF3 switch (`0x11`) — *recognized* in v1: when the reader sees it
  at top level it returns `Error::NotSupported`, which the
  RtmpSession surfaces so we can log "peer sent AMF3 — set
  `objectEncoding=0` in your client" rather than silently mis-parsing.

Files:
- [x] `include/promeki/amf0.h`
- [x] `src/core/amf0.cpp`
- [x] `tests/unit/amf0.cpp` (full doctest matrix: every type
      round-trip, malformed inputs, real-world `connect` / `_result`
      / `onStatus` command bodies (synthesized to match the
      on-the-wire layout — no captured fixtures yet, but the
      structural shape is asserted),
      insertion-order preservation of `connect` field map across
      round-trip).

**Deviations from the plan:**
- The "opportunistic `HashMap<String, size_t>` index" for O(1)
  field lookup is **not yet implemented**.  AMF0 objects in
  practice have ≤ ~12 fields (the `connect` option set is the
  worst case at 10), so the linear-scan path in `find()` /
  `setField()` is fine for v1.  Adding the hash index later is
  additive and keeps the public API unchanged.
- Captured byte streams from real RTMP servers (per the test
  matrix's "from a known-good server" item) are **not yet** in
  `tests/data/rtmp/`.  The current tests build the same shapes
  synthetically through the Variant-style builder API.  Captured
  fixtures can be added when Phase 1 / Phase 2 / Phase 3 testing
  needs them.

The AMF0 facility is also a useful library primitive outside RTMP
(legacy SWF parsing, FLV file readers / writers), so we put it in the
core layer rather than tucked under network/.

### 4. FLV tag framing (new files)

RTMP carries audio / video / script payloads using a subset of the
FLV file format's tag layout. Splitting these into their own header
is worthwhile both for clarity and because we plausibly ship an
`FlvFileMediaIO` later (FLV file is the same tag format wrapped in
a 13-byte file header + a back-pointer trailer) — landing the framer
without the file wrapper costs nothing now.

```cpp
class FlvVideoTag {                     // FLV VIDEODATA
        public:
                enum FrameType {
                        Keyframe = 1,           // IDR / IRAP
                        InterFrame = 2,
                        DisposableInterFrame = 3,
                        GeneratedKeyframe = 4,
                        InfoFrame = 5
                };
                enum Codec {                    // legacy FLV codec IDs
                        H263 = 2, Screen1 = 3, Vp6 = 4, Vp6Alpha = 5,
                        Screen2 = 6, Avc = 7,
                        // Enhanced RTMP "ExVideoTagHeader" path (FrameType bit 7 set)
                        ExHevc, ExVp9, ExAv1
                };
                enum AvcPacketType {
                        SequenceHeader = 0,
                        Nalu = 1,
                        EndOfSequence = 2
                };

                FrameType        frameType = InterFrame;
                Codec            codec = Avc;
                AvcPacketType    packetType = Nalu;
                int32_t          compositionTimeOffsetMs = 0;
                Buffer           data;                  // AVCC NALs / sequence header / etc.

                Error            pack(Buffer &out) const;
                static Error     unpack(const BufferView &in, FlvVideoTag &out);
};
```

Plus `FlvAudioTag` (sound-format / sound-rate / sound-size /
sound-type / aac-packet-type / data) and `FlvScriptTag` (AMF0
serialized name + value, used for `onMetaData`).

Enhanced RTMP support: the codec field for HEVC / VP9 / AV1 is the
"ExVideoTagHeader" form per `enhanced-rtmp.org` v1, where the
keyframe bit is reused as an "is-extended-header" flag. The FlvVideoTag
class hides that distinction from callers.

Files:
- [x] `include/promeki/flvtag.h`
- [x] `src/proav/flvtag.cpp` *(under proav, not network — these are
      media payload framers, not transport)*
- [x] `tests/unit/flvtag.cpp` (round-trip every codec/frame-type
      combo, Enhanced RTMP HEVC sequence header, malformed input).

Coverage scope: legacy AVC + Enhanced-RTMP HEVC are exhaustively
round-tripped; Enhanced VP9 / AV1 share the same code path and
have a single-shape round-trip test each.  `PacketTypeCodedFramesX`
(Enhanced-RTMP packet type 3, "no composition-time offset") parses
into `Nalu` with `compositionTimeOffsetMs = 0`; we emit it as
regular `CodedFrames` (packet type 1) on the write path.
`PacketTypeMetadata` (4) and `PacketTypeMPEG2TSSequenceStart` (5)
return `Error::NotSupported` from `unpack`.

### 5. `AacDecoderConfig` (new addition to existing audio codec layer)

Parallels `AvcDecoderConfig`. Parses and serializes an AAC
`AudioSpecificConfig` (the 2 – 5 byte blob that travels as the AAC
sequence header in FLV / RTMP and as `esds` / `mp4a` config bytes in
ISO-BMFF).

```cpp
struct AacDecoderConfig {
                uint8_t  audioObjectType = 2;   // AAC-LC default
                uint8_t  samplingFrequencyIndex = 0;
                uint32_t samplingFrequency = 0; // populated; also derivable
                uint8_t  channelConfiguration = 0;
                bool     sbr = false;           // explicit SBR (HE-AAC v1)
                bool     ps = false;            // PS  (HE-AAC v2)

                static Error parse(const BufferView &payload, AacDecoderConfig &out);
                Error        serialize(Buffer &out) const;

                // Build the 2-byte minimal config from a desc.
                static AacDecoderConfig fromAudioDesc(const AudioDesc &desc);
                AudioDesc                toAudioDesc() const;
};
```

We also need the inverse for the reader: an `AdtsParser` that strips
ADTS headers off encoder output (some encoders deliver ADTS-framed
AAC; FLV expects raw AAC). FFmpeg / fdk-aac can be configured to emit
either; supporting both makes us robust to whichever the chosen
backend happens to default to.

Files (folded into `aacbitstream.h` paralleling
`h264bitstream.h` / `hevcbitstream.h` — both `AacDecoderConfig` and
`AdtsParser` live in the one header):
- [x] `include/promeki/aacbitstream.h` — declares
      `AacDecoderConfig` + `AdtsParser`
      (`Error AdtsParser::strip(const BufferView &in, Buffer &outRaw, AacDecoderConfig &outCfg)`).
- [x] `src/proav/aacbitstream.cpp`
- [x] `tests/unit/aacbitstream.cpp` (every standard rate ×
      channel combination — 13 × 7 = 91 pairs, HE-AAC v1 / v2
      round-trip, ADTS-framed → raw strip + config recovery,
      multi-frame ADTS concatenation).

**Deviation:** the parser covers AOT 1-7 (LC family) + 5 (SBR) +
29 (PS) via the structured fields, but unknown extension bits are
preserved verbatim through the `rawConfig` byte buffer rather
than decoded.  When `rawConfig` is non-empty on serialize, it is
replayed verbatim (the structured fields are bypassed) — this
makes "parse → serialize" lossless even for configs we don't
fully understand.  Configs we *build* from `AudioDesc` go through
the bit-level encoder.

### 6. AAC encoder backend wiring (new vendoring)

The library has `AudioCodec::AAC` and `AudioFormat::AAC` and the
encoder/decoder framework, but no concrete `AAC` backend. This is
the largest single piece of new code under Phase 0.

Decision: **vendor `fdk-aac`** under `thirdparty/fdk-aac/` and
register an `FdkAacEncoder` / `FdkAacDecoder` against
`AudioCodec::AAC` with `BackendWeight::Vendored`. Rationale:

- It's the de facto live-AAC encoder used by FFmpeg, OBS,
  nginx-rtmp, and Shaka.
- Fraunhofer's "FDK AAC Codec Library for Android" license is a
  permissive clickwrap — verify before vendoring.
- Already available as a CMake-buildable submodule on GitHub
  (`mstorsjo/fdk-aac` mirror).

(If the license review blocks vendoring, the fallback is to gate
AAC behind `find_package(FFmpeg)` as a system dep — that path makes
RTMP harder to ship in distro packages but keeps the binary clean.)

Files:
- [x] `thirdparty/fdk-aac/` (submodule — `mstorsjo/fdk-aac`)
- [x] `src/proav/fdkaaccodec.cpp` — single file registers both
      encoder and decoder backends + the `"FdkAac"` codec backend
      name (mirrors how `src/proav/opusaudiocodec.cpp` is laid
      out).  No public header — registration is via the existing
      `AudioEncoder::registerBackend` / `AudioDecoder::registerBackend`
      pattern at static-init time.
- [x] `tests/unit/fdkaaccodec.cpp` (48 kHz stereo + 44.1 kHz mono
      round-trip with RMS-error tolerance, unsupported-channel
      rejection, encoder + decoder reset).
- [x] CMake feature flag `PROMEKI_ENABLE_AAC` (default ON) and
      `PROMEKI_USE_SYSTEM_FDKAAC` (default OFF) — wired in
      `CMakeLists.txt` next to the `PROMEKI_ENABLE_OPUS` block;
      `PROMEKI_ENABLE_AAC requires PROMEKI_ENABLE_PROAV` guard
      enforced.  `PROMEKI_ENABLE_AAC` also added to
      `include/promeki/config.h.in` and
      `src/core/buildinfo.cpp.in` so it appears in build-info
      output (per the comment block at the top of those files).
- [ ] CMake dependency check: `PROMEKI_ENABLE_RTMP` requires
      `PROMEKI_ENABLE_AAC` — **deferred** to Phase 6 (the option
      that this check would gate doesn't exist yet).  Spec
      remains: emit `FATAL_ERROR` when `_RTMP` is on but `_AAC`
      isn't, mirroring the existing `_SRT requires _TLS` guard.

**Deviations from the plan:**
- **No `cmake/promeki_fdkaac_bundle.cmake`** — fdk-aac is
  self-contained (no external dependencies) so it does not need
  the symbol-isolation surgery the SRT bundle performs against
  its private mbedTLS-3.6.  The vendored build goes through a
  plain `ExternalProject_Add` that builds fdk-aac as a static
  library (`BUILD_SHARED_LIBS=OFF`) and installs into
  `${PROMEKI_THIRDPARTY_PREFIX}`; libpromeki links the static
  archive directly.  No `objcopy --localize-symbols` pass is
  necessary.
- **No separate `fdkaacencoder.h` / `fdkaacdecoder.h` public
  headers** — the registration shape inherited from
  `opusaudiocodec.cpp` keeps both classes file-local under the
  anonymous namespace and exposes them only through the
  `AudioCodec` backend registry.  Callers reach the codecs via
  `AudioCodec(AudioCodec::AAC).createEncoder(...)` rather than
  by including a backend-specific header.
- **HE-AAC v1 / v2 modes are reachable but not yet exercised by
  tests.**  The encoder defaults to AAC-LC; HE-AAC is selectable
  by switching the AOT field.  The decoder accepts whichever
  AudioSpecificConfig the encoder emits, so round-trips of those
  modes ought to work, but the test matrix currently covers only
  LC.  Adding HE-AAC v1 / v2 tests is queued for the same
  follow-up that adds the `RtmpAudioBitrate` × `RtmpAudioCodec`
  matrix in Phase 5.
- **Frame size 1024 vs 960** (the original test matrix line) is
  not configurable in the v1 encoder — fdk-aac picks 1024 for LC
  and we don't override.  The plan's note about 960-sample
  frames is a fdk-aac AOT option (`AACENC_GRANULE_LENGTH=960`)
  that we can wire in when a destination requires it.

### 7. Error code additions

- [x] Add `Error::AuthenticationRequired` to the `Error::Code` enum
      in `include/promeki/error.h`. Used by `RtmpSession` for
      `NetStream.Authenticate.UsherToken` (Phase 3); reusable across
      the network stack.
- [x] Add `Error::ProtocolError` to the same enum. Used by
      `RtmpSession` for unmodelled `onStatus` codes and AMF0 protocol
      violations. Distinct from `CorruptData` (byte-level) and
      `LibraryFailure` (downstream failure).
- [x] Update `tests/unit/error.cpp` round-trip / stringification
      cases to cover both new codes (4 new test cases: name + desc
      non-empty for each, distinctness from adjacent codes, no
      `systemError` mapping).

### 8. URL handling + MediaConfig keys

- [x] `Url` already understands arbitrary schemes; verified
      `rtmp://host:1935/app/streamKey` and
      `rtmps://host:443/app/streamKey?token=…` round-trip cleanly;
      added `Url: rtmp*` test cases in `tests/unit/url.cpp`.
- [ ] **App-vs-stream-key path split** — **deferred** to Phase 4
      (`RtmpClient`).  The URL parser preserves the multi-segment
      path verbatim and the test confirms that
      `rtmp://h/x/y/z/key?t=1` parses to `path=/x/y/z/key` with
      `t=1` in the query; the *split rule* (everything before the
      last `/` is `app`, the last segment is `streamKey`) is the
      consumer's responsibility and lands as a small helper on
      `RtmpClient`'s configure step.  The doctest asserting the
      split shape lives with that helper.
- [ ] `MediaIOFactory` resolves `rtmp` / `rtmps` schemes to
      `RtmpMediaIO` (`Type=Rtmp`) — **deferred** to Phase 5
      (lands with `RtmpMediaIO` itself).  The MediaConfig key
      plumbing required to drive this is in place now.
- [x] Add new `MediaConfig` keys via `PROMEKI_DECLARE_ID(KeyName,
      VariantSpec()...)` next to the existing `RtpFooBar` block in
      `include/promeki/mediaconfig.h`. RTMP is a single-stream
      transport so we use top-level keys (`RtmpFooBar`); we do
      **not** mirror the per-stream `VideoRtpFooBar` style.  All
      35 keys from the table below land in this section.
- [x] **Add `TypeSslContext` to `PROMEKI_VARIANT_TYPES_NETWORK` in
      `include/promeki/variant.h`** — wraps `SslContext::Ptr`, mirrors
      the existing `TypeUrl` / `TypeSocketAddress` registrations.
      Required by the `RtmpTlsContext` MediaConfig key. Same machinery
      will let `SrtMediaIO` accept a configured `SslContext` once it
      lands; we add it once here. Round-trip test in
      `tests/unit/variant.cpp`.
      **Sub-deviation:** the registration is double-gated on
      `PROMEKI_ENABLE_NETWORK && PROMEKI_ENABLE_TLS` (SslContext
      doesn't exist without TLS) and required adding a
      `DataStream` operator pair for `SharedPtr<SslContext,
      false>` — the operators are opaque stubs that write the
      type tag only (an SslContext has no persistent form), so
      Variant payload-dispatch round-trips cleanly but the
      `SslContext::Ptr` itself becomes null after a save/load
      cycle.  Documented at the operator declarations in
      `datastream.h`.

The keys, with concrete Variant types matching the existing
`PROMEKI_DECLARE_ID` shape:

| Key                       | Variant type                  | Default                                             | Notes |
|---------------------------|-------------------------------|-----------------------------------------------------|-------|
| `RtmpUrl`                 | `Variant::TypeUrl`            | invalid                                             | `rtmp://host:1935/app/streamKey` or `rtmps://…`. Required. |
| `RtmpStreamKey`           | `TypeString`                  | `String()`                                          | Override of the URL's last path component (some destinations want the stream key in headers, not the path). |
| `RtmpAppName`             | `TypeString`                  | `String()`                                          | Override of the URL's app component (URL path's leading segment). |
| `RtmpFlashVer`            | `TypeString`                  | `"FMLE/3.0 (compatible; libpromeki/<ver>)"`         | `connect.flashVer`. |
| `RtmpTcUrl`               | `TypeString`                  | `String()` (= reconstruct from `RtmpUrl`)           | `connect.tcUrl`. |
| `RtmpPageUrl`             | `TypeString`                  | `String()`                                          | `connect.pageUrl`. |
| `RtmpSwfUrl`              | `TypeString`                  | `String()`                                          | `connect.swfUrl`. |
| `RtmpEnhancedRtmp`        | `TypeBool`                    | `true`                                              | Emit extended-header when codec is HEVC/VP9/AV1. |
| `RtmpChunkSize`           | `TypeS32`, range `[128, 65535]` | `60000`                                           | Local chunk size. Spec-min 128 (RTMP §5.4.1); spec-max is the 24-bit message-length field's tipping point but most peers reject anything ≥ 65536, so we cap at 65535. 60000 matches OBS / FFmpeg defaults; 4096 produces excess header overhead at modern bitrates. |
| `RtmpWindowAckSize`       | `TypeS32`                     | `5'000'000`                                         | Our advertised `WindowAckSize`. |
| `RtmpPeerBandwidth`       | `TypeS32`                     | `5'000'000`                                         | `SetPeerBandwidth` value (sent with limit-type `Dynamic`). |
| `RtmpHandshakeMode`       | `TypeEnum` (RtmpHandshakeMode)| `Auto`                                              | `Auto` / `Simple` / `Complex`. Auto tries Complex first, falls back. |
| `RtmpFcSubscribe`         | `TypeBool`                    | `false`                                             | Emit `FCSubscribe` before `play` (some Wowza configurations require it). Default off — matches OBS / FFmpeg. |
| `RtmpRepeatParameterSets` | `TypeBool`                    | `true`                                              | Re-inject SPS/PPS (or VPS/SPS/PPS for HEVC) inline ahead of every IDR access unit. Some destinations recover from packet loss only when parameter sets repeat; harmless overhead when they don't. |
| `RtmpEmitAnnexB`          | `TypeBool`                    | `false`                                             | Source-mode only: when true, depacketizer reframes AVCC NALs to Annex-B before emitting `CompressedVideoPayload`. Default false — RTMP is natively AVCC; let the planner insert a converter only when downstream needs it. |
| `RtmpDropUntilKeyframe`   | `TypeBool`                    | `true`                                              | Sink-mode only: drop video access units until the first IDR after `publish` succeeds. Most destinations reject a publish that begins on a non-keyframe. |
| `RtmpStartTcpNoDelay`     | `TypeBool`                    | `true`                                              | `TCP_NODELAY` on the publish/play socket. Default on — at modern bitrates Nagle adds 40 ms RTT-bound jitter without measurable batching benefit. |
| `RtmpConnectTimeoutMs`    | `TypeS32`                     | `10000`                                             | TCP connect + TLS handshake budget. |
| `RtmpHandshakeTimeoutMs`  | `TypeS32`                     | `10000`                                             | RTMP handshake budget. |
| `RtmpCommandTimeoutMs`    | `TypeS32`                     | `5000`                                              | `_result`/`onStatus` reply wait per AMF0 transaction. |
| `RtmpReadIdleTimeoutMs`   | `TypeS32`                     | `30000`                                             | Source-mode dead-peer detector — declare the connection lost after this many ms with no inbound bytes. `0` disables. |
| `RtmpRecvBufferBytes`     | `TypeS32`                     | `0` (kernel default)                                | `SO_RCVBUF`. |
| `RtmpSendBufferBytes`     | `TypeS32`                     | `1048576`                                           | `SO_SNDBUF`. |
| `RtmpTlsContext`          | `TypeSslContext`              | invalid (= build one with system CAs)               | `SslContext::Ptr` override for RTMPS. Adding this variant type is itself a Phase 0 deliverable — see the `TypeSslContext` bullet at the bottom of this section. |
| `RtmpTlsVerify`           | `TypeBool`                    | `true`                                              | Peer-verify; `false` only for self-signed test servers. |
| `RtmpVideoCodec`          | `TypeVideoCodec`              | H.264                                               | Pin for the video stream. |
| `RtmpAudioCodec`          | `TypeAudioCodec`              | AAC                                                 | Pin for the audio stream. |
| `RtmpVideoBitrate`        | `TypeS32`                     | `0` (= auto from `FrameRate` × resolution)          | Target bps. |
| `RtmpAudioBitrate`        | `TypeS32`                     | `128000`                                            | Target bps. |
| `RtmpVideoEncoderBackend` | `TypeString`                  | `String()` (= highest-weight)                       | e.g. `"Nvidia"`. |
| `RtmpAudioEncoderBackend` | `TypeString`                  | `String()`                                          | e.g. `"FdkAac"`. |
| `RtmpKeyframeIntervalSec` | `TypeS32`                     | `2`                                                 | GOP target; forwarded to encoder configure. |
| `RtmpDataEnabled`         | `TypeBool`                    | `true`                                              | Emit/consume FLV `SCRIPTDATA` `onMetaData`. |
| `RtmpSendQueueDepth`      | `TypeS32`, range `[2, 1024]`  | `64`                                                | Bounded `MessageQueue` between packetizer and writer. See Phase 4 for the latency math. |
| `RtmpReadQueueDepth`      | `TypeS32`, range `[2, 1024]`  | `64`                                                | Bounded depacketizer → aggregator queue depth. |
| `RtmpVideoPacing`         | `TypeEnum` (RtmpVideoPacing)  | `Internal`                                          | Sink-mode video pacing source.  `Internal` paces the strand against a built-in wall clock at `MediaConfig::FrameRate`; `External` defers entirely to the clock bound via `MediaIOPortGroup::setClock` (no fallback when unbound — gate is a no-op until a clock arrives); `None` disables strand-level pacing and relies on bounded-queue backpressure alone (fast-pump file ingest). An external clock bound via `setClock` always takes precedence over `Internal` once it arrives, mirroring RTP's `RtpMediaIO::executeCmd(MediaIOCommandSetClock)` semantics. |
| `RtmpPaceSkipThresholdMs` | `TypeS32`, range `[1, 5000]`  | `0` (= one frame interval)                          | `PacingGate` `Skip`-verdict threshold (lag past which the strand drops the current frame to bound pile-up).  `0` resolves to one `FrameRate` interval at gate-arm time. |
| `RtmpPaceReanchorThresholdMs` | `TypeS32`, range `[1, 30000]` | `0` (= 8 × frame interval)                    | `PacingGate` `Reanchor`-verdict threshold (lag past which the gate re-anchors its timeline).  `0` resolves to `DefaultReanchorMultiple` × frame interval at gate-arm time. |

Each key gets a Doxygen comment describing units, defaults, and
what subsystem reads it (matching the `RtpFooBar` convention).

The shared enums declared in `enums.h` (per
`feedback_typedenum_enums_h.md`) are:

- `RtmpRole` — `Client` / `Server`. Used by `RtmpHandshake`,
  `RtmpSession`, and the future `RtmpServer`. **Single declaration**
  rather than duplicating the enum in each class.
- `RtmpHandshakeMode` — `Auto` / `Simple` / `Complex`. Used by both
  `RtmpHandshake::setMode` and the `RtmpHandshakeMode` MediaConfig
  key.
- `RtmpVideoPacing` — `Internal` / `External` / `None`.  Drives the
  `RtmpMediaIO` sink-side `PacingGate`'s clock-binding policy.
  Single source of truth for the strand pacing decision: a config
  read at `Open` time picks the initial clock, and a subsequent
  `MediaIOCommandSetClock` from the planner / `MediaIOPortGroup`
  can swap it.

Class-local enums that don't escape their owning class
(`RtmpHandshake::State`, `RtmpMessage::Type`,
`FlvVideoTag::FrameType` / `Codec` / `AvcPacketType`,
`FlvAudioTag::SoundFormat`, etc.) stay nested where they are used —
they're not shared across the public surface.

### 9. Stream-key / credential redaction

RTMP URLs carry the publish secret as the last path component, and
some destinations push a token through a query parameter. Logging
or stringifying any of these in cleartext leaks credentials into
operator logs.

- [x] `Url::redactedString()` (new helper in
      `include/promeki/url.h` / `src/core/url.cpp`) — returns the
      URL with its last path component and any query value whose
      key (case-insensitively) matches a small allowlist (`token`,
      `auth`, `key`, `password`, `signature`) replaced by `***`. The
      default `Url::toString()` continues to round-trip; redaction
      is opt-in at the call site.
- [ ] Every RTMP `PROMEKI_LOG` site that mentions a URL goes
      through `redactedString()` — **lands with Phases 3-5** when
      `RtmpSession` / `RtmpClient` / `RtmpMediaIO` log statements
      land.  The helper is in place now; deferred work is just
      ensuring every call site uses it.  Same convention should be
      back-ported to `RtpMediaIO` / `SrtMediaIO` in a follow-up;
      not in scope here, but called out so the helper is general.
- [x] doctest in `tests/unit/url.cpp`: round-trip
      `rtmp://h/app/sk?token=xyz` → `redactedString()` returns
      `rtmp://h/app/***?token=***`.  Eight test cases total
      (basic stream-key redaction, multi-segment app + key,
      credential keys, case-insensitive matching, key preservation
      vs value redaction, no-op on URLs without path/query,
      non-mutating, invalid URL handling).

**Implementation note:** the redacted serializer is a parallel
code path to `Url::toString()` (rather than a mutated-copy +
toString re-dispatch) — the percent-encoder otherwise escapes the
`***` literal to `%2A%2A%2A`, which would defeat the purpose.

## Phase 1 — RtmpHandshake (simple + complex / HMAC-SHA256)

**Status (2026-05-10):** Phase 1 is **complete**.  Header + impl +
14 doctest cases (1610 assertions) landed; full ctest green.

Files:
- [x] `include/promeki/rtmphandshake.h`
- [x] `src/network/rtmphandshake.cpp`
- [x] `tests/unit/network/rtmphandshake.cpp` *(lives under
      `tests/unit/network/` to match the rest of the RTP / TLS / HTTP
      protocol tests — the plan's `tests/unit/rtmphandshake.cpp` path
      was a typo)*

`RtmpHandshake` is a pure protocol state machine. It does not own a
socket; the caller feeds it bytes (`feed(BufferView)`) and asks for
bytes to send (`pendingOutput()`). This makes it trivially testable
against canned inputs and lets us share the same state machine
between both `RtmpClient` (TCP) and the eventual `RtmpServer` role.

```cpp
class RtmpHandshake {
                PROMEKI_OBJECT(RtmpHandshake, ObjectBase)
        public:
                // Role and Mode come from enums.h (RtmpRole,
                // RtmpHandshakeMode) — see Phase 0 §8.  Only the
                // class-local State enum lives here.
                enum State { NotStarted, ExchangingC0C1, ExchangingC2S2, Done, Failed };

                explicit RtmpHandshake(RtmpRole role, ObjectBase *parent = nullptr);

                Error  setMode(RtmpHandshakeMode mode);

                /// Bytes the caller should send to the peer.  May
                /// return any number of bytes per call; empty when
                /// nothing is pending.  After draining, the caller
                /// is expected to feed any received bytes back via
                /// @ref feed before calling pendingOutput again.
                Buffer pendingOutput();

                /// Feed bytes received from the peer.  Drives state
                /// transitions.  Returns:
                ///   Error::Ok        — input consumed, state may
                ///                      have advanced; check @ref state.
                ///   Error::CorruptData — peer sent malformed bytes
                ///                      (e.g. version mismatch or
                ///                      complex-mode digest mismatch).
                ///   Error::Cancelled — handshake aborted.
                /// The handshake never returns Error::TryAgain — a
                /// "need more bytes" condition is implicit in
                /// state() != Done after Error::Ok.
                Error  feed(const BufferView &data);

                State              state() const;
                RtmpHandshakeMode  negotiatedMode() const;       // valid once Done

                PROMEKI_SIGNAL(complete);
                PROMEKI_SIGNAL(failed, Error);
};
```

**Implementation checklist (the meaty bit):**

- [x] **Simple handshake (RTMP 1.0 §5.2.1 / FLV spec)**: `C0/S0` is one
      byte (version = 3). `C1/S1` is 1536 bytes — 4-byte timestamp + 4
      zero bytes + 1528 random. `C2/S2` echoes the peer's `C1/S1`.
      Easy. The 1528 random bytes come from `Random::trueRandom()`
      (the OS entropy pool), not the deterministic Mersenne Twister
      `Random::global()` — the random region is functionally a nonce
      against replay against legacy FMS implementations and a
      seedable PRNG defeats that.
- [x] **Complex handshake (Adobe FMS3, also known as the
      "digest+key" handshake)**: encodes an HMAC-SHA256 over selected
      regions of the C1 / S1 payload, with two well-known 30-byte and
      36-byte seed keys (the `GenuineFMSKey` and `GenuineFPKey`
      values lifted from Adobe's FMS distribution).
      - [x] Two schemes for the digest's offset within the 1536-byte
            block: scheme 0 (offset stored at bytes 8–11) and scheme 1
            (offset stored at bytes 772–775). YouTube / FB use scheme
            1; older servers use scheme 0. Implement both, try
            scheme 1 first, fall back to scheme 0.
      - [x] Build the digest input by concatenating the 1504 bytes
            outside the digest field — see RTMP spec §5.2.4 and the
            commonly-cited reference C from rtmpdump.  Streaming
            `HmacSha256` is fed the two regions around the digest
            slot directly, no scratch concatenation.
      - [x] Validate the server's S1 digest and reproduce the S2
            digest before sending C2.
      - [x] On peer rejection of complex → fall back to simple,
            controlled by `setMode`.  In `Auto` mode the client emits
            a Complex C1; if S1 fails to validate against either
            scheme, the machine falls back to a Simple-style echo
            without aborting.  A strict `Complex` setting aborts
            with `Error::CorruptData` instead.
- [x] Constant-time HMAC compare.
- [x] Strict bounds-checking on every offset read.  *Fuzzing pass
      not yet wired in; deferred to a follow-up.*
- [x] Doctest cases (14 total, 1610 assertions):
      - simple-mode happy path (both roles)
      - simple-mode local epoch is delivered to peer
      - simple-mode rejects wrong version byte
      - complex-mode happy path (Auto/Complex pair)
      - complex-mode happy path (both ends explicit Complex)
      - Auto client falls back to Simple against a Simple-only server
      - Complex client rejects a Simple-only server
      - peer disconnect mid-handshake transitions to Failed
      - feed on a Failed instance returns Cancelled
      - Complex client rejects S1 with corrupted digest
      - simple-mode reassembles across fragmented feeds (1-byte +
        3-jagged-chunk splits)
      - setLocalNonce rejects wrong-size buffers
      - setLocalNonce is deterministic across the wire
      - setMode after first emission returns Error::Invalid

**Deviations from the plan:**
- **Doctest path:** lives at `tests/unit/network/rtmphandshake.cpp`
  rather than the top-level `tests/unit/rtmphandshake.cpp` from the
  plan — matches where the rest of the RTP / TLS / HTTP protocol
  tests live.
- **Captured ffmpeg byte streams** under `tests/data/rtmp/` are not
  yet landed.  The current matrix exercises the state machine by
  pairing two instances back-to-back, which catches every protocol
  divergence between the two roles by construction.  Captured
  fixtures can be added when Phase 2 / 3 wire up against a real
  server.
- **`State` enum is named differently from `InternalStep`.**  The
  plan's `State { NotStarted, ExchangingC0C1, ExchangingC2S2, Done,
  Failed }` is the externally-visible state and survives unchanged;
  the implementation adds a private `InternalStep` enum that tracks
  the per-byte sub-step (StepSendC0C1 / StepRecvS0S1 / ...) inside
  `ExchangingC0C1` / `ExchangingC2S2`.
- **Additional public API:** `lastError()`, `markPeerClosed()`,
  `peerEpoch()` / `localEpoch()` / `setLocalEpoch()`, and
  `setLocalNonce()` (test-only).  None of these change the
  documented byte-stream behavior; they exist so the surrounding
  `RtmpClient` / `RtmpSession` can report and inject test state
  without poking at private members.

Foundation status: HMAC + SHA256 must be in tree (Phase 0 §1–§2) for
this phase to compile.

## Phase 2 — Chunk stream layer

**Status (2026-05-10):** Phase 2 is **complete**.  RtmpMessage +
RtmpChunkStream landed with 14 doctest cases (180 assertions).  Full
ctest green at 5688 cases.

Files:
- [x] `include/promeki/rtmpmessage.h` (typed message + payload type
      enum)
- [x] `include/promeki/rtmpchunkstream.h`
- [x] `src/network/rtmpchunkstream.cpp`
- [x] `tests/unit/network/rtmpchunkstream.cpp` *(under `network/` to
      match the rest of the RTP / TLS protocol test files; the
      plan's top-level `tests/unit/` path was a typo)*

`RtmpMessage` is a value type carrying:

```cpp
class RtmpMessage {
        public:
                enum Type {
                        // Protocol Control Messages — chunk stream id 2
                        SetChunkSize         = 1,
                        AbortMessage         = 2,
                        Acknowledgement      = 3,
                        UserControl          = 4,
                        WindowAckSize        = 5,
                        SetPeerBandwidth     = 6,
                        // Audio / video — chunk stream ids 4 and 6 by convention
                        AudioMessage         = 8,
                        VideoMessage         = 9,
                        // AMF — chunk stream id 3 by convention (commands)
                        DataMessageAmf3      = 15,
                        SharedObjectAmf3     = 16,
                        CommandMessageAmf3   = 17,
                        DataMessageAmf0      = 18,
                        SharedObjectAmf0     = 19,
                        CommandMessageAmf0   = 20,
                        AggregateMessage     = 22
                };

                Type     type = AudioMessage;
                uint32_t streamId = 0;            // RTMP message-stream id
                uint32_t timestamp = 0;           // 32-bit, milliseconds, monotone within stream
                Buffer   payload;
                uint32_t chunkStreamId = 0;       // hint to the writer; reader fills in
};
```

`RtmpChunkStream` is the bidirectional (de)multiplexer. It owns:

- the local + peer `chunkSize` (default 128, can be raised via
  `SetChunkSize`),
- the local + peer `windowAckSize` (drives `Acknowledgement` emission),
- a small `HashMap<uint8_t, ChunkStreamState>` mapping chunk-stream-id
  to the per-CS-id state needed for chunk header type-1/2/3 encoding
  (timestamp delta tracking, `messageStreamId` echo, partial-message
  reassembly buffer for fragmented payloads).

Public API:

```cpp
class RtmpChunkStream : public ObjectBase {
                PROMEKI_OBJECT(RtmpChunkStream, ObjectBase)
        public:
                explicit RtmpChunkStream(IODevice *device, ObjectBase *parent = nullptr);

                /// Write a complete message.  Splits across chunks
                /// according to the local chunkSize; selects the
                /// strictest header type permissible against the
                /// per-CS-id state.  All bytes go to the device in
                /// one or more write() calls.
                Error           writeMessage(const RtmpMessage &m);

                /// Drain the device until exactly one complete message
                /// has been reassembled.  May internally consume
                /// multiple chunks (incl. extended-timestamp chunks
                /// and chunkSize-reset frames), block on socket reads,
                /// and emit Acknowledgement frames as the local
                /// receive byte count crosses windowAckSize.  The
                /// caller is responsible for handling
                /// SetChunkSize / Abort / WindowAckSize /
                /// SetPeerBandwidth control messages — readMessage
                /// returns them too, since some applications care
                /// about them.
                Result<RtmpMessage> readMessage(unsigned int timeoutMs = 0);

                int             localChunkSize() const;
                Error           setLocalChunkSize(int bytes);
                int             peerChunkSize() const;

                int             localWindowAckSize() const;
                Error           setLocalWindowAckSize(int bytes);
                int             peerWindowAckSize() const;

                int64_t         bytesSent() const;
                int64_t         bytesReceived() const;
                int64_t         lastAckBytesAcked() const;

                PROMEKI_SIGNAL(controlMessageReceived, RtmpMessage);
                PROMEKI_SIGNAL(peerChunkSizeChanged, int);
                PROMEKI_SIGNAL(peerAck, uint32_t);
};
```

**Implementation checklist:**

- [x] Chunk header encode / decode for all four header types (0 = full,
      1 = timestamp-delta + length + type, 2 = timestamp-delta only,
      3 = continuation).
- [x] Extended timestamp (24-bit timestamp field exhausted →
      additional 32-bit field). We always re-emit the 4-byte
      extended timestamp on every type-3 continuation chunk
      (ffmpeg / librtmp tradition).  Decoder reads them back
      whenever the prior header on that CS-id used extended
      timestamp.
- [x] Chunk Stream Id encoding (1-byte / 2-byte / 3-byte forms — the
      basic header).
- [x] Streaming reassembly for messages whose payload exceeds local
      chunk size.
- [x] Per-CS-id reassembly state survives interleaving.
- [x] Ack emission: compare cumulative `bytesReceived()` against
      `peerWindowAckSize`; emit `Acknowledgement` once per window.
- [x] Thread-safety primitives in place (Atomic for cross-thread
      counters / negotiated values; @c Mutex around per-cs-id encode
      and decode maps).  *Concurrent multi-writer testing is
      deferred to Phase 4 (RtmpClient) where the single-writer
      topology is materialized — the chunk-layer locking is in
      place but only exercised by single-threaded doctest cases
      today.*
- [x] Tests:
      - round-trip every header-type encoding (fmt 0 → 1 → 2 → 3
        chain in a single 4-message run)
      - oversize message split / reassembly (5 KiB at chunk-size 128,
        and 1 MiB at chunk-size 4096)
      - chunk-size raise mid-stream
      - extended-timestamp boundary (exactly 0xFFFFFF + multi-chunk
        plus 0x12345678 well above the 24-bit field)
      - back-pressure: 1 MiB at chunk size 4096 completes without
        losing any byte *(the plan's 16 MiB target would run for
        ~30 s on the doctest fixture; 1 MiB exercises the same code
        paths in under a second)*
      - independent CS-ids reassemble interleaved messages
      - large CS-ids exercise the 2-byte and 3-byte basic header
        forms (csid 200 and csid 10000)
      - Acknowledgement emission once the local receive byte count
        crosses peer-window-ack
      - null-device guards on read + write
      - zero-length payload round-trip
      - out-of-range setLocalChunkSize is rejected

**Deviations from the plan:**
- **Doctest path:** `tests/unit/network/rtmpchunkstream.cpp` — same
  network/ subdir reasoning as Phase 1.
- **Captured-from-ffmpeg byte-stream fixture** under
  `tests/data/rtmp/` is **not yet** landed.  Synthetic round-trip
  through a single in-process pipe already catches every
  protocol-layer divergence; captured ffmpeg byte streams will land
  when Phase 3 / 4 testing needs them to exercise interop edge
  cases.
- **`SetPeerBandwidth` automatic WindowAckSize echo** is not
  emitted by the chunk layer.  Per the RTMP §5.4.5 advisory note,
  the response belongs in the session layer (Phase 3) where the
  connect-flow sequencing lives.  The chunk layer surfaces the
  message via `controlMessageReceivedSignal` so the session can
  decide.
- **`PROMEKI_SIGNAL`s spelled out:** `controlMessageReceivedSignal`
  fires for every received Protocol Control Message;
  `peerChunkSizeChangedSignal` fires when the peer's
  `SetChunkSize` lands; `peerAckSignal` fires on incoming
  `Acknowledgement` messages.
- **Test isolation:** the test fixture uses a tiny in-test
  `PipeDevice` (sequential FIFO IODevice).  No socket or
  threading; one writer drives, one reader drains, deterministic
  byte order.

The chunk stream layer is the densest piece of the protocol and the
most-likely source of interop bugs; the test matrix here is
deliberately heavy.

## Phase 3 — RtmpSession

**Status (2026-05-10):** Phase 3 is **complete**.  RtmpSession +
RtmpConnectOptions landed with 13 doctest cases (72 assertions).
Full ctest green at 5701 cases / 115,273 assertions; zero warnings
in our code.

Files:
- [x] `include/promeki/rtmpsession.h`
- [x] `src/network/rtmpsession.cpp`
- [x] `tests/unit/network/rtmpsession.cpp`

`RtmpSession` is the user-facing protocol session. It owns an
`RtmpChunkStream`, runs the `RtmpHandshake` to completion, and
dispatches AMF0 commands ↔ application-level signals. Mirrors
`RtpSession` in spirit: transport-agnostic (works over any
`IODevice`), no MediaIO indirection, surfaces both raw access
(`sendMessage`, `receivedSignal`) and high-level convenience
(`connect`, `publish`, `play`).

```cpp
class RtmpSession : public ObjectBase {
                PROMEKI_OBJECT(RtmpSession, ObjectBase)
        public:
                // Uses RtmpRole from enums.h — same enum the
                // handshake / server use; not redeclared here.

                explicit RtmpSession(RtmpRole role, ObjectBase *parent = nullptr);

                /// Bind a transport.  IODevice must already be open and
                /// connected.  RtmpSession does not own the device.
                Error            attach(IODevice *device);

                /// Drives the handshake to completion (blocking up to timeoutMs).
                Error            performHandshake(unsigned int timeoutMs = 10000);

                // ----- high-level commands (client side) -----

                /// Issues NetConnection.connect with the given app /
                /// tcUrl / pageUrl / swfUrl / flashVer.  Blocks until
                /// _result or _error.
                Error            connect(const RtmpConnectOptions &opts,
                                         unsigned int timeoutMs = 10000);

                Result<uint32_t> createStream(unsigned int timeoutMs = 5000);

                /// publish: streamKey + mode ("live" / "record" / "append")
                Error            publish(uint32_t streamId, const String &streamKey,
                                         const String &mode = "live",
                                         unsigned int timeoutMs = 5000);

                /// play: streamKey + start + duration (seconds; -2 = live, -1 = recorded)
                Error            play(uint32_t streamId, const String &streamKey,
                                      double start = -2.0, double duration = -1.0,
                                      unsigned int timeoutMs = 5000);

                Error            deleteStream(uint32_t streamId);
                Error            fcPublish(const String &streamKey);
                Error            fcUnpublish(const String &streamKey);

                // ----- raw access -----

                /// Send a fully-formed message.  Used by the writer
                /// thread to dispatch audio / video / metadata.
                Error            sendMessage(const RtmpMessage &m);

                /// Drain one incoming message.  Used by the reader
                /// thread.  Control messages are also dispatched
                /// internally before returning.
                Result<RtmpMessage> readMessage(unsigned int timeoutMs);

                // ----- signals -----

                PROMEKI_SIGNAL(handshakeComplete);
                PROMEKI_SIGNAL(connected);
                PROMEKI_SIGNAL(connectionFailed, Error);
                PROMEKI_SIGNAL(streamCreated, uint32_t);
                PROMEKI_SIGNAL(publishStarted, uint32_t);
                PROMEKI_SIGNAL(playStarted, uint32_t);
                PROMEKI_SIGNAL(onStatus, Amf0Value);              // server status messages
                PROMEKI_SIGNAL(onMetaData, Metadata);
                PROMEKI_SIGNAL(audioMessageReceived, RtmpMessage);
                PROMEKI_SIGNAL(videoMessageReceived, RtmpMessage);
                PROMEKI_SIGNAL(disconnected);
};

struct RtmpConnectOptions {
                String       app;
                String       tcUrl;
                String       pageUrl;
                String       swfUrl;
                String       flashVer;
                String       type = "nonprivate";
                int          objectEncoding = 0;       // AMF0
                int          capabilities = 239;       // FMLE-compatible default
                int          audioCodecs  = 0x0FFF;
                int          videoCodecs  = 0x00FF;
                int          videoFunction = 1;
                /// enhanced-rtmp.org `fourCcList` — list of supported
                /// fourcc-coded video codecs sent in the connect AMF0
                /// object as a strict-array of strings.  Default is
                /// the single entry `"hvc1"` (HEVC).  Add `"vp09"` /
                /// `"av01"` once those encoder backends ship.
                List<FourCC> fourCcList = { FourCC("hvc1") };
};
```

**Implementation checklist:**

- [x] AMF0 command serializer / deserializer for every
      publish/play-flow command listed under "AMF support" in Phase 0.
- [x] Transaction-id tracking — `_result` / `_error` reply matching
      via a `HashMap<double, PendingTransaction *>` keyed on AMF0
      txnId.
- [x] `onStatus` parsing into a typed status code so callers can
      distinguish `NetStream.Publish.Start` /
      `NetStream.Play.Reset` / `NetStream.Play.Start` /
      `NetConnection.Connect.Rejected` etc. from the AMF0 object.
      Status code → `Error` mapping (per the project's "specific
      errors" preference) is fixed at the session boundary so
      callers don't have to grep status strings:

      | AMF0 `code`                               | `Error` returned                      |
      |-------------------------------------------|---------------------------------------|
      | `NetConnection.Connect.Success`           | `Error::Ok`                           |
      | `NetConnection.Connect.Rejected`          | `Error::PermissionDenied`             |
      | `NetConnection.Connect.InvalidApp`        | `Error::InvalidArgument`              |
      | `NetConnection.Connect.Failed`            | `Error::ConnectionRefused`            |
      | `NetConnection.Connect.AppShutdown`       | `Error::Cancelled`                    |
      | `NetStream.Publish.Start`                 | `Error::Ok`                           |
      | `NetStream.Publish.BadName`               | `Error::Exists`                       |
      | `NetStream.Publish.Denied`                | `Error::PermissionDenied`             |
      | `NetStream.Play.Start`                    | `Error::Ok`                           |
      | `NetStream.Play.StreamNotFound`           | `Error::NotFound`                     |
      | `NetStream.Play.Failed`                   | `Error::IOError`                      |
      | `NetStream.Play.UnpublishNotify`          | (not an error — emitted as a signal)  |
      | `NetStream.Authenticate.UsherToken`       | `Error::AuthenticationRequired` *(new code — see below)* |
      | unknown / unrecognized                    | `Error::ProtocolError` *(new code — see below)* with `code` preserved in the message |

      Per the project's "specific error codes" preference
      (`feedback_specific_errors.md`), this mapping requires two
      new codes added to `Error::Code` in `include/promeki/error.h`
      as part of Phase 0:
      - `Error::AuthenticationRequired` — peer demanded an auth
        challenge response we don't (yet) produce. Used here for
        `NetStream.Authenticate.UsherToken` and reusable for any
        future protocol that distinguishes "unauthorized" from
        "permission denied".
      - `Error::ProtocolError` — peer sent a protocol-level
        violation or status code we don't model. Distinct from
        `CorruptData` (byte-level malformed) and `LibraryFailure`
        (downstream library blew up).

      The full AMF0 status object stays available via the
      `onStatus` signal so callers that *do* want the original code
      string (logging, telemetry) can inspect it; the typed `Error`
      is just the routed-back value of the synchronous `connect` /
      `publish` / `play` calls.
- [x] `onBWDone` ⁄ `_checkbw` handshake — no-op acknowledged via
      the unhandled-command path; the session continues.
- [x] User-control message handling: `StreamBegin`, `StreamEOF`,
      `StreamDry`, `SetBufferLength`, `StreamIsRecorded`,
      `PingRequest` (replies with `PingResponse`), `PingResponse`.
- [x] Connect-flow client side: after `_result` for connect, the
      session echoes the peer's `WindowAckSize` and raises local
      chunk size to `DefaultPostConnectChunkSize` (60000).
- [x] @c releaseStream / @c FCPublish / @c FCUnpublish /
      @c FCSubscribe / @c deleteStream are exposed as fire-and-forget
      helpers.  Callers in Phase 4 (`RtmpClient`) wrap @c connect →
      @c releaseStream → @c FCPublish → @c createStream →
      @c publish.
- [x] Tests:
      - `onStatus` → `Error` mapping for every well-known code
      - `attach` / `device()` / `chunkStream()` getters + null guards
      - PingRequest → PingResponse echo (byte-level assertion on
        the echoed timestamp)
      - End-to-end `connect` + `createStream` + `publish` via a
        thread-backed `FakeServer` (verifies connect-flow message
        ordering on the wire and the streamId allocation path)
      - `play` end-to-end via the same fixture
      - `publish` rejection: `NetStream.Publish.Denied` →
        `Error::PermissionDenied`
      - `connect` times out when the server never replies

**Deferred from this phase (carry-overs):**
- **Outgoing keepalive** (emit our own `PingRequest` once we've
  consumed `windowAckSize / 2` worth of inbound bytes) → Phase 4
  (`RtmpClient`), where the writer thread that owns the periodic
  outgoing traffic lives.
- **Application-level dead-peer timer** → Phase 4 — the timer fires
  on the reader thread that doesn't exist yet in the standalone
  session.
- **Captured-from-ffmpeg connect-flow byte stream replay** under
  `tests/data/rtmp/` — synthetic round-trip through the
  `FakeServer` covers every protocol divergence the captured
  fixture would catch.  Captured streams will land when a real
  ffmpeg / nginx-rtmp interop tour reveals a server-specific
  quirk worth pinning.
- **Localhost nginx-rtmp publish smoke test** under
  `utils/promeki-test/` → Phase 5, where the MediaIO backend gives
  a realistic end-to-end pipeline.

**Deviations from the plan:**
- **`Amf0Value` destructor / copy ops / private-ctor moved
  out-of-line** in `amf0.h` + `amf0.cpp`.  The previous inline
  defaults instantiated `SharedPtr<Amf0Data>::~SharedPtr` at every
  include site through the forward-declared `Amf0Data` struct,
  which fired `-Wdelete-incomplete` in any TU that constructed an
  `Amf0Value`.  Out-of-lining keeps the destructor inside
  `amf0.cpp` where `Amf0Data` is fully defined.  Latent issue from
  Phase 0 — picked up by the Phase 3 test file's heavy
  `Amf0Value`-by-value usage.
- **`Metadata` parsing from `onMetaData` is a placeholder** — the
  session detects the script-data path and emits the `onMetaData`
  signal with an empty `Metadata`.  Walking the AMF0 object into
  concrete `Metadata::*` keys (`width`, `height`,
  `videocodecid`, etc.) lands with Phase 5, where `RtmpMediaIO`
  consumes the metadata for `proposeOutput`.  The signal hook is in
  place so the upstream caller can subscribe today.
- **Test path:** `tests/unit/network/rtmpsession.cpp` (vs the
  plan's `tests/unit/rtmpsession.cpp` — typo) for consistency with
  the rest of the network protocol tests.

## Phase 4 — Standalone client classes (publisher + subscriber)

**Status (2026-05-10):** Phase 4 is **complete**.  RtmpClient landed
with 10 doctest cases (44 assertions).  End-to-end publish round-trip
via a local TCP fixture (`FakeRtmpServer` reused across phases) is
green; a second loopback fixture (`FakeRtmpServerZeroTxn`) covers the
RTMP §7.2.2 `onStatus` txnId=0 path.  Full ctest passes at
5747 cases / 122,521 assertions; zero warnings in our code.

**Bugfix iteration (2026-05-10) — successful 1080p29.97 YouTube Live stream:**
- `splitPath`: single-segment URL paths (e.g. `rtmp://a.rtmp.youtube.com/live2`)
  now map the segment to `app` (not `streamKey`).  YouTube's AMF0 `connect`
  call rejects an empty `app`; the stream key is supplied out-of-band via
  `MediaConfig::RtmpStreamKey` or `publish()`'s argument.
- `RtmpSession::handleInboundCommand`: `onStatus` correlation now handles
  `txnId=0` (the real-world path per RTMP §7.2.2).  When `txnId` is zero
  the handler scans `_pending` for a transaction whose `expectedMsid` matches
  the inbound message-stream-id.  `PendingTransaction` gains `expectedMsid`
  and `commandName` fields; all callers (`connect`, `createStream`, `publish`,
  `play`) stamp them.
- Comprehensive `promekiWarn` instrumentation throughout
  `rtmpchunkstream.cpp`, `rtmpclient.cpp`, and `rtmpsession.cpp` for all
  error branches; previously silent failures now log context.

Files:
- [x] `include/promeki/rtmpclient.h`
- [x] `src/network/rtmpclient.cpp`
- [x] `tests/unit/network/rtmpclient.cpp`

`RtmpClient` is the convenience wrapper around `TcpSocket` /
`SslSocket` + `RtmpSession` + `RtmpUrl` parsing. It does not assume
MediaIO; an application that just wants to push some bytes can use
this class directly. `RtmpMediaIO` (Phase 5) is built on top of it.

```cpp
class RtmpClient : public ObjectBase {
                PROMEKI_OBJECT(RtmpClient, ObjectBase)
        public:
                /// Convenience: parse url, open the right socket, run
                /// handshake + connect.  Sync entry point — blocks
                /// until connected or timeoutMs elapses.
                Error           open(const Url &url,
                                     const RtmpConnectOptions &opts = {},
                                     unsigned int timeoutMs = 10000);

                /// Configure peer-verification against the URL's host
                /// when the scheme is rtmps.
                void            setSslContext(SslContext::Ptr ctx);

                /// Begin publishing a stream.  Blocks until the server
                /// acks NetStream.Publish.Start.
                Error           publish(const String &streamKey,
                                        const String &mode = "live",
                                        unsigned int timeoutMs = 5000);

                /// Begin subscribing to a stream.  Some servers
                /// (notably Wowza Streaming Engine in some
                /// configurations) require an `FCSubscribe`
                /// command to be issued before `play`; setting
                /// @p useFcSubscribe = true sends it.  Default
                /// false (matches OBS / ffmpeg).
                Error           play(const String &streamKey,
                                     unsigned int timeoutMs = 5000,
                                     bool useFcSubscribe = false);

                /// Sender-side write paths (used by RtmpMediaIO).
                Error           sendVideo(const FlvVideoTag &tag, uint32_t timestampMs);
                Error           sendAudio(const FlvAudioTag &tag, uint32_t timestampMs);
                Error           sendMetadata(const Metadata &meta, uint32_t timestampMs = 0);

                /// Receiver-side queues exposed for RtmpMediaIO.  Each
                /// returns Error::TryAgain when nothing is pending.
                Result<FlvVideoTag>  takeVideo(unsigned int timeoutMs);
                Result<FlvAudioTag>  takeAudio(unsigned int timeoutMs);
                Result<Metadata>     takeMetadata(unsigned int timeoutMs);

                Error           close();

                // Stats
                int64_t         bytesSent() const;
                int64_t         bytesReceived() const;
                int64_t         videoMessagesSent() const;
                int64_t         audioMessagesSent() const;
                Duration        rttEstimate() const;       // from PingRequest/Response

                PROMEKI_SIGNAL(connected);
                PROMEKI_SIGNAL(disconnected, Error);
                PROMEKI_SIGNAL(metadataReceived, Metadata);
};
```

**Implementation notes:**

- The send / receive thread topology described in §Architecture lives
  inside this class. `RtmpMediaIO` instantiates one `RtmpClient`,
  hands it off, and only ever calls the high-level send / take APIs
  (it never touches `RtmpSession` directly).
- The send-side internal queue is a single bounded
  `Queue<RtmpMessage>` (configurable via `RtmpSendQueueDepth`;
  default `64`). At 60 fps video + ~47 AAC frames/sec, 64 messages
  is roughly **600 ms** of in-flight buffering — enough to absorb
  a TCP congestion-window stall without losing a frame, but small
  enough that backpressure visibly throttles upstream before the
  destination's wallclock skew grows unbounded. The writer thread
  pops via `popBlocking`, serializes via
  `RtmpChunkStream::writeMessage`, and bumps stats. Backpressure:
  when the queue is full, the per-kind packetizer thread sees
  `Error::Timeout` from `pushBlocking(timeoutMs)` and reports it up
  to the strand as `Error::TryAgain`, exactly the same shape
  `MediaIOSink::writeFrame` uses for its capacity gate.
- The recv-side dispatch: the reader thread parks on
  `RtmpSession::readMessage`. Audio / video messages are translated
  into `FlvAudioTag` / `FlvVideoTag` via `flvtag.h`'s `unpack`
  before being pushed onto a per-kind `Queue<...>` for the strand to
  drain.
- TLS path: when the URL scheme is `rtmps`, the client owns an
  `SslSocket` instead of `TcpSocket`, attaches the configured
  `SslContext` (system CA bundle by default), sets the SNI
  hostname to the URL's host (required by YouTube / FB ingests
  which serve multiple RTMPS endpoints from one IP), drives
  `startEncryption` to completion via the standard
  `continueHandshake` loop, then hands the encrypted device to
  `RtmpSession::attach`. Everything above the device sees ordinary
  bytes.
- TCP keepalive `setKeepAlive(true)` is on by default. `setNoDelay`
  is **on** by default (driven by `RtmpStartTcpNoDelay=true`) — at
  modern bitrates an RTMP message is ~1–8 KiB and Nagle's
  measurement-RTT-bound delay (40 ms typical) shows up in
  end-to-end latency without any compensating batching benefit.
  Override via the MediaConfig key for high-cadence publishes that
  prefer batching, or to fall back to legacy behavior.

**Tests:**

- [x] **Loopback round-trip (txnId echo):** `FakeRtmpServer` fixture in
      `tests/unit/network/rtmpclient.cpp` drives the server-side
      handshake + connect-flow inside the same process.  The test
      publishes a synthetic AVC video + AAC audio sequence and
      asserts the fixture parses ≥3 video + ≥3 audio messages back
      into `RtmpMessage`s.  The fixture is currently inline in the
      test file rather than the planned shared
      `tests/unit/rtmpserverfixture.h` — moving it out as a shared
      header lands when Phase 5 (`RtmpMediaIO`) tests need to reuse
      it.
- [x] **Loopback round-trip (txnId=0 / §7.2.2 path):**
      `FakeRtmpServerZeroTxn` sends `onStatus` with `txnId=0.0`
      (matching real YouTube / Twitch / nginx-rtmp behavior per RTMP
      §7.2.2); exercises the `expectedMsid` scan path in
      `RtmpSession::handleInboundCommand`.
- [x] **splitPath corner cases:** 5 subcases — empty path, two-segment,
      multi-segment, single-segment (YouTube `live2` form), trailing slash.
- [x] **URL scheme rejection:** `http://` and empty-host URLs are
      rejected with `Error::InvalidArgument`.
- [x] **Refused-connection cleanup:** opening to a port nothing is
      listening on returns an error and leaves `isOpen()` false.
- [x] **Unopened-client guards:** `sendVideo` / `sendAudio` /
      `publish` / `takeVideo` on an idle client return
      `Error::Invalid`; `close` on a never-opened client is a no-op.
- [x] **Idempotent close:** repeated close calls succeed without
      crashing.
- [ ] **Auth failure paths** (server rejects `connect` with
      `_error`) — covered at the RtmpSession layer in Phase 3 via
      the `NetStream.Publish.Denied → PermissionDenied` test.
      Wiring the same error path through RtmpClient is a small
      additive doctest that we'll add when the
      `RtmpMediaIO`-layer auth-rejection error surfacing is
      exercised by Phase 5.
- [x] **Reconnect-after-drop** is explicitly **not** in scope — the
      client surfaces a disconnection error via the `disconnected`
      signal and the application reopens.  RTP doesn't
      auto-reconnect either; symmetric.

**Deviations from the plan / carry-overs:**
- **`waitForReadyRead` added to `AbstractSocket`** (small foundation
  fix surfaced by this phase): the chunk-stream layer needs a
  pollable wait on the socket fd to honor read timeouts.  The
  IODevice default returned `false` immediately, which made
  read-with-timeout effectively spin.  `AbstractSocket` now overrides
  `waitForReadyRead` to poll the underlying fd with `POLLIN`; this
  is the right place for the override since every socket subclass
  benefits from it.
- **Writer / reader thread startup is deferred until @c publish or
  @c play succeeds.**  The synchronous AMF0 round-trips inside
  `RtmpSession::connect` / `createStream` / `publish` / `play` pump
  `_session->readMessage` from the foreground caller; a concurrent
  reader thread would race for inbound messages on the same chunk
  stream.  The threads start only once the steady-state media-pump
  phase begins.  This matches the devplan's "RtmpWriterThread /
  RtmpReaderThread serve the media phase" diagram, just made
  explicit.
- **`sendMetadata` emits an empty `@setDataFrame` + `onMetaData`
  AMF0 envelope** — the transport mechanics are exercised; the
  metadata-object population happens in Phase 5 once
  `RtmpMediaIO`'s `MediaDesc` consumer is in place.
- **`rttEstimate()` returns 0** — Phase 5's writer-side periodic
  keepalive emitter populates this.
- **`tests/unit/rtmpserverfixture.{h,cpp}`** is **not** factored out
  yet — the `FakeRtmpServer` lives inline in the test file.  Lifting
  it to a shared fixture lands with Phase 5 testing.

## Phase 5 — RtmpMediaIO

**Status (2026-05-10):** Phase 5 first cut is **landed**.  `RtmpMediaIO`
publishes H.264 + HEVC + AAC end-to-end via the `RtmpClient` machinery
from Phase 4; source-mode (play) is wired with a basic depacketizer
loop.  6 new doctest cases (~80 assertions) under
`tests/unit/network/rtmpmediaio.cpp` cover factory registration,
URL→config, `objectId` uniqueness, end-to-end publish via
`FakeRtmpServer`, missing-URL rejection, and refused-connection
cleanup.  Full ctest green: 5716 cases / 115,328 assertions; zero
warnings in our code.

**Update (2026-05-10):** Mid-stream peer-disconnect handling
landed.  `RtmpMediaIO` now hooks `RtmpClient::disconnectedSignal`
and latches the reason; the packetizer and depacketizer worker
loops exit cleanly on the latched flag instead of busy-spinning
on `Error::Invalid` results from `sendVideo`/`takeVideo`, and the
strand-side `executeCmd(Write)` / `executeCmd(Read)` paths
surface the captured `Error` (defaulting to `BrokenPipe` when the
disconnect was clean) so `MediaIOPortConnection::writeErrorSignal`
cascades the failure to the pipeline.  Repro: live publish to a
mediamtx server that gets terminated mid-stream — previously the
sink object kept the stage open and emitted per-frame
"video send failed: Invalid argument" warnings; now the pipeline
tears down the RTMP stage on the next write.  One regression
test added (`rtmpmediaio.cpp`: peer-disconnect mid-stream surfaces
as writeFrame error).

**Update (2026-05-10) — robustness round:**

- **Sink-side `RtmpRepeatParameterSets` enforcement.**  Cached
  parameter-set NALs are now prepended ahead of every IDR whose
  Annex-B access unit doesn't carry them inline.  Late-joining
  subscribers (mediamtx LL-HLS, browser MSE consumers) recover
  cleanly without waiting for the encoder's next out-of-band
  sequence-header refresh.
- **Source-side `MediaDesc` recovery from `SequenceHeader`.**  The
  depacketizer parses `avcC` / `hvcC` and `AudioSpecificConfig`
  into the on-frame descriptors so resolution and audio
  rate/channels flow downstream automatically.
- **Source-side parameter-set propagation.**  A `ParameterSet`-
  flagged `CompressedVideoPayload` carrying the VPS/SPS/PPS NALs
  is now emitted from each `SequenceHeader` tag so out-of-band
  decoders can prime before any NALU frame lands.
- **`onMetaData` pass-through.**  Inbound script-data is emitted
  as a metadata-only Frame on the reader queue instead of being
  silently dropped.

**Known limitation (RTMP wire format):** RTMP timestamps are
32-bit milliseconds, so fractional source rates (29.97, 59.94,
etc.) inherently produce ±1 ms part-duration jitter on the wire
— at 29.97 fps with a 6-frame GOP the keyframe stride lands at
200.2 ms in source time, which floors to alternating 200/201 ms
on consecutive GOP boundaries.  mediamtx's LL-HLS muxer logs a
`part duration changed from 200ms to 201ms` warning per such
transition.  This is benign for non-iOS-LL-HLS subscribers and
unavoidable without a higher-resolution wire format; integer-rate
sources (30, 60, etc.) sidestep it entirely.

Files:
- [x] `include/promeki/rtmpmediaio.h`
- [x] `src/proav/rtmpmediaio.cpp` *(under proav, mirroring `rtpmediaio.cpp`)*
- [x] `tests/unit/network/rtmpmediaio.cpp` *(under `network/` to
      match the rest of the RTP / TLS protocol tests)*

The big piece. `RtmpMediaIO` derives from `DedicatedThreadMediaIO`
(same as `RtpMediaIO`) and behaves like RtpMediaIO at the MediaIO
layer:

- Source and sink mode supported. `ReadWrite` rejected.
- Configuration via `MediaConfig` keys from Phase 0 §8.
- `proposeInput` / `proposeOutput`: declares the compressed
  `PixelFormat` / `AudioFormat` set we accept on the wire (H.264 +
  HEVC + AAC). Uncompressed inputs upstream are bridged in by the
  planner inserting a `VideoEncoderMediaIO` / `AudioEncoderMediaIO`
  stage. Same model as today's RTP path with H.264 / HEVC.
- `MediaIOStats` keys (mirroring `RtpMediaIO`'s
  `StatsFramesSent` / `StatsBytesSent` / etc.):

```
StatsFramesSent / FramesReceived
StatsVideoMessagesSent / Received
StatsAudioMessagesSent / Received
StatsBytesSent / BytesReceived
StatsSendQueueDepth / ReadQueueDepth    // current depth of the bounded queues
StatsSendQueueOverflows                 // count of writeFrame calls that hit Error::TryAgain
StatsRttUs                              // RTT from ping-response
StatsHandshakeDurationMs
StatsConnectDurationMs
StatsLastDisconnectError                // last terminal Error before disconnect; auto-reconnect itself is out of scope (see Phase 4 caveat / "Out of scope")
StatsTimestampWrapEvents                // count of 32-bit timestamp rollovers (~49.7 days each)
StatsTxVideoIntervalUs (Histogram)
StatsTxVideoEncodeUs (Histogram)
StatsTxAudioIntervalUs (Histogram)
StatsRxVideoIntervalUs (Histogram)
StatsRxAudioIntervalUs (Histogram)
StatsRxFrameAssembleUs (Histogram)
StatsAudioSilenceFramesEmitted          // when source stalls; mirrors RTP
StatsVideoFramesDroppedPreIdr           // sink-side: count of access units dropped while waiting for the first IDR after publish (RtmpDropUntilKeyframe)
StatsPacingTicksOnTime / Late           // PacingGate verdict counters (video, sink only); mirrors RtpMediaIO's gate exposure
StatsPacingTicksSkipped                 // count of frames dropped by the gate (lag past skip threshold)
StatsPacingReanchors                    // count of timeline re-anchors (lag past reanchor threshold)
StatsPacingSlackUs (Histogram)          // signed slack (deadline − now) sampled on every wait()
StatsPacingClockKind                    // String tag: "internal" / "external" / "none" — reflects the live gate binding, not the configured mode
```

- `MediaIOCommandParams` actions (analogous to RTP's `GetSdp`):
  - `GetServerOnConnect`: returns the AMF0 object the server
    responded with on connect — useful for logging the negotiated
    `objectEncoding`, `data.version`, etc.
  - `GetMetadata`: returns the most recent `onMetaData` AMF0 object
    seen by the reader.

**Sink (publish) path:**

1. `executeCmd(Open)` parses `RtmpUrl`, builds the `RtmpConnectOptions`
   from MediaConfig keys, instantiates `RtmpClient`, attaches
   `SslContext` if `rtmps`, calls `RtmpClient::open` (handshake +
   connect), then `RtmpClient::publish`.
2. The first frame's `MediaDesc` drives encoder / converter selection:
   - For raw video input the planner inserts a `VideoEncoderMediaIO`
     upstream; we configure it with `RtmpVideoBitrate`,
     `RtmpKeyframeIntervalSec`, and the chosen backend pin. We
     receive `CompressedVideoPayload` from then on. Encoder is
     configured with **B-frames disabled** by default — most live
     destinations are happier with PTS == DTS, and the
     composition-time-offset path below covers the cases where they
     are not.
   - For AAC: the planner inserts an `AudioEncoderMediaIO` and, when
     the upstream sample rate isn't a rate AAC + FLV can carry
     cleanly, an `AudioResamplerMediaIO` ahead of the encoder. The
     **resample target is 48 kHz by default** (the modern-destination
     canonical rate; YouTube / Twitch / FB all consume 48 kHz
     happily). 44.1 kHz is also acceptable on the wire — selectable
     via a future `RtmpAudioSampleRate` config key, deferred until
     someone hits a destination that requires 44.1 specifically.
     HE-AAC v1 / v2 use implicit SBR doubling so the encoded sample
     rate may be half the input rate (24 kHz → 48 kHz); the FLV
     `soundrate` field carries the encoded rate, the
     `AudioSpecificConfig` carries the actual sample rate. We end up
     receiving `CompressedAudioPayload` regardless of upstream
     sample format / rate.
3. **Video pacing on the strand.** Mirrors
   `RtpMediaIO::paceVideoFrame` exactly.  A single
   `PacingGate _videoPaceGate` member, armed by `executeCmd(Open)`
   from the `RtmpVideoPacing` enum:
   - `Internal` (default) → `_videoPaceGate.setClock(SystemWallClock::create())`
     and `setPeriod(MediaConfig::FrameRate.frameDuration())`.  No
     external clock required; the strand paces the first frame at
     `now()` and every subsequent frame at `anchor + n × period`.
   - `External` → no clock bound at `Open`.  The gate stays a
     no-op until `executeCmd(MediaIOCommandSetClock)` arrives
     from the planner (typically forwarded from a capture-card
     MediaIO upstream whose port-group `clock()` is the device
     clock).  A null clock from `setClock` re-arms back to the
     `Internal` policy when `RtmpVideoPacing=Internal`, or stays
     a no-op when `RtmpVideoPacing=External`.
   - `None` → gate is never armed.  Strand floods at upstream
     rate; only the bounded `MessageQueue` provides backpressure.
   Called on the strand immediately *after* the encoder produces
   a `CompressedVideoPayload` (so the encoder's variable cost
   doesn't double-pay against the deadline) and *before* the
   per-kind PayloadQueue push.  Audio is **not** paced by this
   gate — AAC's natural per-frame cadence (1024 samples → ~21 ms
   at 48 kHz) lands inside the video frame interval anyway, and
   the bounded `MessageQueue` keeps audio + video in roughly the
   same wall-clock window once video is paced.  Verdicts:
   - `OnTime` / `Late` → proceed.
   - `Skip` → drop the frame, increment
     `StatsPacingTicksSkipped`, call `noteFrameDropped(portGroup(0))`,
     bump `_frameCount`, return `Error::Ok` (matches RTP).
   - `Reanchor` → log a `promekiWarn`, increment
     `StatsPacingReanchors`, proceed with the frame.
   `executeCmd(MediaIOCommandSetClock)` swaps the gate's clock
   on the strand and returns `Error::Ok`; the framework's
   `completeCommand` then updates `MediaIOPortGroup::clock()`
   so callers observe the live binding.  Returns
   `Error::NotSupported` in source mode (RX timing is driven by
   network arrival).
4. **First-IDR gating.** Per `RtmpDropUntilKeyframe` (default `true`),
   non-keyframe video access units arriving before the first IDR
   after `publish` succeeds are silently dropped — most destinations
   reject a stream that begins on an inter-frame and refuse to
   recover for the rest of the connection. Audio is *not* gated;
   audio-only prelude is fine. `StatsAudioSilenceFramesEmitted` and
   a new `StatsVideoFramesDroppedPreIdr` counter both surface the
   gating activity.
5. **Sequence header + parameter-set policy.** The first IDR access
   unit triggers an `AvcDecoderConfig::fromAnnexB` (or
   `HevcDecoderConfig::fromAnnexB`) build, then a single
   `FlvVideoTag` of `packetType = SequenceHeader` with the
   serialized `avcC` / `hvcC` payload — sent before any `Nalu`
   packet. Subsequent access units are `Annex-B → AVCC` via
   `H264Bitstream::annexBToAvcc` (HEVC analogue for `hvc1`), framed
   as `packetType = Nalu`. When `RtmpRepeatParameterSets=true`
   (default) we also keep the SPS/PPS (or VPS/SPS/PPS) NAL units
   inline in the AVCC payload of every IDR, so a subscriber that
   joins mid-stream can decode the next IDR without waiting for the
   next out-of-band sequence-header refresh.
6. **Composition-time / DTS handling.** The packetizer computes
   `compositionTimeOffsetMs = pts() - dts()` from
   `MediaPayload::pts()` / `dts()` directly (no helper method
   today; computed inline at framing time). For B-frame-bearing
   HEVC inputs the field is non-zero and goes straight onto the
   wire as the FLV `compositionTime`. Where the encoder didn't
   populate DTS (`dts().isInvalid()`), CTO is `0` and we stamp the
   message timestamp with the PTS. This covers both the common
   no-B-frame path *and* the Enhanced-RTMP HEVC case where
   B-frames are typical without forcing the encoder to disable
   them — the only requirement is that when B-frames are present,
   the encoder populate DTS.
7. The first AAC packet triggers an `AacDecoderConfig::fromAudioDesc`
   build → `FlvAudioTag` `aacPacketType = SequenceHeader`. Subsequent
   AAC frames are `aacPacketType = Raw`.
8. `onMetaData` is dispatched once, immediately after `publish`
   succeeds, populated from the `MediaDesc` (width, height,
   videocodecid, audiocodecid, framerate, audiosamplerate,
   audiochannels, encoder string from `Application::userAgent`).
9. Per-frame timestamp: 32-bit **milliseconds**, monotonically
   increasing per stream, derived from `Frame::captureTime` for
   audio and from the encoder's PTS for video. The millisecond
   quantization is a hard ceiling on RTMP's audio-sync precision —
   sub-ms accuracy is not recoverable on the wire. For audio at
   48 kHz that's ~48 samples of jitter per packet, well below the
   threshold any subscriber DSP will care about, but worth noting
   for calling code that compares RTMP timestamps to RTP-side ones.
   RTMP timestamps wrap at 32 bits (~49.7 days). The chunk-stream
   layer uses the extended-timestamp escape (24-bit field exhausted
   → 32-bit extra field) which is mandatory for any stream past the
   first 4.66 hours of wall time anyway, so wrap handling and
   extended-timestamp emission share the same code path. We
   increment `StatsTimestampWrapEvents` on each wrap and reset the
   per-stream extended-timestamp delta tracking; the wire stays
   continuous from the receiver's perspective.

**Source (play) path:**

The codec / desc of a source-mode RTMP play is **not known until
the first SequenceHeader arrives**, exactly like the RTP RX path.
The `proposeOutput` shape during `executeCmd(Open)` therefore
declares the *union* of what we can emit — `CompressedVideoPayload`
in `H264` or `HEVC`, `CompressedAudioPayload` in `AAC`, and
`Metadata` — and the planner reconciles the concrete codec when the
first frame's `MediaDesc` lands. This is the same model RTP RX
uses today; no new planner machinery is required.

1. `executeCmd(Open)` performs handshake + connect + `play` (with
   optional `FCSubscribe` for Wowza interop, gated by an opt-in
   MediaConfig key).
2. The reader thread drains FLV tags via `RtmpClient::takeVideo` /
   `takeAudio` / `takeMetadata`, fans them into the per-kind
   depacketizer threads.
3. Video depacketizer:
   - on `SequenceHeader`: parse the `avcC` / `hvcC` blob into an
     `AvcDecoderConfig` / `HevcDecoderConfig`; cache parameter sets;
     emit `CompressedVideoPayload(ParameterSet)` so a downstream
     decoder (NvdecVideoDecoder, etc.) can prime.
   - on `Nalu`: iterate the AVCC NAL list
     (`H264Bitstream::forEachAvccNal` equivalent — already in tree),
     build `CompressedVideoPayload`. **Output framing is AVCC by
     default** since that's what RTMP carries on the wire and most
     downstream decoders (NVDEC, ffmpeg's `h264_cuvid`) accept it
     directly. When `RtmpEmitAnnexB=true` the depacketizer reframes
     to Annex-B with start-code prefixes; this is opt-in because
     the planner can also insert a converter MediaIO when an
     Annex-B-only consumer is downstream and we'd rather avoid the
     copy when no one needs it.
   - on `EndOfSequence`: emit a `MediaPayload::Flags::EndOfStream`
     marker.
4. Audio depacketizer:
   - on AAC `SequenceHeader`: parse `AacDecoderConfig`, derive
     `AudioDesc`, cache for downstream.
   - on AAC `Raw`: emit `CompressedAudioPayload` carrying the raw
     AAC frame; downstream is responsible for decoding (the planner
     inserts `AudioDecoderMediaIO` for raw output).
5. Metadata depacketizer: `onMetaData` payload → `Amf0Reader` →
   `Amf0Value` → walk known fields into a `Metadata` and emit on
   the data stream.
6. Aggregator thread merges per-kind output into a `Frame` stream;
   matches `RtpAggregatorThread`'s shape closely. The same Aggregator
   mode (Video / AudioOnly / DataOnly) selection happens at thread
   start.

**Threading model:**

| Thread                          | Owns                                  |
|---------------------------------|---------------------------------------|
| Strand (DedicatedThreadMediaIO) | executeCmd, descriptor cache          |
| Per-kind packetizer (3, sink)   | encoding + FLV framing                |
| RtmpWriter (sink)               | TCP / TLS write side                  |
| RtmpReader (source)             | TCP / TLS read side                   |
| Per-kind depacketizer (3, src)  | FLV unframing + payload build         |
| Aggregator (source)             | Frame emit                            |

`Atomic<int64_t>` counters across thread boundaries; `Histogram`
single-writer / single-reader; bounded `Queue<>` between every stage.
Cross-thread shutdown via the existing `Queue::cancelWaiters` /
`requestStop` pattern that already proved out under RTP.

**Tests:**

- [x] Doctest unit tests against an in-tree mock `RtmpServer`
      fixture (lifted inline into the test file mirroring the
      Phase 4 `FakeRtmpServer`).  Validates handshake → connect →
      publish → message exchange end-to-end, sequence-header
      ordering (AVC + AAC), and keyframe / inter-frame routing.
- [x] **Pacing doctests** (in `tests/unit/network/rtmpmediaio.cpp`):
      - `RtmpVideoPacing=None` — no gate waits, queue-bounded
        backpressure only.
      - `RtmpVideoPacing=Internal` paces against the configured
        `FrameRate` (100 fps test asserts the strand sleeps the
        expected accumulated interval before the third frame).
      - `RtmpVideoPacing=External` stays a no-op until
        `executeCmd(MediaIOCommandSetClock)` binds a clock, then
        switches to "external" (verified via `StatsPacingClockKind`),
        and reverts to "none" on detach.

      Carry-overs from the original plan (still TODO):
      - `Internal` + flood the strand faster than realtime —
        assert `StatsPacingTicksSkipped` matches the configured
        skip-threshold math and that `noteFrameDropped` is called.
      - `External` mode + bind a `ManualClock` via
        `executeCmd(MediaIOCommandSetClock)` mid-publish, advance
        it tick-by-tick, assert the strand only releases a frame
        per advance.  Then bind `nullptr` and assert the gate
        reverts to no-op (the `External` policy).
      - `None` mode — assert no `PacingGate` waits ever happen and
        a flood goes through at queue-bounded rate only.
- [ ] Functional test under `utils/promeki-test/cases/rtmp/`:
      - `rtmp.h264.aac.publish` — TPG → RtmpMediaIO sink → in-process
        sink server fixture → verify frames received.
      - `rtmp.hevc.aac.publish` — same with Enhanced RTMP.
      - `rtmp.h264.aac.play`    — fixture publish → RtmpMediaIO
        source → frame-level inspection.
      - `rtmp.tls.publish`      — RTMPS path, with an
        `SslContext` configured against a local self-signed cert.
      - `rtmp.handshake.fallback` — fixture rejects complex,
        client falls back to simple, publish completes.
      - `rtmp.h264.aac.publish.paced` — TPG feeding faster than
        realtime, `RtmpVideoPacing=Internal`, assert the wall-clock
        delivery rate at the fixture matches `FrameRate` within
        tolerance.
      - `rtmp.h264.aac.publish.captureclock` — feed a synthetic
        capture-card stub whose port-group exposes a controllable
        `Clock`, propagate via `setClock`, assert the destination
        sees the capture stub's cadence (not a wall clock).
- [ ] One real-network smoke test (gated by an env var) that
      publishes 10 seconds of TPG to a configurable RTMP destination
      and asserts the destination's read-back URL works. Off by
      default in CI.

**Deviations from the plan / carry-overs:**

- **Strand-side `PacingGate` wired (2026-05-10 follow-up).**  All
  three `RtmpVideoPacing` modes (`Internal` / `External` / `None`)
  are honored end-to-end, with the gate clock swappable mid-stream
  via `executeCmd(MediaIOCommandSetClock)` and the pacing stats
  (`StatsPacingTicksOnTime` / `Late` / `Skipped`, `StatsPacingReanchors`,
  `StatsPacingClockKind`) populated.  Doctest coverage is in
  `tests/unit/network/rtmpmediaio.cpp` (`RtmpVideoPacing=None
  disables pacing`, `=Internal paces against FrameRate`,
  `=External waits for setClock binding`).
- **Stats coverage subset.**  The cumulative counters (`StatsFramesSent`,
  `StatsBytesSent`, `StatsVideoMessagesSent`, etc.) and the
  `StatsConnectDurationMs` / `StatsHandshakeDurationMs` /
  `StatsVideoFramesDroppedPreIdr` / `StatsSendQueueOverflows` keys are
  populated; the RTT, timestamp-wrap-event, last-disconnect-error,
  and per-kind interval-histogram keys from the plan's table are
  **deferred** to a follow-up.  The current set is sufficient for
  smoke-level observability and matches what the in-tree counters
  already track.
- **`MediaIOCommandParams` not implemented.**  The plan's
  `GetServerOnConnect` and `GetMetadata` params commands are not
  wired up.  Land alongside the `Metadata`-population path
  (Phase 4 carry-over re: `RtmpClient::sendMetadata` body).
- **Sequence-header policy is best-effort.**  The sink emits the
  `avcC` / `hvcC` sequence header on the first IDR by extracting
  the SPS/PPS (and VPS for HEVC) from the Annex-B access unit.
  When the upstream encoder delivers AVCC bytes directly the
  sequence header is not yet built; the bitstream still flows.
  This matches what the planner's `VideoEncoderMediaIO` upstream
  typically produces today (Annex-B).
- **`RtmpRepeatParameterSets` is enforced (2026-05-10 follow-up).**
  The packetizer caches the parameter sets parsed from the first
  IDR's Annex-B access unit and, on every subsequent IDR that
  arrives without inline SPS/PPS (and VPS for HEVC), prepends the
  cached NALs as length-prefixed AVCC bytes ahead of the IDR
  slice.  A late-joining subscriber can therefore decode the
  next IDR without waiting for an out-of-band sequence-header
  refresh.  Annex-B input only — AVCC-direct encoder output is
  forwarded verbatim (parameter-set introspection on AVCC is the
  encoder's responsibility).  Doctest coverage:
  `tests/unit/network/rtmpmediaio.cpp` (`RtmpRepeatParameterSets=true
  injects SPS on bare IDRs` / `=false skips SPS injection`).
- **`RtmpDropUntilKeyframe` honored.**  Pre-IDR access units are
  dropped and counted via `StatsVideoFramesDroppedPreIdr`.
- **Source-mode `MediaDesc` derived from `SequenceHeader`
  (2026-05-10 follow-up).**  The depacketizer parses the inbound
  `avcC` / `hvcC` (and AAC `AudioSpecificConfig`) blobs, recovers
  resolution via `H264Bitstream::parseSpsResolution` /
  `HevcDecoderConfig::parseSpsResolution`, and stamps every
  subsequent `CompressedVideoPayload` / `CompressedAudioPayload`
  with the recovered `ImageDesc` / `AudioDesc`.  The strand-side
  `mediaDescChanged` / `updatedMediaDesc` plumbing is not yet
  wired, but the per-payload descriptors carry the truth.
- **Source-mode parameter-set propagation (2026-05-10 follow-up).**
  The depacketizer emits a `CompressedVideoPayload(ParameterSet)`
  whose AVCC bytes hold the parsed VPS/SPS/PPS NALs, so decoders
  that prefer an out-of-band primer (NVDEC, ffmpeg's
  `h264_cuvid`) can configure themselves before any NALU frame
  lands.  Decoders that already cope with in-band parameter sets
  see one extra ParameterSet-flagged Frame and are otherwise
  unaffected.
- **`onMetaData` pass-through (2026-05-10 follow-up).**  The
  depacketizer drains `RtmpClient::takeMetadata` and emits each
  blob as a metadata-only Frame onto the reader queue.  Mapping
  the AMF0 object into specific `Metadata` keys is informational
  and not driven by any consumer today; the raw object is on the
  Frame's metadata for callers that want to inspect it.
- **`RtmpMediaIO`'s `MediaConfig::Filename` fallback.**  Not
  implemented; users open RTMP streams via `MediaConfig::RtmpUrl`
  or `MediaIO::createFromUrl`.
- **Captured-from-real-server fixtures** under `tests/data/rtmp/`
  not yet landed (same carry-over as Phases 1-3).
- **Cumulative-aggregate `setName` thread label** lands per the
  `Thread::setName` API rather than the plan's `setObjectName`.

## Phase 6 — Documentation + integration

- [ ] `docs/rtmp.md` — architecture reference matching `docs/srt.md`'s
      shape: design rationale, threading diagram, interop notes
      (which servers we've validated against, common pitfalls like
      "Twitch refuses simple handshake"), tested codec / role matrix.
- [ ] `devplan/README.md` index entry.
- [ ] `CMakeLists.txt`:
      - [ ] `option(PROMEKI_ENABLE_RTMP …)` defaulting `ON` when
            `_NETWORK` is on.
      - [ ] `option(PROMEKI_USE_SYSTEM_FDKAAC …)` defaulting `OFF`.
      - [ ] Compile-conditional inclusion of the new sources.
      - [ ] Bundle script for fdk-aac per Phase 0 §6.
      - [ ] `target_compile_definitions(promeki PRIVATE
            PROMEKI_ENABLE_RTMP PROMEKI_ENABLE_AAC)` when enabled.
- [ ] `include/promeki/config.h.in` exposes `PROMEKI_ENABLE_RTMP` to
      consumers.

No dedicated `demos/rtmp-demo/` — `utils/mediaplay/` already drives
URL outputs through `MediaIOFactory::createFromUrl`, so the smoke-test
surface is `mediaplay <pipeline.json>` where the sink stage has an
`rtmp://` / `rtmps://` path.  Same model as the SRT sink testing.

## Out of scope / deferred

Tracked explicitly so future plans can pick them up without
re-deriving context:

- **Server roles** — `RtmpServer`, per-connection
  `RtmpServerConnection`, `RtmpServerMediaIO` (ingest), and the
  server-side `play` path. The protocol classes
  (`RtmpHandshake`, `RtmpChunkStream`, `RtmpSession`) are
  **role-agnostic by construction** in v1 — they take an `RtmpRole`
  and operate identically on either side. Adding the server is
  therefore a pure additive layer: an accept loop, a per-connection
  strand mirroring `RtmpClient`, and a `RtmpServerMediaIO` that
  exposes accepted streams as MediaIO sources / sinks. Files / API
  will mirror the client side; `SrtServer` is the working model.
  Roughly parallels Phase 4 in code volume.
- **RTMPE** — Adobe RC4 obfuscation. Easy to add given that mbedTLS
  has RC4; gated behind `PROMEKI_ENABLE_RTMPE` so the cipher only
  links when explicitly requested.
- **RTMPT / RTMPTE** — HTTP-tunneled RTMP. Reuses the existing
  HttpServer / HttpClient classes; adapter layer that takes raw
  RTMP byte streams and packages them in POST / GET response bodies
  per the (informal) Adobe spec.
- **AMF3** — needed only when peers negotiate ObjectEncoding = 3 in
  the connect packet; we send 0. Adding AMF3 is its own ~400-line
  encoder/decoder pair plus reference-table machinery; the RtmpSession
  is wired so that the moment we support it, simply bumping
  `RtmpConnectOptions::objectEncoding` to 3 unlocks it without
  protocol-class edits.
- **Enhanced RTMP VP9 / AV1** — the FlvVideoTag layer accepts the
  fourcc-based extended header so the framing supports them today,
  but we don't ship a VP9 / AV1 video encoder backend in v1. Once
  we do (e.g. wire libvpx or libdav1d), enabling them is a
  one-line `RtmpVideoCodec` registration.
- **Reconnect / resume** — application-driven for v1. `RtmpClient`
  surfaces clean disconnect signals; the wrapping pipeline decides
  whether and how to reconnect. A resilient-sink `MediaIO`
  transformer (like FFmpeg's `-reconnect_at_eof`) is a separate
  feature spanning more than RTMP.
- **Multi-track audio** (>1 audio stream per RTMP connection) —
  RTMP supports it via additional message-stream ids but no major
  destination consumes them; not in v1.
- **Trick-modes on play** — pause, seek, set-buffer-length on a
  source-mode RtmpMediaIO. Trivial protocol additions but require
  MediaIOCommandSeek wiring; deferred.
- **AAC LATM / xHE-AAC** — only AAC-LC / HE-AAC / HE-AAC v2 in v1.
- **CDN-specific auth schemes (Akamai / Limelight / Wowza secure
  token / "Adobe-style" `secureToken`)** — these wedge a challenge /
  response pair into the AMF0 `connect` body or surface as a
  `NetStream.Authenticate.UsherToken` `onStatus`. The session
  surfaces `Error::AuthenticationRequired` when it sees one of the
  known shapes; producing the token response is destination-specific
  and not in v1. Token-bearing query parameters that destinations
  pass through verbatim (`?token=…` on the URL) work today via the
  existing query preservation path — only the AMF0-embedded
  challenge/response variants are deferred.

## Implementation order summary

```
Phase 0  ── library foundations ──┐  ✅ complete (2026-05-10)
                                  │
                                  ▼
Phase 1 ── RtmpHandshake ────► tests  ✅ complete (2026-05-10)
                                  │
                                  ▼
Phase 2 ── RtmpChunkStream ───► tests  ✅ complete (2026-05-10)
                                  │
                                  ▼
Phase 3 ── RtmpSession ──────────► tests  ✅ complete (2026-05-10)
                                  │
                                  ▼
Phase 4 ── RtmpClient ───────────► tests + demo  ✅ complete (2026-05-10)
                                  │
                                  ▼
Phase 5 ── RtmpMediaIO ──────────► doctest landed ✅ first cut (2026-05-10);
                                  │                  PacingGate, parameter-set
                                  │                  repeat, source-side desc /
                                  │                  metadata wired (2026-05-10);
                                  │                  promeki-test matrix +
                                  │                  remaining stats keys +
                                  │                  MediaIOCommandParams TODO
                                  ▼
Phase 6 ── docs + CMake + demo wiring   (next)
```

Phase 0 carry-overs (small items deferred into later phases, called
out here so they don't get lost):

- App-vs-stream-key path-split helper (§8) → Phase 4 (`RtmpClient`).
- `MediaIOFactory` registration for `rtmp` / `rtmps` (§8) → Phase 5
  (`RtmpMediaIO`).
- `PROMEKI_ENABLE_RTMP requires PROMEKI_ENABLE_AAC` CMake guard
  (§6) → Phase 6 (when the `PROMEKI_ENABLE_RTMP` option itself is
  added).
- `redactedString()` adoption at every `PROMEKI_LOG` site (§9) →
  Phases 3 – 5 (lands alongside the log statements themselves).
- Captured-from-real-server AMF0 byte streams under
  `tests/data/rtmp/` (§3) → Phase 1 / 2 / 3 when handshake / chunk
  / session tests need them.
- HE-AAC v1 / v2 + 960-sample-frame test coverage (§6) → Phase 5
  (the same matrix add that exercises `RtmpAudioBitrate` ×
  `RtmpAudioCodec`).

Server roles are listed under Out of scope / deferred; the protocol
classes from Phases 1–3 are role-agnostic so they don't need
revisiting when server work begins.

## References

- Adobe RTMP Specification 1.0 (2012): the canonical PDF, kept as
  the on-the-wire normative.
- `enhanced-rtmp.org` v1 (2023): codec extension framework for
  HEVC / VP9 / AV1.
- ffmpeg `librtmp/` and `librtmp` upstream — reference C
  implementation of the complex handshake (the digest-key dance is
  notoriously under-documented in the official spec).
- nginx-rtmp-module — reference server implementation for testing
  client behavior under load.
- OBS Studio source — reference of which connect-flow shape the
  major destinations actually accept.
- Existing libpromeki RTP devplans: `devplan/network/rtp-tx.md`,
  `devplan/network/rtp-rx.md`, `devplan/network/srt.md` — same
  authoring conventions and threading model patterns used here.
