# QuickTime: little-endian float audio storage

**Files:** `src/proav/mediaiotask_quicktime.cpp` (`pickStorageFormat`),
`src/proav/quicktime_writer.cpp` (`pcmFourCCForDataType`)

**FIXME:** Little-endian float (`AudioFormat::PCMI_Float32LE`) has no
single-FourCC mapping in the QuickTime sample-entry format. The
QuickTime `fl32` FourCC is big-endian float; for little-endian float
the spec requires the generic `lpcm` FourCC plus a `pcmC` extension
atom describing endianness and the "sample is float" flag.

Current workaround in `QuickTimeMediaIO::pickStorageFormat()` promotes
incoming `PCMI_Float32LE` sources to `PCMI_S16LE` for storage —
widely compatible but lossy (32-bit float → 16-bit int).

## Tasks

- [ ] Either:
  - (a) Emit a proper `lpcm` sample entry with a `pcmC` extension
    atom carrying endianness + sample-is-float flags so little-endian
    float can be stored natively, **or**
  - (b) Promote to `PCMI_Float32BE` (byte-swap on write, use `fl32`
    FourCC) instead of dropping to s16 — preserves bit depth.
- [ ] Regardless of which path, drop the lossy promotion from
  `pickStorageFormat()` and update the round-trip tests to verify
  float precision.
- [ ] Audit the writer's `pcmFourCCForDataType` fallthrough — the
  `lpcm` default is currently a trap (silent data-format mismatch)
  unless `pcmC` is also emitted.
