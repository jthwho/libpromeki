# Inspector: `PcmMarker` audio-channel decoder

`InspectorMediaIO` should grow a decoder for the TPG's
`AudioPattern::PcmMarker` output — the sample-domain inverse of the
generator. Gives sample-exact round-trip verification (bit flips,
dropped / duplicated / reordered chunks all become visible), the
audio-pipeline analog of the picture `ImageDataDecoder`.

The generator framing is already defined in
`AudioTestPattern::kPcmMarker*` constants:

- 16-sample alternating preamble.
- 8-sample start marker (four highs + four lows).
- 64-bit MSB-first payload at ±0.8.
- Trailing parity bit at ±0.6.

## Tasks

- [ ] Hunt for the preamble; latch on first match.
- [ ] Walk the start marker; reject and retry on parity / shape
  mismatch.
- [ ] Decode the 64-bit payload and validate the parity bit.
- [ ] Report the decoded value to the Inspector event alongside the
  existing LTC / picture-data results.
- [ ] When the payload looks like a BCD64 timecode, parse via
  `Timecode::fromBcd64`; otherwise expose the raw 64-bit counter.
- [ ] Tests round-trip TPG → Inspector and verify recovered values.
