# MPEG-2 Transport Stream — COMPLETE (v1)

**Library:** `promeki` (feature flag `PROMEKI_ENABLE_PROAV`)
**Landed:** 2026-05-31

Full MPEG-TS mux / demux stack from wire primitives through
transport-agnostic frame I/O, with file and SRT transport backends.

---

## Shipped

### Wire primitives (`mpegts.h/cpp`)

`MpegTs` — static helpers for the fixed 188-byte packet format:

- Packet layout constants: `PacketSize`, `SyncByte`, `NullPid`,
  `PatPid`; reserved fields `MaxPid`, `MaxContinuityCounter`.
- PID allocation sentinels: `DefaultVideoPid` (0x100),
  `DefaultAudioPid` (0x101), `DefaultPmtPid` (0x1000).
- PSI table builders: `buildPat`, `buildPmt` (with
  `registration_descriptor` for SMPTE 302M).
- PES packet assembler: `buildPesHeader` with optional PTS / DTS.
- Adaptation field writer: `buildAdaptationField` with PCR.
- TS packet finaliser: `buildTsPacket` (stuffing to 188 bytes,
  continuity counter increment, payload-unit-start-indicator).
- Registration format identifiers: `RegFormatH264`, `RegFormatHevc`,
  `RegFormatSmpte302M`.
- Helper: `MpegTs::StreamInfo` (pid, stream_type, PES stream_id,
  registration_descriptor bytes).

### Demuxer (`mpegtsdemuxer.h/cpp`)

`MpegTsDemuxer` — stateful 188-byte packet parser:

- `feed(BufferView)` — consume a raw byte run; returns the count of
  TS packets processed (or `Error::InvalidData` on sync loss).
- PID auto-detect from PAT → PMT.  Stream types recognised:
  H.264 (0x1B), HEVC (0x24), AAC-ADTS (0x0F), AAC-LATM (0x11),
  SMPTE 302M (0x06 + "BSSD" registration_descriptor), MP3 (0x03).
- PES reassembly with continuity-counter tracking; duplicate /
  out-of-order TS packets handled gracefully.
- PCR extraction and callback: `setPcrCallback`.
- Access-unit delivery: `setStreamCallback(pid, callback)` or
  broadcast `setStreamCallback(callback)`; each `AccessUnit` carries
  pid, stream_type, pts, dts, and a `BufferView` over the reassembled
  PES payload.
- Streams introspection: `streams()` returns the parsed PMT table
  after PAT → PMT probe.

### Muxer (`mpegtsmuxer.h/cpp`)

`MpegTsMuxer` — stateful TS packet emitter:

- `addStream(pid, stream_type, pes_stream_id, registration_descriptor)`
  — register an elementary stream before writing.
- `writePat` / `writePmt` — emit PSI tables on demand.
- `writeAccessUnit(pid, pts_90khz, dts_90khz, BufferView, pcr_pid)`
  — PES-packetise + TS-packetise one access unit; inserts PCR in
  the adaptation field of the first TS packet when `pcr_pid` matches.
- `writePcr(pid, pcr_value)` — standalone PCR-only null packet.
- Output routed through `setPacketCallback` (called once per 188-byte
  packet) — transport is not owned by the muxer.
- `continuityCounters()` accessor for state inspection in tests.

### SMPTE ST 302M (`smpte302m.h/cpp`)

`Smpte302M` — AES3 / linear PCM → MPEG-2 TS PES encoder/decoder:

- `encode(AudioDesc, BufferView samples) → Buffer` — packs
  16 / 20 / 24-bit PCM (48 kHz, 1–8 channels in 1–4 AES3 streams)
  into a 302M PES payload with the 4-byte BSSD header.
- `decode(Buffer payload, AudioDesc &outDesc) → Buffer` — strips
  the header and unpacks the bit-packed AES3 words back to plain
  interleaved PCM.
- Validates channel-count / sample-width constraints per §5.2–5.4.

### Frame-level glue (`mpegtsframer.h/cpp`)

`MpegTsFramer` — transport-agnostic bridge between `Frame` objects
and the mux / demux byte stream:

- `writeFrame(Frame)` — maps `CompressedVideoPayload` /
  `CompressedAudioPayload` onto TS PIDs; handles first-frame
  PAT+PMT emission and the configurable PAT/PMT re-emission cadence.
- `feedBytes(BufferView)` — pushes raw TS bytes into the demuxer;
  emits reassembled `Frame` objects via `setFrameCallback`.
- `configure(MediaConfig)` — picks up PID assignments,
  PAT/PMT/PCR cadence, CBR mux rate from `MediaConfig` keys.
- Probes H.264 / HEVC SPS for real `ImageDesc` dimensions so emitted
  Frames carry accurate geometry.
- PTS synthesis from configured `FrameRate` when payloads don't
  carry explicit timing.

### MediaIO backends

- `MpegTsFileMediaIO` — bidirectional `File`-backed TS I/O.  Sink
  accepts compressed video + audio; source reads .ts files and emits
  `Frame`s.  Registered as `"MpegTsFile"` with extensions `.ts`,
  `.m2ts`.
- `SrtMediaIO` — same framer wired to `SrtSocketTransport`.
  Caller / Listener / Rendezvous modes; `MediaConfig` keys parallel
  `MpegTsFileMediaIO` for consistency.  Registered as `"Srt"`.

### Tests

- `tests/unit/mpegts.cpp` — packet-level helpers, PAT/PMT builders,
  PES header, continuity counter rollover.
- `tests/unit/mpegtsframer.cpp` — framer + demuxer integration:
  round-trip of H.264 / HEVC / AAC / 302M payloads; PCR callback;
  stream discovery; SPS dimension probe.
- `tests/unit/mpegtsmuxer.cpp` — muxer + demuxer round-trips for
  multiple stream types; PAT/PMT cadence; PCR insertion.
- `tests/unit/smpte302m.cpp` — 302M encode / decode for 16/20/24-bit
  × 2/4/6/8 channels.
- `tests/unit/mediaiotask_mpegtsfile.cpp` — MediaIO-level pipeline
  test: encode H.264 → MpegTsFileMediaIO sink → source → decode.
- `tests/unit/srtmediaio.cpp` — SrtMediaIO loopback handshake and
  basic open/close lifecycle.
- `tests/unit/jpegxsbitstream.cpp` — JPEG XS bitstream framing
  (co-landed; JPEG XS payloads are a future TS stream type).

---

## Deferred Items

- [ ] **Multi-program TS** — current muxer / framer supports exactly one program (one video PID + one audio PID).  PAT / PMT can enumerate multiple programs but the allocation logic is 1:1.
- [ ] **SMPTE 302M demux in MpegTsFramer** — `Smpte302M::decode` exists and is unit-tested but `MpegTsFramer::feedBytes` does not yet route 302M PES payloads through it; uncompressed audio from .ts sources comes out as `UncompressedAudioPayload` only after this is wired.
- [ ] **CBR null-packet stuffing** — `MpegTs::MuxRateBps` MediaConfig key is plumbed but the null-packet insertion loop in `MpegTsFramer::writeFrame` is not yet implemented; VBR output only.
- [ ] **DVB subtitles / SCTE-35 MPEG-TS carriage** — stream_type 0x06 is used for 302M; subtitle / cue-tone PIDs would need their own registration_descriptors.
- [ ] **`promeki-test` functional test** — full encode → file → decode end-to-end with real compressed A/V content; too slow for unit scope.
- [ ] **MPEG-TS source: seeking** — `MpegTsFileMediaIO` source does not support `seek`; random-access on TS files needs a PCR-indexed seek table.
