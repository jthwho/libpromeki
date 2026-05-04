# NVENC / NVDEC Backend

**Library:** `promeki` (gated by `PROMEKI_ENABLE_NVENC` / `PROMEKI_ENABLE_NVDEC`)
**Status:** Functionally complete; HDR + multi-format + timecode SEI + VUI
+ NVDEC SEI round-trip all landed. Functional test (`nvenc-functest`)
runs **92 passed / 0 failed / 2 skipped** on real hardware.
**Standards:** All work follows `CODING_STANDARDS.md`; every change requires
unit tests where the helper is exercisable without GPU.

The full historical narrative — the panic investigations, the three
pre-panic fixes, the second interlaced-panic investigation, and the
detailed list of files touched — lives in git history (commit
`44c696f` and earlier). What remains is the follow-up backlog.

## Remaining work

### Encoder / pipeline plumbing

- **Zero-copy GPU path.** Current `uploadFrame` is a host-staging
  `nvEncLockInputBuffer` + memcpy. Add `nvEncRegisterResource` +
  `nvEncMapInputResource` so a CUDA-resident `Image` buffer goes
  straight to NVENC without a host round-trip.
- **Dynamic `nvEncReconfigureEncoder`.** `_needReconfigure` is recorded
  but never acted on; wire it so bitrate / RC mode / keyframe-interval
  changes via `configure()` take effect at the next IDR.
- **Stream-level HDR re-emit on reconfigure.** Today `configure()`
  captures `_masteringDisplay` / `_contentLightLevel` per call, but the
  HDR block is sent only once at session init. Once
  `nvEncReconfigureEncoder` is wired, re-send the HDR metadata block.
- **Alpha-layer encoding.** `_caps.supportAlpha` is populated but no
  consumer; revisit when an alpha-bearing input format and a downstream
  consumer both exist.
- **`MediaIOTask_VideoEncoder` AV1 registration verification.** Confirm
  the generic mediaiotask resolves `"AV1"` end-to-end now that the
  string-keyed and typed `VideoCodec::AV1` factories are both wired.

### Pro-video features still missing

- **HDR transfer auto-derivation.** `ColorModel` doesn't yet distinguish
  HDR transfer curves (PQ / HLG) from SDR gamma, so `ColorModel::toH273()`
  always returns an SDR transfer. HDR callers must set
  `VideoTransferCharacteristics::SMPTE2084` (HDR10) or `ARIB_STD_B67`
  (HLG) explicitly. Resolve once `ColorModel` gains HDR-curve awareness.
- **Sample / display aspect ratio.** `nvEncInitializeEncoder` hardcodes
  `darWidth = _width; darHeight = _height` (square pixel only).
  Anamorphic SD / DVCPRO HD / 4:3-storage-on-16:9-display bitstreams
  emit the wrong aspect in VUI. Add a MediaConfig key for DAR or pull
  SAR from `ImageDesc` / `VideoFormat`.
- **True interlaced (PAFF) encoding.** Today `pic.pictureStruct` is
  always `NV_ENC_PIC_STRUCT_FRAME`; the bitstream is frame-coded with
  `pic_struct` carried in pic_timing / Time Code SEI only. Spec-strict
  decoders treat the output as progressive. The two interlaced
  encode→decode round-trip cases in `nvenc-functest` are `SKIP`-ed
  for this reason. Implementing field-paired submits with
  `frameFieldMode = FIELD` and `NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM` /
  `_BOTTOM_TOP` is the substantial follow-up.
- **Caption / AFD / bar-data SEI.** Plumb CEA-608/708 closed captions
  (payloadType 4, user_data_registered_itu_t_t35), AFD, and bar data
  through `NV_ENC_PIC_PARAMS::*PicParams::seiPayloadArray`. Needs a
  `Metadata::ClosedCaptions` Buffer key produced by an upstream
  VANC-decoding stage. Decode side mirrors via NVDEC's
  `pfnGetSEIMsg` (currently dropped on the `default` branch).
  AV1 path goes through `NV_ENC_PIC_PARAMS_AV1::obuPayloadArray` /
  ITU-T T.35 OBU.
- **Timecode `Mode` inference on decode.** `parseH264PicTiming` /
  `parseHevcTimeCode` currently pick `NDF30` / `DF30` from
  `cnt_dropped_flag` alone; for 24 / 25 / 50 / 59.94 / 60 fps the
  digits are right but `tc.mode().fps()` is wrong, so `++tc` wraps at
  the wrong boundary. Thread `CUVIDEOFORMAT::frame_rate` through
  `handleSequence` to the SEI parsers.

### Test coverage backlog

The pure-CPU helpers below are GPU-free and fuzzable; doctest targets
that don't need hardware. Several would have caught past bugs.

- NVENC side: `toNvencRc`, `toNvencPreset`, `toNvencTuning`,
  `h264ProfileGuid`, `h264Level`, `hevcLevel`, `av1Level`,
  `toNvencMastering`, `toNvencCll`, `toNvencTimeCode`,
  `populateVuiColorDescription`, `Impl::resolveColorDescription`.
- NVDEC side: `BitReader`, `parseClockTimestamp`, `parseH264PicTiming`,
  `parseHevcTimeCode`, `parseMasteringDisplaySei`,
  `parseContentLightLevelSei`. The SEI parsers in particular accept
  attacker-shaped input and deserve assertions against truncation /
  overflow.
- `ColorModel::toH273` is already covered (in
  `tests/unit/colormodel.cpp`); the rest follow the same pattern.
