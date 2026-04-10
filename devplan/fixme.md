# Existing FIXMEs

**Standards:** All fixes must follow `CODING_STANDARDS.md`. All changes require updated unit tests. See `README.md` for full requirements.

Tracked FIXME comments scattered across the codebase. Address these as they become relevant to ongoing phase work (e.g., fix the File Windows code when refactoring File to derive from IODevice in Phase 2).

---

## Windows File Implementation

**File:** `src/core/file.cpp:40`
**FIXME:** "The windows code here needs love."

The Windows `#ifdef` branch is a stub — `isOpen()` returns false, and the rest of the Windows-specific File methods are likely incomplete or missing.

- [ ] Implement Windows File backend using `CreateFile`/`ReadFile`/`WriteFile` HANDLE API
- [ ] Test on Windows (or at minimum ensure it compiles with correct stubs)
- [ ] Natural time to fix: Phase 2 File -> IODevice refactor

---

## AudioGen Planar Format Support

**File:** `src/proav/audiogen.cpp:66`
**FIXME:** "Need to set to new plane for planar."

Currently increments `data++` per channel, which only works for interleaved formats. Planar formats store each channel in a separate memory plane.

- [ ] Detect planar vs interleaved from `AudioDesc`
- [ ] For planar: advance to next plane's base pointer per channel
- [ ] For interleaved: keep current `data++` behavior
- [ ] Test with both planar and interleaved audio generation

---

## DateTime Number Word Parsing

**File:** `src/core/datetime.cpp:112`
**FIXME:** "Need to use the String::parseNumberWords()"

The `std::istringstream` was replaced with `strtoll` as part of the stream migration, but the FIXME still stands: the code should use `String::parseNumberWords()` for natural language number parsing (e.g., "three days ago") instead of bare `strtoll`.

- [ ] Implement or verify `String::parseNumberWords()` exists
- [ ] Replace `strtoll` token parsing with `String::parseNumberWords()`
- [ ] Update tests

---

## Replace Direct std Library Usage with Library Wrappers

Library classes should use the library's own container/type wrappers (`List`, `Map`, `Array`, `String`) instead of raw std types. The following violations have been identified:

### std::vector → List\<T\>

- **`src/core/bufferediodevice.cpp:149,167,211,227,240`** — Multiple `std::vector<uint8_t>` used as temporary read/collect buffers.
### std::map → Map\<K,V\>

- **`src/core/string.cpp:283`** — `static const std::map<std::string, int64_t> numberWords` lookup table.
- **`src/core/datetime.cpp:78`** — `static const std::map<std::string, system_clock::duration> units` lookup table.

### std::array → Array\<T,N\>

- **`include/promeki/macaddress.h:109`** — `std::array<uint8_t, 6>` in constructor initializer.
- **`include/promeki/musicalscale.h:45`** — `using MembershipMask = std::array<int, 12>` public typedef.
- **`include/promeki/util.h:116,127,141-144,154`** — `std::array<T, 4>` in public template function signatures (`promekiCatmullRom`, `promekiBezier`, `promekiBicubic`, `promekiCubic`).
- **`src/core/system.cpp:27`** — `std::array<char, HOST_NAME_MAX>` local variable.

### Tasks

- [ ] Replace `std::vector` with `List<T>` in `src/core/bufferediodevice.cpp`
- [ ] Replace `std::map` with `Map<K,V>` in `src/core/string.cpp` and `src/core/datetime.cpp`
- [ ] Replace `std::array` with `Array<T,N>` in `macaddress.h`, `musicalscale.h`
- [ ] Replace `std::array` with `Array<T,N>` in `util.h` template functions
- [ ] Replace `std::array` with `Array<T,N>` in `src/core/system.cpp`
- [ ] Verify all replacements compile and pass tests

---

## QuickTime: Little-Endian Float Audio Storage

**Files:** `src/proav/mediaiotask_quicktime.cpp` (`pickStorageFormat`), `src/proav/quicktime_writer.cpp` (`pcmFourCCForDataType`)
**FIXME:** Little-endian float (`AudioDesc::PCMI_Float32LE`) has no single-FourCC mapping in the QuickTime sample-entry format. The QuickTime `fl32` FourCC is big-endian float; for little-endian float, the spec requires the generic `lpcm` FourCC plus a `pcmC` extension atom describing endianness and the "sample is float" flag.

