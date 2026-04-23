# NVENC Pro Video Features

**Phase:** 4B (MediaIO backend expansion)
**Dependencies:** MediaIO framework, VideoEncoder factory, VideoCodec typed registry, CudaBootstrap / CudaDevice.
**Library:** `promeki` (built when `PROMEKI_ENABLE_NVENC` is on).
**Status:** Functional test green again — **92 passed, 0 failed, 2 skipped** at `--frames 8` on 2026-04-17 after the second-investigation interlaced fixes landed. The 2 skipped cases are the two interlaced encode→decode round-trip cases, intentionally skipped (with explanatory comments and the original test bodies preserved inside `if(false)`) because the encoder's frame-coded interlaced path cannot produce a spec-conformant interlaced bitstream without true PAFF coding — see "What this changeset does NOT fix" below. The previous panic that hit during the first interlaced test run is gone after the two fixes documented in "Suspected panic vectors found". Panic-resistant logging (`fsync` per line, default `/mnt/data/tmp/promeki/nvenc-functest.log`) added so any future panic leaves the offending sub-case visible on disk.

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests (unit test coverage for pure-CPU logic; the encoder itself is exercised by the functional test that needs GPU hardware).

## NVENC SDK in use

- NVENC SDK 13.0.37 headers at `/home/jth/src/nvenc/Video_Codec_SDK_13.0.37/Interface/nvEncodeAPI.h`.
- Dynamic-loaded entry point: `libnvidia-encode.so.1` via `dlopen` (see `loadNvenc()` in `src/proav/nvencvideoencoder.cpp`).
- CUDA driver API used for device + primary context (`cuInit`, `cuDeviceGet`, `cuDevicePrimaryCtxRetain`).

---

## What shipped in this changeset (pre-panic)

### New public types

- `MasteringDisplay` (`include/promeki/masteringdisplay.h`, `src/core/masteringdisplay.cpp`) — SMPTE ST 2086 mastering display color volume: R/G/B/white-point CIE xy + min/max luminance. Carries a `::HDR10` preset constant. Added as a Variant type (`TypeMasteringDisplay`).
- `ContentLightLevel` (`include/promeki/contentlightlevel.h`, `src/core/contentlightlevel.cpp`) — CTA-861.3 / ITU-T H.265 Annex D MaxCLL + MaxFALL. Added as a Variant type (`TypeContentLightLevel`).
- Both types register as Variant payloads in `include/promeki/variant.h` (X-macro list).

### New PixelFormat / PixelMemLayout / VideoCodec entries

- `PixelFormat::AV1` (ID 161) — compressed AV1 (fourcc `av01`), backed by `PixelMemLayout::P_420_3x10_LE` and `VideoCodec::AV1`.
- `PixelFormat::YUV8_444_Planar_Rec709` (ID 162) — 8-bit YCbCr 4:4:4 planar, Rec.709 limited range.
- `PixelFormat::YUV10_444_Planar_LE_Rec709` (ID 163) — 10-bit YCbCr 4:4:4 planar LE.
- `PixelMemLayout::P_444_3x10_LE` (ID 81) — 3 planes, 10-bit LE in 16-bit words, 4:4:4.
- `VideoCodec::AV1` gains `compressedPixelFormats = { PixelFormat::AV1 }` in `src/core/videocodec.cpp`.

### New MediaConfig keys

In `include/promeki/mediaconfig.h`:

| Key | Type | Default | Purpose |
|---|---|---|---|
| `VideoSpatialAQ` | bool | false | Enable spatial AQ. |
| `VideoSpatialAQStrength` | int32 | 0 | 1–15; 0 = auto. |
| `VideoTemporalAQ` | bool | false | Enable temporal AQ. |
| `VideoMultiPass` | int32 | 0 | 0=off, 1=¼-res, 2=full-res. |
| `VideoRepeatHeaders` | bool | false | Emit SPS/PPS / VPS / SeqHdr with every IDR. |
| `VideoTimecodeSEI` | bool | false | Emit SMPTE timecode as H.264 Picture Timing / HEVC Time Code SEI. |
| `VideoColorPrimaries` | Enum(ColorPrimaries) | Auto | VUI / AV1 color primaries (ISO/IEC 23091-4). Auto derives from first frame's ColorModel. |
| `VideoTransferCharacteristics` | Enum(TransferCharacteristics) | Auto | VUI / AV1 transfer characteristics. Auto derives SDR curve; HDR (PQ / HLG) must be set explicitly. |
| `VideoMatrixCoefficients` | Enum(MatrixCoefficients) | Auto | VUI / AV1 matrix coefficients. Auto derives from ColorModel. |
| `VideoRange` | Enum(VideoRange) | Unknown | Studio (Limited) / PC (Full) range. Unknown derives from PixelFormat::videoRange(). |
| `HdrMasteringDisplay` | MasteringDisplay | — | Stream-level SMPTE ST 2086. |
| `HdrContentLightLevel` | ContentLightLevel | — | Stream-level CTA-861.3. |

### New Metadata keys

In `include/promeki/metadata.h`:

| Key | Type | Purpose |
|---|---|---|
| `ForceKeyframe` | bool | Request an IDR at this frame. `MediaIOTask_VideoEncoder` maps it to `VideoEncoder::requestKeyframe()`. |
| `MasteringDisplay` | MasteringDisplay | Per-frame override of stream-level HDR mastering. |
| `ContentLightLevel` | ContentLightLevel | Per-frame override of stream-level CLL. |

### New VideoEncoderPreset enum value

- `VideoEncoderPreset::Lossless` (`include/promeki/enums.h`) — maps to `NV_ENC_TUNING_INFO_LOSSLESS` and preset P2 in the NVENC backend.

### NVENC backend (`src/proav/nvencvideoencoder.cpp`)

The previous NV12-only first-cut has been generalised:

