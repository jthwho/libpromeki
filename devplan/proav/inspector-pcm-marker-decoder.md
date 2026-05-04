# Inspector: `PcmMarker` audio-channel decoder

**COMPLETE** — shipped in the `AudioDataEncoder` / `AudioDataDecoder` +
inspector `AudioData` test changeset.

## What shipped

- `AudioDataEncoder` — Manchester-encoded 76-bit codeword (4 sync +
  64 payload + 8 CRC-8/AUTOSAR), one encoder per
  (AudioDesc, samplesPerBit, amplitude) triple.  Handles every PCM
  format (interleaved + planar) via pre-built primer buffers.

- `AudioDataDecoder` — sync-nibble run-length measurement + integrate-
  and-compare Manchester demodulation.  ±50 % pitch tolerance absorbs
  ordinary SRC drift.  Streaming `decodeAll(StreamState&, ...)` API for
  cross-chunk codeword reassembly.  Per-band `decode(payload, band)` for
  batch-style callers.

- Wire format unified with TPG video data band:
  `[stream:8][channel:8][frame:48]` (MSB-first, big-endian payload
  bytes for CRC).  `AudioTestPattern::PcmMarker` replaced the old
  bespoke preamble / start-marker / parity-bit framing.

- `InspectorTest::AudioData` (default-on) — per-channel
  `AudioDataDecoder::StreamState`, per-channel active latch, new
  `InspectorEvent::AudioChannelMarker` struct, and three new
  `InspectorDiscontinuity::Kind` values:
  - `AudioChannelMismatch` — encoded channel byte ≠ physical channel.
  - `AudioDataDecodeFailure` — sync-detected codeword with bad CRC or
    sync nibble.
  - `AudioDataLengthAnomaly` — measured codeword length deviates
    substantially from the expected span.

- Tests: `tests/unit/audiodataencoder.cpp` (13 cases),
  `tests/unit/audiodatadecoder.cpp` (19 cases including 16-phase SRC
  round-trips and multi-codeword streaming), plus inspector integration
  tests in `tests/unit/mediaiotask_inspector.cpp`.

For design notes see `docs/` (doxygen) and the class-level comments in
`include/promeki/audiodataencoder.h` / `include/promeki/audiodatadecoder.h`.