Current workaround in `MediaIOTask_QuickTime::pickStorageFormat()` promotes incoming `PCMI_Float32LE` sources to `PCMI_S16LE` for storage — widely compatible but lossy (32-bit float → 16-bit int).

- [ ] Either:
  - (a) Emit a proper `lpcm` sample entry with a `pcmC` extension atom carrying endianness + sample-is-float flags so little-endian float can be stored natively, **or**
  - (b) Promote to `PCMI_Float32BE` (byte-swap on write, use `fl32` FourCC) instead of dropping to s16 — preserves bit depth.
- [ ] Regardless of which path, drop the lossy promotion from `pickStorageFormat()` and update the round-trip tests to verify float precision.
- [ ] Audit the writer's `pcmFourCCForDataType` fallthrough — the `lpcm` default is currently a trap (silent data-format mismatch) unless `pcmC` is also emitted.

---

## QuickTime: `raw ` Codec Byte Order — BGR vs RGB Disagreement

**Files:** `src/core/pixeldesc.cpp` (`makeRGB8`), `src/proav/quicktime_writer.cpp` (visual sample entry writer)
**FIXME:** Players disagree on the byte order of the QuickTime `raw ` codec tag with depth 24.

The QuickTime File Format Specification historically defines `raw ` as **B, G, R** byte order per pixel. Modern ffmpeg, VLC, and our own reader treat `raw ` as **R, G, B** byte order (the order ffmpeg's `rawvideo` encoder emits). mplayer follows the historical QT spec and reports `VIDEO: [BGR] 320x180 24bpp` for our files — which means it will swap red and blue channels on display.

Reproduction: ffmpeg decodes our `RGB8_sRGB` → `raw ` output and produces correct SMPTE color bars. mplayer opens the same file and reads the byte layout as BGR.

Options for a proper fix:
- **(a)** Switch our `RGB8_sRGB` plane layout to BGR and swap on encode. Matches the official QT spec but requires byte-swapping on every frame write (and a full code-path audit for anything that touches RGB8_sRGB bytes directly).
- **(b)** Use a different PixelDesc whose QuickTime FourCC unambiguously encodes the byte order (e.g. a dedicated `BGR8_sRGB` entry with `raw ` as its QT FourCC, and keep `RGB8_sRGB` for paths that write to containers that use RGB order natively).
- **(c)** Route 24-bit RGB through a different QuickTime codec tag that modern players all agree is RGB (e.g. emit the rarer `BGR ` four-letter code or use a proprietary FourCC and hope for the best — not recommended).

Short-term mitigation: test playback in VLC / ffplay / QuickTime Player (the widely-used players all agree on RGB for `raw ` 24-bit). mplayer is the outlier. Document the disagreement rather than chase the long tail.

- [ ] Pick option (a) or (b) — lean toward (b) for cleanliness, since it keeps `RGB8_sRGB` meaning what it says and isolates the container-specific byte order to a separate PixelDesc.
- [ ] Add a mplayer playback test once one of the above fixes is in place.

---

## CMake: Incremental builds miss header layout changes across shared libraries

**File:** `CMakeLists.txt` (SDL library target)
**FIXME:** When a header in the core `promeki` library (e.g. `include/promeki/audiodesc.h`) changes its struct layout, CMake's incremental dependency tracking rebuilds `libpromeki.so` but does not always rebuild `libpromeki-sdl.so`. The result is an ABI mismatch at runtime — code in one library holds the new struct layout, code in the other still has the old layout — and move/copy operations corrupt memory (observed as a segfault in `std::_Rb_tree::_M_move_data` when a `Metadata` move-assigned an `AudioDesc` across the library boundary).

Workaround: run `make clean && build` after any header-level ABI change.

- [ ] Tighten the SDL target's header dependency tracking so layout changes in `include/promeki/*.h` trigger a rebuild of `libpromeki-sdl.so`.
- [ ] Consider adding an ABI-check step to CI that links a known-clean test binary against the freshly-built libraries and runs a basic smoke test.

---

## BufferPool is available but not wired into any hot path

**File:** `include/promeki/bufferpool.h`, `src/core/bufferpool.cpp`
**FIXME:** The `BufferPool` class was added during the Phase 6 efficiency batch as a reusable primitive for realtime reader/writer hot paths, but it is intentionally not wired into `QuickTimeReader::readSample()` (or anywhere else). For the current workload (~30 allocations/sec for a single video reader) the per-call allocation cost is well under 0.1% overhead, so wiring it in blindly is premature optimization.

- [ ] Profile a realistic multi-stream / high-frame-rate workload and identify whether buffer allocation shows up in the hot path.
- [ ] If yes, wire `BufferPool` into `QuickTimeReader` as an optional opt-in.
- [ ] If no after multiple workloads, consider whether the class should be removed or kept as an available utility for other backends.

---

## Fragmented MP4 reader: trex defaults are emitted by writer but ignored by reader

**Files:** `src/proav/quicktime_reader.cpp` (parseTraf / parseTrun), `src/proav/quicktime_writer.cpp` (appendMvex)
**FIXME:** The fragmented writer emits an `mvex`/`trex` box with per-track default sample values, and our fragmented reader correctly parses `tfhd` default overrides — but does not fall back to `trex` defaults when `tfhd` does not supply them. Because our own writer always writes `tfhd` overrides (via the Phase 6 audio trun compression pass), our reader round-trips our own output correctly. External fragmented MP4 files that rely purely on `trex` defaults with a bare `tfhd` will read as zero-duration / zero-size samples.

- [ ] Parse `mvex`/`trex` during `parseMoov` and stash per-track defaults.
- [ ] In `parseTraf`, use the stashed `trex` defaults when the corresponding `tfhd` override flag is not set.
- [ ] Add a test with a synthesized fragmented MP4 that uses `trex` defaults without `tfhd` overrides, to verify the fallback path.

---

## MediaIOTask_QuickTime: compressed audio pull rate drifts

**File:** `src/proav/mediaiotask_quicktime.cpp` (`executeCmd(MediaIOCommandRead)` audio branch)
**FIXME:** When reading a file with compressed audio (AAC-in-MP4, Opus, etc.), MediaIOTask_QuickTime currently pulls exactly one audio packet per video frame read. This is correct for the PCM path (where `FrameRate::samplesPerFrame()` gives the exact PCM frame count for the current video frame), but for variable-duration compressed audio packets the strategy drifts. Example: AAC at 48 kHz has 1024-sample packets (~21.3 ms), video at 30 fps has 33.3 ms per frame → we fall behind by about half a packet per video frame. Over a 1-minute file the audio would end ~15 seconds short of the video.

- [ ] Replace the "one packet per video frame" heuristic with a dts-walking strategy: compute the target end-of-frame time in the audio track's timescale, then read audio samples while their cumulative dts is below that target. Requires per-sample dts/duration access on the sample index — already available for non-compact paths.
- [ ] Update the AAC-in-MP4 round-trip test to verify A/V sync over a longer duration (e.g. 2+ seconds).

---

## QuickTime XMP parser only matches the `bext:` prefix

**Files:** `src/proav/quicktime_reader.cpp` (`extractBextElement`, `parseUdta`), `src/proav/quicktime_writer.cpp` (`buildBextXmpPacket`, `appendUdta`)
**FIXME:** The XMP reader in `QuickTimeReader::parseUdta` is a minimal substring-based extractor. It locates `<bext:localName>` and its matching closing tag via `String::find` — no namespace-aware XML parsing, no prefix resolution. If a third-party tool emits the same bext namespace URI (`http://ns.adobe.com/bwf/bext/1.0/`) under a different prefix, the reader will miss the fields even though the XMP is spec-compliant. Example that would slip through:

```xml
<rdf:Description xmlns:ns1="http://ns.adobe.com/bwf/bext/1.0/">
  <ns1:umid>0123456789abcdef...</ns1:umid>
</rdf:Description>
```

The writer side (`buildBextXmpPacket`) also hand-rolls XML by string concatenation with manual escaping of `&`, `<`, `>`. Round-tripping our own output is unaffected because we always emit the `bext:` prefix, and most media tools that embed BWF XMP use the same convention by default. The gap matters only when ingesting packets from tools that bind the namespace URI to a non-standard prefix.

- [ ] Blocked on adding a real XML parser to the core library (tracked separately as a library-level TODO).
- [ ] Once proper XML support lands, replace `extractBextElement()` with namespace-aware lookup against `http://ns.adobe.com/bwf/bext/1.0/` so any prefix bound to that URI is accepted.
- [ ] Replace the string-concatenation emitter in `buildBextXmpPacket()` with structured XML output.
- [ ] Add a round-trip test with a hand-crafted XMP packet that uses a non-`bext:` prefix, to verify the new parser handles it.

---

## JPEG XS: packed RGB encode path is not implemented

**Files:** `src/proav/jpegxsimagecodec.cpp` (`classifyInput`), `include/promeki/pixeldesc.h`
**FIXME:** The initial `JpegXsImageCodec` drop only accepts planar YUV 4:2:2 / 4:2:0 at 8/10/12-bit. SVT-JPEG-XS itself supports `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` for interleaved 8-bit RGB (RGBRGB…), and we already declared the `JPEG_XS_RGB8_sRGB` compressed PixelDesc so the library can *describe* an RGB JPEG XS stream — but the codec's `classifyInput()` rejects `RGB8_sRGB` on the input side, and there is no corresponding `PACKED_YUV444_OR_RGB` branch in the encode/decode plumbing.

The SVT encoder's single-plane layout for packed RGB differs from the planar path: `components_num` is 1, `components[0].byte_size = width*height*3*pixel_size`, and stride/min-size accounting treats the whole image as one component. The decoder side also needs the tightly-packed temp-plane copy path extended to handle a single 3×-wide plane.

- [ ] Extend `classifyInput()` with an `RGB8_sRGB` case that maps to `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` and tags the output as `JPEG_XS_RGB8_sRGB`.
- [ ] In `encode()`, handle the single-plane packed layout: `stride[0]` in bytes, `data_yuv[0]` pointing at the interleaved RGB buffer. Verify against `svt_jpeg_xs_encoder_get_image_config()` output.
- [ ] In `decode()`, extend the temp-plane allocation loop to honor `components_num == 1` for packed-RGB bitstreams.
- [ ] Add a `JpegXsImageCodec_RoundTripRGB8` test mirroring the planar round-trip coverage.
- [ ] Optional: add 10/12-bit RGB (SVT supports it via `PLANAR_YUV444_OR_RGB` with `gbrp10le`-style plane ordering, which also needs a new planar RGB PixelDesc since the library's current RGB10/12 variants are interleaved `I_3x10_LE` / `I_3x12_LE`).

---

## JPEG XS: additional matrix / range / colour-space variants

**Files:** `src/core/pixeldesc.cpp`, `src/proav/jpegxsimagecodec.cpp`
**FIXME:** The first-pass codec exposes only the Rec.709 limited-range YUV family (`JPEG_XS_{YUV8,YUV10,YUV12}_{422,420}_Rec709`). JPEG XS itself is colour-space agnostic — matrix / range / primaries live out-of-band in the container (ISO/IEC 21122-3 sample entry) or the SDP (RFC 9134 `a=fmtp`), never in the bitstream — so adding more variants is purely a bookkeeping exercise once real workflows need them.

Likely additions when an upstream caller actually asks for them:
- `JPEG_XS_*_Rec601` / `JPEG_XS_*_Rec601_Full` — legacy broadcast / strict JFIF analogues.
- `JPEG_XS_*_Rec709_Full` — modern full-range YUV from cameras with an ICC profile.
- `JPEG_XS_*_Rec2020` — HDR / UHD contribution, including the 10- and 12-bit planar variants.
- `JPEG_XS_*_SemiPlanar_*` (NV12 / NV16) — if a zero-copy path from a hardware decoder wants to avoid the deinterleave cost on the input side (SVT needs planar, so the codec would have to run the NV-planar → fully-planar split like the JPEG codec already does).

- [ ] Wait for a concrete upstream need before expanding the matrix; the current Rec.709 limited-range default matches ST 2110 JPEG XS carriage.
- [ ] When adding, follow the JPEG variant pattern in `pixeldesc.cpp` (`makeJPEG_XS_YUV` helper already structured for this) and extend `classifyInput()` / `defaultDecodeTarget()` accordingly.
- [ ] Extend the codec's validation in `decode()` to match on the new uncompressed targets.

---

## JPEG XS: QuickTime / ISO-BMFF container support (jxsm sample entry)

**Files:** `src/proav/quicktime_writer.cpp` (visual sample entry writer, `fourCCForPixelDesc`), `src/proav/quicktime_reader.cpp` (video sample-entry parse path), `src/proav/mediaiotask_quicktime.cpp` (`pickStorageFormat`)
**FIXME:** The codec can encode and decode JPEG XS, the `PixelDesc` carries the `jxsm` FourCC in its `fourccList`, but nothing on the QuickTime writer/reader path knows how to actually emit or consume an ISO/IEC 21122-3 sample entry. Writing a JPEG XS MP4 requires:

1. A `jxsm` (JPEG XS movie) visual sample entry in `stsd` — the standard ISO visual sample entry plus an extension box that carries the JPEG XS codestream headers (`jxpl` profile/level, `colr` nclc/nclx colour info, `jpgC` codestream header sample for random access).
2. The sample entry's `depth` and `component_count` fields pulled from the JPEG XS image config (`bit_depth`, `components_num`), not the current `depth=24` default the `raw ` / `2vuy` path uses.
3. In the reader, sample-entry recognition for `jxsm` and mapping back to one of the `JPEG_XS_*_Rec709` PixelDescs (matrix / range come from the `colr` atom's nclc codes, not from inspecting the bitstream).
4. Round-trip test that writes a JPEG XS-encoded image into an MP4, reads it back, and verifies the decompressed frame matches the source within JPEG XS's quantisation tolerance.

Relevant references: ISO/IEC 21122-3 Annex A for the `jxsm` sample entry, ISO/IEC 14496-12 for the host visual sample entry layout, and the SVT-JPEG-XS ffmpeg-plugin source (`thirdparty/svt-jpeg-xs/ffmpeg-plugin/`) which implements the same bindings against FFmpeg's ISO-BMFF writer — a useful cross-check for the extension-atom byte layout.

- [ ] Add a `jxsm` sample entry emitter in `quicktime_writer.cpp` that carries the required extension atoms (`jpgC` at minimum — verify which others are mandatory per 21122-3).
- [ ] Teach `pickStorageFormat()` in `mediaiotask_quicktime.cpp` to route `JPEG_XS_*` PixelDescs through the new path (instead of the current RGB8 / 2vuy fallback).
- [ ] Add reader support: recognise `jxsm`, parse the extension atoms, and map the track to the corresponding `JPEG_XS_*_Rec709` PixelDesc using the `colr` atom's matrix/range codes.
- [ ] Round-trip test: `MediaIOTask_QuickTime` writer → reader, comparing decoded frame to source.
- [ ] External interop test: verify the written `.mov` opens in ffplay/VLC/QuickTime Player.

---

## JPEG XS: RFC 9134 slice-mode packetization

**Files:** `src/network/rtppayload.cpp` (`RtpPayloadJpegXs`), `src/proav/mediaiotask_rtp.cpp`, `include/promeki/mediaiotask_rtp.h`
**FIXME:** `RtpPayloadJpegXs` implements codestream mode (K=0): each `pack()` call fragments one complete codestream across MTU-sized packets with the 4-byte RFC 9134 header. Slice mode (K=1) — where the encoder emits one slice per packet and each slice fits in a single RTP packet — is not yet implemented. Slice mode is what ST 2110-22 mandates for constant-bitrate contribution links.

Slice mode requires:
- **Encoder integration**: SVT-JPEG-XS `slice_packetization_mode = 1` makes each `svt_jpeg_xs_encoder_get_packet()` return one slice-sized bitstream buffer. Needs a new `MediaConfig::JpegXsSlicePacketization` key plumbed into the encoder API.
- **Pack path**: instead of fragmenting a complete codestream, each encoder output buffer maps 1:1 to one RTP packet with `K=1` in the header. The `SEP` counter increments per slice; the `P` (last packet in frame) flag comes from `last_packet_in_frame` on the encoder output.
- **Unpack path**: reassembly by RTP timestamp is the same, but verification should check the per-frame packet counter for gaps / reordering.
- **SDP**: the `a=fmtp` line must carry `packetization-mode=1` so receivers know to expect slice boundaries at packet boundaries.

- [ ] Add `MediaConfig::JpegXsSlicePacketization` key; plumb into `JpegXsImageCodec` encoder init.
- [ ] Extend `RtpPayloadJpegXs::pack()` with a K=1 branch that maps encoder slices 1:1 to RTP packets.
- [ ] Update `RtpPayloadJpegXs::unpack()` to verify per-frame packet counter for gap detection.
- [ ] Update `buildJpegXsFmtp()` to emit `packetization-mode=1` when slice mode is active.
- [ ] Loopback test mirroring the existing codestream-mode coverage.

---

## QuickTimeWriter: compressed audio input path is missing

**File:** `src/proav/quicktime_writer.cpp` (`addAudioTrack`, `writeSample` audio branch)
**FIXME:** The writer side treats audio samples as raw PCM only. `addAudioTrack()` requires a valid `AudioDesc::DataType` (PCM bytes per sample); it does not accept a compressed `AudioDesc` with `codecFourCC()` set. The reader side handles compressed audio cleanly (after the Phase 6 AudioDesc/Audio extensions), so this is an asymmetric gap that blocks remux-style workflows (open a compressed source, write it to a new container without transcoding).

- [ ] Update `addAudioTrack()` to accept compressed AudioDesc (non-zero `codecFourCC()`).
- [ ] In `writeSample()` audio branch, when the track is compressed, write each sample as one variable-size entry (with its own duration / size / keyframe flags) rather than treating it as a constant-size PCM chunk.
- [ ] In `appendTrak()` stsd emission, when the track is compressed, emit the correct sample entry form (ISO-BMFF `mp4a` / `Opus` / etc.) with any required extension atoms (`esds` for AAC, `dOps` for Opus, etc.) — at minimum a stub that carries the codec-specific config bytes supplied via the track metadata.
- [ ] Extend MediaIOTask_QuickTime writer path to accept compressed audio frames (currently refuses via the non-PCM guard in `setupWriterFromFrame`).

---

## RTP JPEG reader: no in-band signal for Rec.709 or limited/full range

**Files:** `src/proav/mediaiotask_rtp.cpp` (`emitVideoFrame` deferred JPEG geometry)
**FIXME:** When the RTP JPEG reader discovers image geometry from the first reassembled frame, it correctly detects subsampling (4:2:2 vs 4:2:0 from the RFC 2435 Type field / SOF0 sampling factors) and RGB (SOF0 component structure + Type >= 2), but always defaults to Rec.601 full range per the JFIF specification. RFC 2435 carries no metadata for the color matrix (Rec.601 vs Rec.709) or quantization range (full vs limited).

JPEG itself supports optional markers that can carry color information:
- **APP2 (ICC Profile)** — the most authoritative source; a full ICC color profile can be embedded across chained APP2 segments. Definitively identifies primaries, matrix, and transfer function.
- **APP14 (Adobe)** — carries a color transform byte (0=unknown/RGB, 1=YCbCr, 2=YCCK).
- **APP1 (EXIF)** — `ColorSpace` tag (1=sRGB, 0xFFFF=uncalibrated).
- **APP0 (JFIF)** — its presence implies Rec.601 full-range YCbCr per the JFIF spec.

However, standard RFC 2435 senders strip all markers and transmit only the entropy-coded data + quantization tables. Our own `RtpPayloadJpeg::unpack()` reconstructs a bare JFIF (SOI/DQT/SOF0/DHT/SOS/ECS/EOI) with none of these APP markers. Non-standard MJPEG-over-RTP implementations that send complete JFIF frames (typically with a dynamic PT) could include these markers, but the current reader does not inspect them.

- [ ] Parse APP2 ICC profile from reassembled JFIF when present — extract primaries/matrix/TRC and map to the closest PixelDesc variant (Rec.601 vs Rec.709, full vs limited).
- [ ] Parse APP14 Adobe marker for the color transform byte as a secondary signal.
- [ ] Consider a `MediaConfig::VideoColorModel` override key so callers can force Rec.709 / limited range for broadcast sources that use JPEG transport without color metadata.

---

## CMake: libjpeg-turbo ExternalProject did not forward NASM compiler path

**File:** `CMakeLists.txt` (libjpeg-turbo ExternalProject_Add)
**FIXME (fixed):** The vendored libjpeg-turbo ExternalProject build did not forward `CMAKE_ASM_NASM_COMPILER` to the sub-build, causing `check_language(ASM_NASM)` to fail and SIMD to be silently disabled. Fixed by adding `find_program(PROMEKI_NASM_PATH NAMES nasm yasm)` and passing `-DCMAKE_ASM_NASM_COMPILER=${PROMEKI_NASM_PATH}` to the ExternalProject. Performance impact: libjpeg-turbo without SIMD is significantly slower for encode/decode.

- [x] Forward NASM path to libjpeg-turbo ExternalProject (fixed 2026-04-09)

