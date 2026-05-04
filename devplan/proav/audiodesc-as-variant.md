# Promote `AudioDesc` to a Variant-registered first-class type

`AudioDesc` is currently a plain value class holding sample rate,
channels, and format — unlike `PixelFormat`, `ColorModel`, and
`PixelMemLayout`, which are registry-backed `TypeRegistry` wrappers
with stable IDs and a Variant type tag.

Plan:

1. Pick whichever pieces of `AudioDesc` identify a "named" format
   (e.g. `PCMI_Float32LE @ 48 k @ stereo`) and promote those to
   registered IDs.
2. Add `TypeAudioDesc` to the Variant X-macro so `MediaConfig` keys
   can use `setType(Variant::TypeAudioDesc)` directly.

The current `MediaConfig` workaround uses the parallel
`OutputAudioRate` / `OutputAudioChannels` / `OutputAudioDataType`
keys. Once `TypeAudioDesc` lands, those can collapse to a single
`OutputAudioDesc` key.

## Tasks

- [ ] Identify the AudioDesc fields that count toward "named"
  identity (rate / channels / format / channel map?).
- [ ] Add the TypeRegistry pattern to `AudioDesc` and a
  `Variant::TypeAudioDesc` slot.
- [ ] Wire JSON / DataStream round-trip for the new Variant alt.
- [ ] Migrate the parallel `OutputAudio*` keys where it makes sense.