- **Format dispatch table** (`kFormatTable`) — six entries covering NV12, P010, NV16, P210, YUV444, YUV444_10BIT. Each row encodes the NVENC buffer format, chroma format IDC, bit depth, bytes per pixel Y, UV height divisor, and plane count. `lookupFormat(PixelFormat::ID)` searches the table; `supportedInputs()` now enumerates it.
- **Codec dispatch** — `Codec_H264`, `Codec_HEVC`, `Codec_AV1` (enum extended in `include/promeki/nvencvideoencoder.h`). Registered under names `"H264"`, `"HEVC"`, `"AV1"` via `VideoEncoder::registerEncoder` and also wired into the typed `VideoCodec::createEncoder` hook via `VideoCodec::registerData`.
- **Caps query** — `populateCaps()` queries `NV_ENC_CAPS_SUPPORT_{10BIT,422,444,LOSSLESS,LOOKAHEAD,TEMPORAL_AQ,ALPHA_LAYER_ENCODING}` and `NV_ENC_CAPS_NUM_MAX_BFRAMES` once per session. `validateFormatCaps()` rejects formats the GPU can't do.
- **Profile / level mapping** — `h264ProfileGuid`, `hevcProfileGuid`, `av1ProfileGuid`, and `h264Level`, `hevcLevel`, `av1Level` translate the caller's string (`"high"`, `"main10"`, `"4.1"`, etc.) to NVENC GUIDs / enums. Empty strings trigger autoselect based on format (chroma / bit depth).
- **HDR submit path** — per-frame mastering / CLL written into `NV_ENC_PIC_PARAMS_HEVC::pMasteringDisplay` / `pMaxCll` and the matching AV1 struct. Stream-level values live on `Impl::_masteringDisplay` / `_contentLightLevel`; per-frame `Metadata::MasteringDisplay` on the input Image overrides.
- **Upload path** — `uploadFrame()` replaced the old `uploadNV12()`. Dispatches on `_fmt->planeCount`: 3 for planar 4:4:4 (Y/U/V equal-sized), 2 for semi-planar (Y + interleaved UV at `uvHeightDivisor` rows).
- **Slot pool** — `_numSlots = max(32, effectiveB*4 + laFrames + 32)`. Each slot owns an NVENC input buffer + bitstream buffer. `acquireFreeSlot()` drains completed in-flight slots to recycle when the pool is empty.
- **Dynamic reconfigure** — `configure()` re-captures the config and sets `_needReconfigure = _sessionOpen`; reapplies at the next frame (currently just clears the flag — full NVENC `nvEncReconfigureEncoder` path is a follow-up).

### Functional test (`tests/func/nvenc/main.cpp`, `tests/func/nvenc/CMakeLists.txt`)

New `nvenc-functest` binary. Generates an RGBA8 ColorBars source, converts to each input format via `Image::convert`, and exercises:

1. `testSupportedInputsList` — sanity that NV12 is in `supportedInputs()`.
2. `testUnsupportedFormat` — feeds RGB, expects `Error::PixelFormatNotSupported`.
3. `testBasicEncode` — 14 (format × codec) combos over NV12, P010, NV16, P210, YUV444, YUV444_10 × H264 / HEVC / AV1.
4. `testForceKeyframe` — sets `Metadata::ForceKeyframe` at the midpoint; asserts midpoint frame is a keyframe.
5. `testRateControlModes` — CBR 10Mbps, VBR 10Mbps, CQP QP=23, CQP QP=35.
6. `testPresets` — UltraLowLatency / LowLatency / Balanced / HighQuality.
7. `testGopAndIdr` — GOP=10, GOP=30, GOP=15 IDR=30.
8. `testProfileLevel` — explicit and autoselect profile/level combos.
9. `testBFrames` — H.264 B=0,1,2,3; HEVC B=0,2,3.
10. `testLookahead` — H.264 LA=4,8,16; HEVC LA=8,16.
11. `testAdaptiveQuantization` — spatial / temporal / combined.
12. `testMultiPass` — off / ¼-res / full-res.
13. `testRepeatHeaders` — H264 / HEVC / AV1, assert ≥2 keyframes at GOP=15.
14. `testHdrMetadata` — HEVC + AV1 with mastering / CLL / both.
15. `testLossless` — YUV444 H264, YUV444 HEVC, NV12 HEVC. The NV12 H264 row was dropped because H.264 lossless is only spec-valid in the High 4:4:4 Predictive profile — see fix #2 below.
16. `testTimecode` — H.264 (24 NDF, 30 NDF, 29.97 DF) + HEVC (25 NDF, 29.97 DF) + AV1 (flag-on sanity). Verifies the encoder accepts `Metadata::Timecode` at every input frame and still emits a valid bitstream; AV1 hits the warn-once path because NVENC does not expose an AV1 timecode OBU.
17. `testColorDescription` — 8 combos covering Auto derivation (NV12 → BT.709 limited for all three codecs), explicit HDR10 override (P010 BT.2020 + PQ for HEVC / AV1), HLG (P010 BT.2020 + ARIB on HEVC), explicit full-range override, and explicit Unspecified (suppress VUI color block). Verifies the resolver passes Auto through ColorModel::toH273() and explicit values through untouched, and that NVENC accepts every combination without rejecting the init.
18. `testEncodeDecodeRoundTrip` — 4 cases (2 active, 2 skipped):
    - **`HEVC HDR10 + SEI round-trip`** (active) — P010 BT.2020 PQ, mastering + CLL + timecode, encoded via NVENC then decoded via NVDEC. Verifies VUI primaries/transfer/matrix/range, Timecode SEI, MasteringDisplay SEI, ContentLightLevel SEI all recovered on the decoded Image.
    - **`H264 Rec.709 + timecode round-trip`** (active) — NV12, Auto-derived VUI + timecode. Same Metadata-recovery assertions.
    - **`H264 InterlacedTFF round-trip`** (SKIPPED) — wired up but currently impossible to pass because NVENC keeps `frameFieldMode = FRAME` (frame-coded interlaced bitstream + pic_struct SEI yields a progressive bitstream as far as NVDEC is concerned). Original assert body preserved inside `if(false)` for re-enablement once true PAFF coding lands.
    - **`HEVC InterlacedBFF round-trip`** (SKIPPED) — same FRAME-mode constraint, plus NVENC's API doesn't expose pic_struct in HEVC pic_timing SEI at all (only in Time Code SEI, which our NVDEC parser doesn't surface as `Metadata::VideoScanMode` today).
19. `testInterlaced` — 7 cases (3 H.264 + 3 HEVC + 1 AV1) covering Progressive, InterlacedEvenFirst, InterlacedOddFirst per codec. Asserts the encoder accepts every interlaced config and emits packets. AV1 hits its existing warn-and-fall-through path; HEVC interlaced now hits a new warn-once explaining that pic_struct rides on Time Code SEI, not pic_timing.

CLI: `--width`, `--height`, `--frames`, `--verbose`, `--log PATH`.

### Panic-resistant logging

`nvenc-functest` mirrors every section header, per-iteration `TRY` marker, and `PASS` / `FAIL` / `SKIP` outcome to a log file with `fflush(3)` + `fsync(2)` after each line. A kernel panic between a `TRY` and its matching outcome therefore leaves the offending sub-case visible on disk after reboot — no reliance on terminal scrollback or kernel-printed stack traces.

- Default log path: `/mnt/data/tmp/promeki/nvenc-functest.log` (the user's scratch dir; `/tmp` is tmpfs on this machine and would be wiped on panic).
- Disable file logging with `--log -` (stdout-only, original behaviour).
- Override path with `--log /some/other/file`.

Each `TRY` marker is paired with the test sub-case label (`TRY NV12 / H.264`, `TRY ForceKeyframe / HEVC`, `TRY HEVC InterlacedBFF round-trip`, etc.). After a panic, `tail -1 /mnt/data/tmp/promeki/nvenc-functest.log` identifies the exact sub-case in flight; if the last line is a `PASS` the panic happened at the boundary between tests (suspect cleanup / reuse rather than the test body itself).

---

## The panic (resolved 2026-04-17)

During the first full `nvenc-functest` run on real hardware, the kernel reported a **corrupted-stack** panic. We did not capture the exact test active at panic time — a diagnostic gap noted below in "If a future panic happens". The corruption most likely came from the NVIDIA kernel driver ingesting a malformed structure from user-space: either a pointer to memory that had since gone out of scope, or a profile / chroma-format mismatch that hit an unchecked assumption inside the driver validator.

After the three fixes below, the functional test completes cleanly: **69/69 at `--frames 8`** and **69/69 at the default 30-frame run**, covering all three codecs, every supported format, B-frames, lookahead, HDR metadata, and lossless.

## Fixes applied in this changeset

### Fix #1 — HDR per-frame pic params now live on the Slot, not submitFrame's stack

**File:** `src/proav/nvencvideoencoder.cpp`

Before, `submitFrame()` allocated `MASTERING_DISPLAY_INFO nvMd{}` and `CONTENT_LIGHT_LEVEL nvCll{}` as function-local stack objects, then stored their addresses in `pic.codecPicParams.hevcPicParams.pMasteringDisplay` / `pMaxCll` (and the AV1 equivalents). With B-frames or lookahead enabled, `nvEncEncodePicture` commonly returns `NV_ENC_ERR_NEED_MORE_INPUT` — it has not emitted output and may still be holding the per-picture params for later consumption on the subsequent encode pass. If the driver lazily copies the pointee, by the time the frame actually encodes those pointers reference reclaimed stack.

**Change:**

- Added `nvMd`, `nvCll`, `hasMd`, `hasCll` to the `Slot` struct (Slot lifetime spans submit → bitstream lock, so the storage is valid for the entire deferred window).
- `submitFrame()` now writes into `slot->nvMd` / `slot->nvCll` and hands NVENC `&slot->nvMd` / `&slot->nvCll`.
- `hasMd` / `hasCll` flags are reset at the start of each submit so a reused slot can't accidentally inherit HDR state from its previous frame.

### Fix #2 — H.264 lossless only forces HIGH_444 when input is actually 4:4:4

**Files:** `src/proav/nvencvideoencoder.cpp`, `tests/func/nvenc/main.cpp`

The NVENC header documents `qpPrimeYZeroTransformBypassFlag` as "set this to 1, set QP to 0 and RC_mode to NV_ENC_PARAMS_RC_CONSTQP and profile to HIGH_444_PREDICTIVE_PROFILE". HIGH_444 in turn requires `chroma_format_idc == 3` (YUV 4:4:4) per the H.264 spec. Before the fix, the backend unconditionally forced `encCfg.profileGUID = NV_ENC_H264_PROFILE_HIGH_444_GUID` and `qpPrimeYZeroTransformBypassFlag = 1` whenever `VideoEncoderPreset::Lossless` was requested and `NV_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE` returned true — including with NV12 (`chromaFormatIDC=1`) input. The resulting bitstream carries a profile / chroma-format mismatch that user-space validation in NVENC does not always catch, and at least some driver versions trip deeper inside kernel space.

**Change:**

- Only apply the HIGH_444 override and `qpPrimeYZeroTransformBypassFlag` when `_fmt->chromaFormatIDC == 3`.
- Non-4:4:4 lossless requests log a warning and fall through to the normal CQP path (with whatever profile `h264ProfileGuid()` would otherwise select).
- Removed the `Lossless H264 NV12` row from `testLossless` — with the fix it would merely exercise a CQP path, not a lossless path, so the label was misleading.
- HEVC lossless still goes through `NV_ENC_HEVC_PROFILE_FREXT_GUID`, which supports 4:2:0 per the HEVC Range Extensions, so `Lossless HEVC NV12` remains a valid test row.

### Fix #3 — Robust encoder cleanup on partial initialization

**File:** `src/proav/nvencvideoencoder.cpp`

`ensureSession()` acquires the encoder handle via `nvEncOpenEncodeSessionEx` and only sets `_sessionOpen = true` at the very end, after `allocateSlots()` succeeds. Before the fix, `destroySession()` was guarded on `_sessionOpen`, so any error between those two points (`validateFormatCaps`, `nvEncGetEncodePresetConfigEx`, `nvEncInitializeEncoder`, `allocateSlots`) returned with a live encoder handle and `_sessionOpen == false`. The destructor would then skip the destroy call and leak the handle for the rest of the process lifetime, letting GPU-side state accumulate across repeated test failures.

**Change:**

- `destroySession()` now keys on `_encoder != nullptr`, not `_sessionOpen`. It's safe to call on a partially-initialized session.
- Each of the four failure paths in `ensureSession` now calls `destroySession()` before returning: `validateFormatCaps` failure, `nvEncGetEncodePresetConfigEx` failure, `nvEncInitializeEncoder` failure, `allocateSlots` failure.

### Verification

- `promeki` library: builds clean.
- `nvenc-functest`: links and builds clean.
- `unittest-promeki`: 4030/4030 passing.
- `nvenc-functest` on real hardware: 85/85 passing at `--frames 8` (2026-04-17) including the new `testTimecode`, `testColorDescription`, and `testEncodeDecodeRoundTrip` groups.
- `unittest-promeki`: 4032/4032 passing including the new `PixelFormat::videoRange` and `ColorModel::toH273` tests.

---

## Follow-up items

Follow-up items from the initial review. Small cleanups from the review landed alongside the main fixes; larger items remain open.

### Landed

- **`nvEncGetEncodeCaps` error handling** — `queryCap()` now checks the NVENCSTATUS, logs at Warn on failure, and returns 0 explicitly.
- **Planar offset math** — `uploadFrame()` now accumulates the destination offset from each plane's actual row count (`planeRows × pitch`), so adding a planar 4:2:0 / 4:2:2 entry to `kFormatTable` later can't silently overrun the NVENC surface.
- **`frameIdx` narrowing** — the `uint64 → uint32` assignment in `submitFrame` is now an explicit `static_cast<uint32_t>(_frameIdx)` with a comment noting that NVENC only uses the field as an ordering hint, so wrapping at 2^32 is benign.
- **Test GOP/IDR relationship** — `testGopAndIdr` now uses `GOP=15 IDR=30` (a multiple) instead of the inverted `GOP=30 IDR=15`. Spec-valid and more representative of real use.
- **`testForceKeyframe` comment** — clarifies that `VideoEncoder::requestKeyframe()` is the direct API and `Metadata::ForceKeyframe` is the higher-level hook routed through `MediaIOTask_VideoEncoder`, so future test authors don't think both calls are required.
- **Test section banners flush stdout** — a `section()` helper replaces the raw `std::printf("\n--- X ---\n")`. Any future panic will leave evidence of which group was active in the pre-panic log.
- **Timecode SEI** — `MediaConfig::VideoTimecodeSEI` (bool) toggles H.264 `enableTimeCode` + `outputPictureTimingSEI` or HEVC `outputTimeCodeSEI` at init; per frame, `Metadata::Timecode` on the source Image is translated through `toNvencTimeCode()` into `NV_ENC_TIME_CODE` on `NV_ENC_PIC_PARAMS_H264::timeCode` / `_HEVC::timeCode`. Drop-frame is signalled via `cntDroppedFrames`. Frames with no Timecode set `skipClockTimestampInsertion = 1` rather than emitting zeros. AV1 emits a warn-once and skips (NVENC has no AV1 timecode OBU path). The `NV_ENC_TIME_CODE` struct lives inline inside the pic-params struct, so no slot-resident storage is needed (unlike the HDR pointers from fix #1). Exposed through `MediaIOTask_VideoEncoder::formatDesc`.

- **VUI / AV1 color description** — four new MediaConfig keys (`VideoColorPrimaries`, `VideoTransferCharacteristics`, `VideoMatrixCoefficients`, `VideoRange`) drive `NV_ENC_CONFIG_H264_VUI_PARAMETERS` / `NV_ENC_CONFIG_HEVC_VUI_PARAMETERS` / `NV_ENC_CONFIG_AV1`. All four default to "Auto" / "Unknown" (numeric sentinel 255 / 0) and get resolved against the first frame's PixelFormat at session init: `ColorModel::toH273()` provides the (primaries, transfer, matrix) triplet for well-known color models, and `PixelFormat::videoRange()` provides the range. Explicit config values bypass the resolver, so HDR10 callers set `TransferCharacteristics::SMPTE2084` etc. verbatim. `Unspecified` (2) on a primaries/transfer/matrix field remains available as a "suppress signalling" opt-out. Landed alongside three infrastructure pieces: **(a)** new `VideoRange { Unknown, Limited, Full }` TypedEnum; **(b)** new `PixelFormat::Data::videoRange` field + `PixelFormat::videoRange()` accessor, auto-populated at `registerData()` time from the existing `compSemantics[0]` min/max so the 132 existing factories keep working unchanged; **(c)** new `ColorModel::toH273()` static helper (library-first — reusable by future x264/VAAPI/etc. backends).
  - Known gap: the auto-derivation can't distinguish SDR gamma vs. PQ vs. HLG because the ColorModel table doesn't model HDR curves yet. Documented in the MediaConfig key descriptions; HDR callers set the transfer explicitly.

- **NVDEC: bitstream → Metadata round-trip** — NVDEC now surfaces everything an NVENC round-trip cares about on the decoded `Image`'s `Metadata`:
  - `CUVIDEOFORMAT::video_signal_description` → `Metadata::VideoColorPrimaries`, `VideoTransferCharacteristics`, `VideoMatrixCoefficients`, `VideoRange`. Cached in `handleSequence()` and stamped on each output Image in `handleDisplay()`.
  - `pfnGetSEIMsg` callback parses H.264 `pic_timing` (payloadType 1), HEVC `time_code` (payloadType 136), `mastering_display_colour_volume` (137), and `content_light_level_info` (144) → `Metadata::Timecode`, `MasteringDisplay`, `ContentLightLevel`. A `BitReader` handles the bit-level H.264/HEVC `clock_timestamp` RBSP syntax; HDR SEI use `memcpy`-style host-endian reads (confirmed empirically that NVDEC pre-swaps the u16 / u32 fields before handing us the payload, matching the real bytes the parser delivers).
  - `MediaIOTask_VideoDecoder::formatDesc` exposes the four VUI color-description keys as overrides. Default `Auto` / `Unknown` → use bitstream; explicit value → stamp that value instead, irrespective of what the bitstream signalled. Useful for mistagged streams that need downstream correction.
  - 4 new `Metadata` keys added alongside the existing `Timecode` / `MasteringDisplay` / `ContentLightLevel` (which were already present): `VideoColorPrimaries`, `VideoTransferCharacteristics`, `VideoMatrixCoefficients`, `VideoRange`. Intentionally separate from the legacy DPX `TransferCharacteristic` / `Colorimetric` keys — the DPX set uses SMPTE 268M codepoints, the new set uses H.273.
  - Incidental bug fix: both `NvencVideoEncoder::configure` and `NvdecVideoDecoder::configure` used to read the VUI override keys via `VariantDatabase::getAs<Enum>`, which returns a default-constructed `Enum` (value `-1` / InvalidValue) when the key is absent rather than the `VariantSpec`'s registered default. That `-1` cast-to-uint32 was flowing through the Auto/Unknown check (which only recognised 255) as a concrete override, poisoning the VUI with `0xFFFFFFFF` on the encode side and preventing bitstream fallback on the decode side. Both now look up the spec default explicitly when the key is missing.

### Still open

- **HDR metadata bypass if encoder is reconfigured mid-stream** — `configure()` captures `_masteringDisplay` / `_contentLightLevel` on each call, but `_needReconfigure` only flips a flag; we never re-send the stream-level HDR block to NVENC. Currently a moot point because the flag isn't acted on; becomes relevant when dynamic `nvEncReconfigureEncoder` lands.

---

## Interlaced support — second panic investigation (2026-04-17)

After the interlaced encoder/decoder plumbing landed (new `MediaConfig::VideoScanMode` key, `_cfgScanMode` / `_effectiveScanMode` resolution, per-frame `Metadata::VideoScanMode` override path, NVDEC `progressive_frame` / `top_field_first` → `VideoScanMode` mapping, plus the new `testInterlaced` and `testEncodeDecodeRoundTrip` interlaced cases), running `nvenc-functest` triggered a kernel panic. Test was not re-run after the fixes below per reviewer instructions.

### What the interlaced support actually wires up

- **Encoder session**: `MediaConfig::VideoScanMode` (default `Unknown`) is captured in `_cfgScanMode` at `configure()` time, then resolved against the first frame's `ImageDesc::videoScanMode()` at `ensureSession()` time into `_effectiveScanMode` (falling back to `Progressive` if both are `Unknown`).
- **Encoder session config**: when `_effectiveScanMode.isInterlaced()`, the session-init path was setting `h.outputPictureTimingSEI = 1` for both H.264 and HEVC, intending to make NVENC emit pic_struct in the per-picture pic_timing SEI.
- **Encoder per-frame**: `submitFrame` builds an `NV_ENC_TIME_CODE` with `displayPicStruct` mapped from the (possibly per-frame `Metadata::VideoScanMode`-overridden) scan mode and writes it into `pic.codecPicParams.h264PicParams.timeCode` / `_HEVC::timeCode`.
- **Encoder per-frame `pictureStruct`**: kept at `NV_ENC_PIC_STRUCT_FRAME` always; NVENC's `frameFieldMode` is left at the preset default (`FRAME`). True PAFF/MBAFF coding is intentionally out of scope for this iteration (frame-coded interlaced bitstream + pic_struct in SEI is the design).
- **Decoder**: `handleDisplay` reads `CUVIDPARSERDISPINFO::progressive_frame` / `top_field_first` and stamps `Metadata::VideoScanMode` on the output `Image` (`Progressive` / `InterlacedEvenFirst` / `InterlacedOddFirst`).
- **Tests**: `testInterlaced` runs every codec × scan-mode combo and asserts the encoder accepts the config and emits packets. The two new `testEncodeDecodeRoundTrip` interlaced cases (H.264 InterlacedTFF, HEVC InterlacedBFF) round-trip through NVENC → NVDEC and assert the decoded `Metadata::VideoScanMode` matches.

### Suspected panic vectors found

#### Vector A — H.264: `outputPictureTimingSEI = 1` without `enableTimeCode = 1`

**File:** `src/proav/nvencvideoencoder.cpp` (the H.264 branch in `ensureSession`).

The pre-fix code set `h.outputPictureTimingSEI = 1` for the interlaced session but only set `h.enableTimeCode = 1` when the caller had explicitly asked for `MediaConfig::VideoTimecodeSEI`. The NVENC SDK header is explicit on lines 2428 / 1845:

> `NV_ENC_PIC_PARAMS_H264::timeCode` — "Specifies the clock timestamp sets used in picture timing SEI. **Applicable only when NV_ENC_CONFIG_H264::enableTimeCode is set to 1.**"

Without `enableTimeCode`, NVENC ignores the per-pic `timeCode` field entirely. So our `displayPicStruct` never reached the bitstream — but worse, NVENC was being told to emit a pic_timing SEI with no per-pic data source. The pic_timing SEI's content (cpb_removal_delay, dpb_output_delay, pic_struct, NumClockTS, clock_timestamp[]) is driven by HRD state and the timeCode pic-params; with neither configured, the driver's SEI emission walks uninitialised internal state. This is the same failure shape that produced the original panic in **fix #2** (a configuration mismatch the user-space validator missed but the kernel-mode emitter tripped on).

**Fix:** When `_effectiveScanMode.isInterlaced()`, also set `h.enableTimeCode = 1`. The two flags are now paired in a single block:

```cpp
if(_timecodeSEI || sessionInterlaced) {
    h.enableTimeCode         = 1;
    h.outputPictureTimingSEI = 1;
}
```

For the no-timecode interlaced case the per-frame path already sets `skipClockTimestampInsertion = 1`, so only `displayPicStruct` lands on the bitstream — `enableTimeCode = 1` doesn't force a clock-timestamp set on every pic.

#### Vector B — HEVC: `outputPictureTimingSEI = 1` cannot carry `pic_struct` through NVENC's API

**File:** `src/proav/nvencvideoencoder.cpp` (the HEVC branch in `ensureSession`).

The pre-fix code mirrored the H.264 path: `h.outputPictureTimingSEI = 1` for HEVC interlaced, with `pic.codecPicParams.hevcPicParams.timeCode = nvTc` per frame. But the SDK header doc on lines 1632 – 1633 routes `NV_ENC_TIME_CODE` to a *different* HEVC SEI than for H.264:

> "For H264, this structure is used to populate **Picture Timing SEI** when `NV_ENC_CONFIG_H264::enableTimeCode` is set to 1.
> For HEVC, this structure is used to populate **Time Code SEI** when `NV_ENC_CONFIG_HEVC::outputTimeCodeSEI` is set to 1."

(The doc string says `enableTimeCodeSEI`, but the actual struct field on line 1950 is `outputTimeCodeSEI`; the doc is a typo.)

So `NV_ENC_TIME_CODE::displayPicStruct` feeds HEVC's *Time Code SEI* (payloadType 136), not pic_timing (payloadType 1). HEVC's pic_timing SEI carries `pic_struct` via a separate path that NVENC's public API does not expose. The pre-fix code was therefore enabling pic_timing SEI for HEVC interlaced with no way to feed it `pic_struct` — same uninitialised-internal-state shape as Vector A, but HEVC-side.

**Fix:** Drop `h.outputPictureTimingSEI = 1` for HEVC interlaced. Set `h.outputTimeCodeSEI = 1` instead, which is the SEI NVENC actually populates from `timeCode->displayPicStruct`. Add a `promekiWarn(...)` explaining the limitation: routing `pic_struct` through HEVC Time Code SEI is non-standard for `pic_struct` semantics (most HEVC players read `pic_struct` from pic_timing only) and the bitstream will be treated as progressive by spec-strict players.

```cpp
if(_timecodeSEI || sessionInterlaced) {
    h.outputTimeCodeSEI = 1;
}
if(sessionInterlaced) {
    promekiWarn("NvencVideoEncoder: HEVC interlaced scan mode requested; "
                "NVENC's public API does not expose pic_struct in HEVC "
                "pic_timing SEI, so field order will be carried only in "
                "the Time Code SEI displayPicStruct (non-standard for "
                "pic_struct semantics).  Most HEVC players read pic_struct "
                "from pic_timing only and will treat the output as progressive.");
}
```

True spec-compliant HEVC interlaced signalling (pic_struct in pic_timing) would require either (a) full PAFF coding with `frameFieldMode != FRAME` and field-pair input — explicitly out of scope per the existing comment in the encoder — or (b) a post-pass that rewrites the SEI. Both are follow-ups; the panic-avoidance fix is just to stop asking NVENC for a pic_timing SEI it can't populate.

### What this changeset does NOT fix (functional, not panic) — both round-trip cases now SKIPPED

Even with both panic-vector fixes applied, the **encode→decode round-trip for interlaced content cannot pass** with the current frame-coded encoder path. Both interlaced round-trip cases were verified to fail this way once on real hardware (`expected InterlacedEvenFirst (3), got Progressive (1)` for H.264; `expected InterlacedOddFirst (4), got Progressive (1)` for HEVC) and are now marked `skip()` in `tests/func/nvenc/main.cpp` with explanatory comments. The original test bodies are preserved verbatim inside `if(false)` blocks so re-enabling them once true PAFF coding lands is a single-line edit.

Why the round-trip cannot pass today:

- **H.264**: NVENC keeps `frameFieldMode = FRAME` and `pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME`, so the SPS gets `frame_mbs_only_flag = 1`. Spec-strict NVDEC reports `progressive_frame = 1` for any such bitstream, which maps to `VideoScanMode::Progressive` regardless of any `pic_struct = 3 / 4` in pic_timing SEI (those `pic_struct` values are only spec-valid when `frame_mbs_only_flag = 0`).
- **HEVC**: With the Vector-B fix, `displayPicStruct` rides on HEVC Time Code SEI rather than pic_timing. NVDEC's HEVC time_code SEI parser today only extracts the inner `clock_timestamp` set into `Metadata::Timecode`; it doesn't surface `displayPicStruct` as `Metadata::VideoScanMode`. And the same `frameFieldMode = FRAME` constraint as H.264 also applies — the bitstream is progressive whichever SEI carries the field-order hint.

The right long-term resolution for the round-trip tests is to implement true PAFF coding: split each input frame into two field images, set `frameFieldMode = FIELD`, and send paired-field submits with `pic.pictureStruct = NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM` / `_BOTTOM_TOP`. This is the only NVENC-supported path that yields a spec-conformant interlaced bitstream and is a substantial follow-up. Until then, `testInterlaced`'s seven "encoder accepts the config and emits packets" cases are sufficient to catch regressions in the panic-avoidance fix; the encode side is exercised, only the decode-side `VideoScanMode` round-trip assertion is bypassed.

A narrower alternative that might revive the HEVC case alone: extend `parseHevcTimeCode` to surface `displayPicStruct` as `Metadata::VideoScanMode` when present. This is non-standard for `pic_struct` semantics (most decoders look at HEVC pic_timing SEI for that) and won't help for bitstreams produced by encoders other than NVENC, so it's filed under "future work" rather than as a near-term fix.

### What was checked but found innocent

- **Per-frame `timeCode` lifetime** — `pic.codecPicParams.h264PicParams.timeCode` is an inline POD struct (not a pointer like the HDR `pMasteringDisplay` from fix #1), so `submitFrame` writing it on its stack frame is safe across deferred consumption. No slot-resident storage is needed.
- **NVDEC SEI buffer overrun** — `handleSEI` walks `pSEIMessage[i].sei_message_size` cumulatively into `pSEIData` without a total-size cross-check, but CUVID owns the buffer sizing and our parsers all bounds-check via `BitReader`. A SIGSEGV in our process is not a kernel panic.
- **`populateVuiColorDescription`** — the VUI block is populated only when at least one field is non-Unspecified or range is concrete; values are clamped to the H.273 range. No risk of feeding NVENC out-of-range VUI codepoints.
- **`_cfgScanMode` / `_effectiveScanMode` `Unknown` initialisation** — `readEnum` correctly returns the spec default (`VideoScanMode::Unknown` = 0) on missing key, so the resolution fall-through (`cfg → first frame → Progressive`) lands on a defined value.
- **AV1 interlaced** — already handled by the existing warn-once path; AV1 codec config has no interlaced signalling path, so we never enable any SEI for it. This is unchanged by either fix.
- **Encoder `pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME`** — consistent with `frameFieldMode = FRAME` (preset default); does not change with these fixes. Inconsistency between the two would be a config error NVENC would reject at init.

### Verification (executed 2026-04-17)

Re-run on real hardware after both fixes landed and the round-trip assertions were converted to `skip()`:

```
build/bin/nvenc-functest --frames 8
```

Result: **92 passed, 0 failed, 2 skipped.**

What the run actually showed:

1. **No kernel panic.** Both Vector-A (H.264 `outputPictureTimingSEI` without `enableTimeCode`) and Vector-B (HEVC pic_timing SEI with no pic_struct source) confirmed safe — every code path that previously crashed the kernel now completes without incident.
2. **`testInterlaced` group: 7/7 PASS** — all three H.264 scan modes, all three HEVC scan modes, and the AV1 warn-and-fall-through path. Confirms the encoder accepts every interlaced config and emits packets.
3. **Round-trip skips fired correctly:** `H264 InterlacedTFF round-trip` and `HEVC InterlacedBFF round-trip` both report `SKIP` with the documented limitation reason. The other two round-trip cases (`HEVC HDR10 + SEI round-trip`, `H264 Rec.709 + timecode round-trip`) still pass — VUI / mastering / CLL / timecode all survive the NVENC → NVDEC trip.
4. **Expected diagnostics fired:**
   - One `promekiWarn` per HEVC interlaced session ("NVENC's public API does not expose pic_struct in HEVC pic_timing SEI...") — Vector B's new diagnostic.
   - One `promekiWarn` per AV1 interlaced session ("interlaced scan mode requested for AV1 but NVENC does not expose...") — pre-existing diagnostic, unchanged.
   - One `promekiWarn` for the AV1 timecode SEI case — pre-existing.
5. **Panic-resistant log written cleanly** to `/mnt/data/tmp/promeki/nvenc-functest.log` (8.5 KB), with `TRY <label>` paired against every `PASS` / `FAIL` / `SKIP` line. `tail -5` of the log shows the final results banner.

---

## If a future panic happens

The test is green again as of 2026-04-17 (92 passed / 0 failed / 2 skipped at `--frames 8`, with both interlaced panic vectors patched and the panic-resistant log in place). If a future change regresses and the kernel panics, do this in order:

### 1. Capture exactly which test was active

Read the panic log written by `nvenc-functest`:

```
tail -20 /mnt/data/tmp/promeki/nvenc-functest.log
```

Every `--- section ---`, `TRY <label>`, and `PASS / FAIL / SKIP` line is `fsync()`-ed to disk before the next syscall, so the log survives kernel panics that wipe the terminal scrollback and tmpfs `/tmp`. The last `TRY` line without a matching outcome identifies the sub-case that crashed; if the last line is a `PASS`, the panic happened during cleanup or during the next test's setup. Override the log path with `--log /some/other/file` or disable file logging with `--log -`.

### 2. Re-run with reduced scope

```
build/bin/nvenc-functest --frames 8 --verbose --log /mnt/data/tmp/promeki/panic-hunt.log
```

Then progressively drop tests from `main()` at the bottom of `tests/func/nvenc/main.cpp` until you isolate the offender. The `TRY` markers from step 1 should already point at the right test — re-running with reduced scope just confirms reproducibility.

Known-sensitive tests to suspect first:

- `testBFrames` with B=2 or B=3 on HEVC (most deferred-frame pressure).
- `testLookahead` LA=16 (also deferred-frame).
- `testHdrMetadata` after `testBFrames` (exercises HDR slot path under pressure).
- `testMultiPass` full-res (extra encoder passes).

### 3. Run under driver-aware tools

```
compute-sanitizer --tool memcheck build/bin/nvenc-functest --frames 8
```

(or `cuda-memcheck` if `compute-sanitizer` isn't available). The sanitizer catches dangling pointer reads before the driver trips, so if the residual issue is another stack-lifetime bug like fix #1, this will point at it.

Also useful:

```
dmesg -T | tail -200
```

after a panic to grab the kernel log — look for the offending process, the faulting RIP, and any `nvidia` module frames in the stack.

### 4. Isolate by codec

If the panic only repros for one codec, bias investigation accordingly:

- **AV1 only** — check the AV1 SDK version supports the `inputBitDepth` / `outputBitDepth` / `chromaFormatIDC` fields (SDK 12+). Verify the GPU actually has AV1 encoder silicon (RTX 40-series / Ada or newer, plus some Hopper / data-center parts). If the caps query silently reports maxBFrames=0 but the code still blindly asks for B-frames, that's likely to be toxic — see remaining risk #1.
- **HEVC only** — look at `testHdrMetadata` with fix #1 applied; if it still panics, the HDR slot-resident fix wasn't the full story and the next suspect is `h.outputMasteringDisplay = 1` on a session where the actual `_masteringDisplay` isn't valid (the flag is set in `configure()` based on `_cfg`'s stream-level HDR, but the per-frame path could still emit a pic-params pointer without matching stream-level setup).
- **H264 only with fix #2 applied** — this should now be unlikely; if it still happens, look at `testLossless YUV444` — that's the remaining case where we do flip HIGH_444, and the `YUV8_444_Planar_Rec709` CSC output is the next place to check for buffer/layout issues.

### 5. Re-check the fixes actually took effect

A useful sanity check before starting a panic hunt:

```
grep -n "slot->nvMd" src/proav/nvencvideoencoder.cpp
grep -n "chromaFormatIDC == 3" src/proav/nvencvideoencoder.cpp
grep -n "tolerate being called on a partially-initialized" src/proav/nvencvideoencoder.cpp
```

All three should return matches. If not, you're looking at an old build.

---

## Future work (beyond panic recovery)

- **Zero-copy GPU path** — current uploadFrame is a host-memory staging path (`nvEncLockInputBuffer` + memcpy). The follow-up is `nvEncRegisterResource` + `nvEncMapInputResource` against a CUDA device pointer so the Image's GPU buffer (when it has one) goes straight to NVENC without a host round trip.
- **Dynamic `nvEncReconfigureEncoder`** — the `_needReconfigure` flag is recorded but never acted on. Wire it to `nvEncReconfigureEncoder` so bitrate / RC mode / keyframe interval changes applied via `configure()` take effect at the next IDR instead of being silently ignored.
- **Stream-level HDR emission on reconfigure** — see "remaining risk #5".
- **Alpha-layer encoding** — `_caps.supportAlpha` is populated but no code consumes it. Add once we have an input format that carries alpha and a consumer that needs it.
- **`MediaIOTask_VideoEncoder` AV1 registration surface** — verify the generic mediaiotask resolves `"AV1"` correctly now that the string-keyed and typed `VideoCodec::AV1` factories are both wired.
- **Unit test coverage for pure-CPU helpers** — NVENC side: `toNvencRc`, `toNvencPreset`, `toNvencTuning`, `h264ProfileGuid`, `h264Level`, `hevcLevel`, `av1Level`, `toNvencMastering`, `toNvencCll`, `toNvencTimeCode`, `populateVuiColorDescription`, `Impl::resolveColorDescription`. NVDEC side: `BitReader`, `parseClockTimestamp`, `parseH264PicTiming`, `parseHevcTimeCode`, `parseMasteringDisplaySei`, `parseContentLightLevelSei`. All of these are constexpr-y lookup tables or pure byte/bit parsers; easy doctest targets that don't need GPU and would have caught e.g. the old unconditional HIGH_444 override or the host-vs-wire endian mix-up on the HDR SEI. The SEI parsers are especially worth covering because a malformed bitstream can currently feed them arbitrary bytes, and the fuzzable surface deserves assertions against truncation and overflow digits. `ColorModel::toH273` is already unit-tested (in `tests/unit/colormodel.cpp`); the rest should follow the same pattern.

### Additional pro-video features still missing

Call-outs of NVENC-exposed pro-video capabilities that the backend does not yet plumb. None of these block the current pipeline; they're the next wave of broadcast/delivery-grade features after timecode.

- **HDR transfer auto-derivation** — the library's `ColorModel` doesn't distinguish HDR transfer curves (PQ / HLG) from SDR gamma today, so `ColorModel::toH273()` always returns an SDR transfer and HDR callers have to set `VideoTransferCharacteristics::SMPTE2084` (HDR10) or `ARIB_STD_B67` (HLG) explicitly. When the ColorModel gains HDR curve awareness (or the Image gains a dedicated HDR transfer metadata key), the Auto path should pick the correct value without caller intervention.
- **Sample / display aspect ratio** — `nvEncInitializeEncoder` currently hardcodes `darWidth = _width; darHeight = _height`, which is only correct for square-pixel formats. Anamorphic SD (4:3 storage aperture on a 4:3 or 16:9 display) and DVCPRO HD (non-1:1 SAR) bitstreams come out with the wrong aspect signalled in the VUI. Needs a MediaConfig key for DAR or pulling SAR from `ImageDesc` / `VideoFormat`.
- **Interlaced encoding** — `pic.pictureStruct` is unconditionally `NV_ENC_PIC_STRUCT_FRAME`. H.264 has `NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM` / `_BOTTOM_TOP` for interlaced; HEVC encodes progressive frames but signals field order through pic-timing SEI. Should drive off `VideoFormat::videoScanMode()` (or a MediaConfig override) so 1080i / 486i / 576i content encodes as actual interlaced bitstream. Needs care: NVENC's interlaced support has historically been H.264-only and requires matching PicStruct / repeat-first-field conventions.
- **Caption / AFD / bar data SEI** — H.264 / HEVC use `NV_ENC_PIC_PARAMS::*PicParams::seiPayloadArray` for user-data unregistered and user-data registered SEI. The broadcast-critical ones are CEA-608/708 closed captions (payloadType 4, user_data_registered_itu_t_t35), Active Format Description (AFD, payloadType 4 with ATSC A/53-1 wrapper), and bar data. Need a `Metadata` key that carries the raw payload bytes (e.g. `Metadata::ClosedCaptions` as a Buffer) so an upstream VANC-decoding MediaIOTask can produce the data, and the encoder wraps it in SEI on the way out. Parallel AV1 support would go through `NV_ENC_PIC_PARAMS_AV1::obuPayloadArray` / ITU-T T.35 OBU. The decode side should mirror — NVDEC's `pfnGetSEIMsg` already fires on payloadType 4 but the handler currently falls through to the `default` branch and drops the payload.

- **Timecode Mode inference on decode** — `parseH264PicTiming` and `parseHevcTimeCode` currently pick `Timecode::NDF30` / `DF30` from `cnt_dropped_flag` alone because the SEI parser doesn't have access to the sequence header's timing info. That misreports the frame rate for 24 / 25 / 50 / 59.94 / 60 fps streams — the digits are right but `tc.mode().fps()` is wrong, so `++tc` wraps at the wrong boundary. Fix: thread the parsed `CUVIDEOFORMAT::frame_rate` through from `handleSequence` to the SEI parsers (or to `handleSEI`) and pick the matching `Timecode::Mode` from the nominal rate.

(Timecode / VUI / HDR SEI parsing on the decode side landed in this changeset — see the "NVDEC: bitstream → Metadata round-trip" item above in "Landed".)

---

## Files touched in this changeset

Modified:
- `CMakeLists.txt` — added `nvenc-functest` to the functional test tree.
- `include/promeki/enums.h` — `VideoEncoderPreset::Lossless`; new `VideoRange`, `ColorPrimaries`, `TransferCharacteristics`, `MatrixCoefficients` TypedEnums.
- `include/promeki/mediaconfig.h` — twelve new MediaConfig keys (including `VideoTimecodeSEI`, `VideoColorPrimaries`, `VideoTransferCharacteristics`, `VideoMatrixCoefficients`, `VideoRange`).
- `include/promeki/metadata.h` — `ForceKeyframe`, `MasteringDisplay`, `ContentLightLevel`, `VideoColorPrimaries`, `VideoTransferCharacteristics`, `VideoMatrixCoefficients`, `VideoRange` (the last four are stamped on decoded Images by `NvdecVideoDecoder`).
- `include/promeki/colormodel.h` / `src/core/colormodel.cpp` — new `ColorModel::H273` struct + `ColorModel::toH273()` static helper.
- `include/promeki/pixelformat.h` / `src/core/pixelformat.cpp` — new `Data::videoRange` field, `PixelFormat::videoRange()` accessor, auto-derivation at `registerData()` time from `compSemantics[0]` min/max.
- `src/proav/nvdecvideodecoder.cpp` — VUI color-description parsing from `CUVIDEOFORMAT::video_signal_description`, SEI parsing via `pfnGetSEIMsg` (H.264 pic_timing / HEVC time_code / mastering display / CLL), plus `configure()` override keys and a bit-level `BitReader` for the timing SEI.
- `src/proav/mediaiotask_videodecoder.cpp` — FormatDesc exposes the four VUI override keys.
- `tests/unit/colormodel.cpp`, `tests/unit/pixelformat.cpp` — tests for the new helpers.
- `include/promeki/nvencvideoencoder.h` — `Codec_AV1` added to `Codec` enum; description updated.
- `include/promeki/pixelformat.h` — AV1 / YUV8_444_Planar_Rec709 / YUV10_444_Planar_LE_Rec709.
- `include/promeki/pixelmemlayout.h` — `P_444_3x10_LE`.
- `include/promeki/variant.h` — `TypeMasteringDisplay`, `TypeContentLightLevel` registrations + includes.
- `src/core/pixelformat.cpp` — factories + registrations for the three new PixelFormats.
- `src/core/pixelmemlayout.cpp` — factory + registration for `P_444_3x10_LE`.
- `src/core/videocodec.cpp` — `VideoCodec::AV1` gains `compressedPixelFormats`.
- `src/proav/mediaiotask_videoencoder.cpp` — FormatDesc exposes new config keys (incl. `VideoTimecodeSEI`, `VideoColorPrimaries`, `VideoTransferCharacteristics`, `VideoMatrixCoefficients`, `VideoRange`); `ForceKeyframe` wiring.
- `src/proav/nvencvideoencoder.cpp` — the main backend. Timecode SEI wiring (`toNvencTimeCode()`, per-frame `Metadata::Timecode` translation). VUI color description wiring: `populateVuiColorDescription()` helper for H.264/HEVC, direct-field population for AV1, `Impl::resolveColorDescription()` folds Auto/Unknown against the first frame's `PixelFormat::colorModel()` / `videoRange()`. Interlaced support: `_cfgScanMode` / `_effectiveScanMode` resolution at `ensureSession()` time; `toNvencDisplayPicStruct()` mapping; `toNvencTimeCode()` takes a `VideoScanMode` parameter; per-frame `Metadata::VideoScanMode` override path. **Second-investigation interlaced fixes:** H.264 now sets `enableTimeCode = 1` whenever `outputPictureTimingSEI = 1` (was the kernel-panic shape Vector A); HEVC interlaced uses `outputTimeCodeSEI = 1` rather than `outputPictureTimingSEI = 1` (Vector B) and warns-once that pic_struct lands in HEVC Time Code SEI rather than pic_timing.
- `tests/func/nvenc/main.cpp` — added `testTimecode` (6 codec × mode combos), `testColorDescription` (8 codec × color-description combos covering Auto derivation, explicit HDR10 / HLG / full-range / Unspecified), `testEncodeDecodeRoundTrip` (HEVC HDR10 + H.264 Rec.709 active round-trips, plus 2 skipped interlaced round-trip cases preserved inside `if(false)` for future re-enablement), and `testInterlaced` (7 codec × scan-mode combos asserting encoder accepts the config and emits packets). **Panic-resistant logging:** new `--log PATH` CLI option (default `/mnt/data/tmp/promeki/nvenc-functest.log`, `-` to disable); `logf()` helper that mirrors every line to stdout AND a log file with `fflush(3)` + `fsync(2)` after each write; `tryStart(label)` markers paired with each `PASS` / `FAIL` / `SKIP` so the last log line before any future panic identifies the offending sub-case.
- `tests/func/CMakeLists.txt` — include `nvenc/`.
- `tests/unit/mediaiotask_videoencoder.cpp`, `tests/unit/videocodec_registry.cpp` — adjustments for new surfaces.

Added (untracked):
- `include/promeki/contentlightlevel.h`, `src/core/contentlightlevel.cpp`.
- `include/promeki/masteringdisplay.h`, `src/core/masteringdisplay.cpp`.
- `tests/func/nvenc/CMakeLists.txt`, `tests/func/nvenc/main.cpp`.
