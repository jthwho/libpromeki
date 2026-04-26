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
**FIXME:** Little-endian float (`AudioFormat::PCMI_Float32LE`) has no single-FourCC mapping in the QuickTime sample-entry format. The QuickTime `fl32` FourCC is big-endian float; for little-endian float, the spec requires the generic `lpcm` FourCC plus a `pcmC` extension atom describing endianness and the "sample is float" flag.

Current workaround in `MediaIOTask_QuickTime::pickStorageFormat()` promotes incoming `PCMI_Float32LE` sources to `PCMI_S16LE` for storage — widely compatible but lossy (32-bit float → 16-bit int).

- [ ] Either:
  - (a) Emit a proper `lpcm` sample entry with a `pcmC` extension atom carrying endianness + sample-is-float flags so little-endian float can be stored natively, **or**
  - (b) Promote to `PCMI_Float32BE` (byte-swap on write, use `fl32` FourCC) instead of dropping to s16 — preserves bit depth.
- [ ] Regardless of which path, drop the lossy promotion from `pickStorageFormat()` and update the round-trip tests to verify float precision.
- [ ] Audit the writer's `pcmFourCCForDataType` fallthrough — the `lpcm` default is currently a trap (silent data-format mismatch) unless `pcmC` is also emitted.

---

## QuickTime: `raw ` Codec Byte Order — BGR vs RGB Disagreement

**Files:** `src/core/pixelformat.cpp` (`makeRGB8`), `src/proav/quicktime_writer.cpp` (visual sample entry writer)
**FIXME:** Players disagree on the byte order of the QuickTime `raw ` codec tag with depth 24.

The QuickTime File Format Specification historically defines `raw ` as **B, G, R** byte order per pixel. Modern ffmpeg, VLC, and our own reader treat `raw ` as **R, G, B** byte order (the order ffmpeg's `rawvideo` encoder emits). mplayer follows the historical QT spec and reports `VIDEO: [BGR] 320x180 24bpp` for our files — which means it will swap red and blue channels on display.

Reproduction: ffmpeg decodes our `RGB8_sRGB` → `raw ` output and produces correct SMPTE color bars. mplayer opens the same file and reads the byte layout as BGR.

Options for a proper fix:
- **(a)** Switch our `RGB8_sRGB` plane layout to BGR and swap on encode. Matches the official QT spec but requires byte-swapping on every frame write (and a full code-path audit for anything that touches RGB8_sRGB bytes directly).
- **(b)** Use a different PixelFormat whose QuickTime FourCC unambiguously encodes the byte order (e.g. a dedicated `BGR8_sRGB` entry with `raw ` as its QT FourCC, and keep `RGB8_sRGB` for paths that write to containers that use RGB order natively).
- **(c)** Route 24-bit RGB through a different QuickTime codec tag that modern players all agree is RGB (e.g. emit the rarer `BGR ` four-letter code or use a proprietary FourCC and hope for the best — not recommended).

Short-term mitigation: test playback in VLC / ffplay / QuickTime Player (the widely-used players all agree on RGB for `raw ` 24-bit). mplayer is the outlier. Document the disagreement rather than chase the long tail.

- [ ] Pick option (a) or (b) — lean toward (b) for cleanliness, since it keeps `RGB8_sRGB` meaning what it says and isolates the container-specific byte order to a separate PixelFormat.
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

## JPEG XS: SVT-JPEG-XS `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` validation bug

**Files:** SVT-JPEG-XS `Source/Lib/Encoder/Codec/EncHandle.c` (`svt_jpeg_xs_encoder_send_picture`, line ~958)
**FIXME:** SVT-JPEG-XS advertises `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` for interleaved 8/10-bit RGB input (the encoder deinterleaves to planar internally with AVX2/AVX512 fast paths). However, `svt_jpeg_xs_encoder_send_picture()` has a validation bug that prevents it from working: the validation loop at line ~958 iterates `pi->comps_num` (which is 3 for RGB — set by `format_get_sampling_factory` in `Pi.c:540`) and checks `stride[c]` / `alloc_size[c]` for all 3 logical components. But `svt_jpeg_xs_image_buffer_alloc()` (in `ImageBuffer.c:32`) only fills `stride[0]` / `alloc_size[0]` / `data_yuv[0]` for packed RGB, leaving components 1 and 2 at zero. The validation computes `min_size = 0 * (height-1) + width * pixel_size = width`, finds `alloc_size[1] = 0 < width`, and returns `SvtJxsErrorBadParameter`.

**Current workaround:** The codec uses `COLOUR_FORMAT_PLANAR_YUV444_OR_RGB` with `RGB8_Planar_sRGB` as the native encode/decode format. The CSC system provides Highway-accelerated fast paths for `RGB8_sRGB` ↔ `RGB8_Planar_sRGB` interleaving, so the user-visible pipeline (RGB8 → JPEG XS → RGB8) works correctly — the deinterleave just happens in promeki's CSC layer instead of inside the SVT encoder.

**Performance impact:** The CSC deinterleave adds one extra pass over the pixel data before encode. For the decode path there is no penalty — SVT always outputs planar regardless of the input format.

- [ ] Monitor SVT-JPEG-XS upstream for a fix to the `send_picture` validation (the bug is in the mismatch between `pi->comps_num = 3` and `image_buffer_alloc` filling only component 0 for packed formats).
- [ ] Once fixed upstream: add `RGB8_sRGB` → `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` back to `classifyInput()` and update `JPEG_XS_RGB8_sRGB` `encodeSources` to include `RGB8_sRGB` directly, bypassing the CSC deinterleave on encode.
- [ ] Optional: add 10/12-bit planar RGB encode/decode (SVT supports `PLANAR_YUV444_OR_RGB` at all bit depths; needs new `P_444_3x10_LE` PixelMemLayout + `RGB10_Planar_LE_sRGB` PixelFormat + CSC fast paths).

---

## JPEG XS: additional matrix / range / colour-space variants

**Files:** `src/core/pixelformat.cpp`, `src/proav/jpegxsimagecodec.cpp`
**FIXME:** The first-pass codec exposes only the Rec.709 limited-range YUV family (`JPEG_XS_{YUV8,YUV10,YUV12}_{422,420}_Rec709`). JPEG XS itself is colour-space agnostic — matrix / range / primaries live out-of-band in the container (ISO/IEC 21122-3 sample entry) or the SDP (RFC 9134 `a=fmtp`), never in the bitstream — so adding more variants is purely a bookkeeping exercise once real workflows need them.

Likely additions when an upstream caller actually asks for them:
- `JPEG_XS_*_Rec601` / `JPEG_XS_*_Rec601_Full` — legacy broadcast / strict JFIF analogues.
- `JPEG_XS_*_Rec709_Full` — modern full-range YUV from cameras with an ICC profile.
- `JPEG_XS_*_Rec2020` — HDR / UHD contribution, including the 10- and 12-bit planar variants.
- `JPEG_XS_*_SemiPlanar_*` (NV12 / NV16) — if a zero-copy path from a hardware decoder wants to avoid the deinterleave cost on the input side (SVT needs planar, so the codec would have to run the NV-planar → fully-planar split like the JPEG codec already does).

- [ ] Wait for a concrete upstream need before expanding the matrix; the current Rec.709 limited-range default matches ST 2110 JPEG XS carriage.
- [ ] When adding, follow the JPEG variant pattern in `pixelformat.cpp` (`makeJPEG_XS_YUV` helper already structured for this) and extend `classifyInput()` / `defaultDecodeTarget()` accordingly.
- [ ] Extend the codec's validation in `decode()` to match on the new uncompressed targets.

---

## JPEG XS: QuickTime / ISO-BMFF container support (jxsm sample entry)

**Files:** `src/proav/quicktime_writer.cpp` (visual sample entry writer, `quickTimeCodecFourCC`), `src/proav/quicktime_reader.cpp` (video sample-entry parse path and `pixelFormatForQuickTimeFourCC`), `src/proav/mediaiotask_quicktime.cpp` (`pickStorageFormat`)
**FIXME:** `JpegXsImageCodec` can encode and decode JPEG XS and every `JPEG_XS_*` PixelFormat already carries `fourccList = { "jxsm" }`, but the QuickTime reader/writer does not implement the ISO/IEC 21122-3 Annex C sample entry. The writer currently closes the `jxsm` sample entry immediately after the base `VisualSampleEntry` header with no codec-specific child boxes (`quicktime_writer.cpp:847-903`), and the reader's `pixelFormatForQuickTimeFourCC()` (`quicktime_reader.cpp:33-41`) does a linear scan that always returns the first `JPEG_XS_*` variant (id 153) regardless of the actual codestream — the `VisualSampleEntry.depth` field is skipped (`quicktime_reader.cpp:466`) and the sample-entry parser never walks child boxes inside the entry (the remainder is `(void)entryRemain;` at line 531).

**Blocked on ISO/IEC 21122-3:2024 procurement.** Implementation will be a byte-exact spec-compliant reader and writer per Annex C ("Use of JPEG XS codestreams in the ISOBMFF — Motion JPEG XS"). Registered 4CCs per the MP4 Registration Authority (`github.com/mp4ra/mp4ra.github.io`): `jxsm` (sample entry), `jxpl` (Profile and Level), `jpvi` (Video Information), `jpvs` (Video Support), `jptp` (Video Transport Parameter).

- [ ] Obtain ISO/IEC 21122-3:2024 (`iso.org/standard/86420.html`).
- [ ] Implement the `jxsm` sample entry writer in `quicktime_writer.cpp` with all mandatory child boxes (`jxpl`, `jpvi`, and whatever else Annex C requires) byte-exact to the spec. Cite clause numbers in code comments.
- [ ] Implement sample-entry child-box parsing in `quicktime_reader.cpp`: walk child boxes until `entrySize` is exhausted (replacing the `(void)entryRemain;` at line 531), parse the JPEG XS boxes and the standard `colr` box, actually read `VisualSampleEntry.depth` (currently skipped at line 466).
- [ ] Map the parsed sample entry back to the correct `JPEG_XS_*` PixelFormat variant using the spec's fields (bit depth, sampling, colour model).
- [ ] Route `JPEG_XS_*` PixelFormats through `MediaIOTask_QuickTime::pickStorageFormat()` as their own storage format.
- [ ] Round-trip test `tests/quicktime_jpegxs.cpp`: write → read for each of the seven `JPEG_XS_*` PixelFormats, verify decoded frame matches source within codec tolerance.
- [ ] External interop test: verify the written `.mov` opens in whatever JPEG XS-in-MOV consumers exist at the time of implementation.

---

## JPEG XS: RFC 9134 slice-mode packetization

**Files:** `src/network/rtppayload.cpp` (`RtpPayloadJpegXs`), `src/proav/mediaiotask_rtp.cpp`, `include/promeki/mediaiotask_rtp.h`
**FIXME:** `RtpPayloadJpegXs` implements codestream mode (K=0): each `pack()` call fragments one complete codestream across MTU-sized packets with the 4-byte RFC 9134 header. Slice mode (K=1) — where the encoder emits one slice per packet and each slice fits in a single RTP packet — is not yet implemented. Slice mode is what ST 2110-22 mandates for constant-bitrate contribution links.

Slice mode requires:
- **Encoder integration**: SVT-JPEG-XS `slice_packetization_mode = 1` makes each `svt_jpeg_xs_encoder_get_packet()` return one slice-sized bitstream buffer. Needs a new `MediaConfig::JpegXsSlicePacketization` key plumbed into the encoder API.
- **Pack path**: instead of fragmenting a complete codestream, each encoder output buffer maps 1:1 to one RTP packet with `K=1` in the header. The `SEP` counter increments per slice; the `P` (last packet in frame) flag comes from `last_packet_in_frame` on the encoder output.
- **Unpack path**: reassembly by RTP timestamp is the same, but verification should check the per-frame packet counter for gaps / reordering.
- **SDP**: the `a=fmtp` line must carry `packetization-mode=1` so receivers know to expect slice boundaries at packet boundaries.

- [ ] Add `MediaConfig::JpegXsSlicePacketization` key; plumb into `JpegXsVideoEncoder` encoder init.
- [ ] Extend `RtpPayloadJpegXs::pack()` with a K=1 branch that maps encoder slices 1:1 to RTP packets.
- [ ] Update `RtpPayloadJpegXs::unpack()` to verify per-frame packet counter for gap detection.
- [ ] Update `buildJpegXsFmtp()` to emit `packetization-mode=1` when slice mode is active.
- [ ] Loopback test mirroring the existing codestream-mode coverage.

---

## QuickTimeWriter: compressed audio input path is missing

**File:** `src/proav/quicktime_writer.cpp` (`addAudioTrack`, `writeSample` audio branch)
**FIXME:** The writer side treats audio samples as raw PCM only. `addAudioTrack()` requires a PCM `AudioFormat`; it does not accept an `AudioDesc` whose `AudioFormat` is a compressed entry (`Opus`, `AAC`, …). The reader side handles compressed audio cleanly (after the Phase 6 AudioDesc/Audio extensions), so this is an asymmetric gap that blocks remux-style workflows (open a compressed source, write it to a new container without transcoding).

- [ ] Update `addAudioTrack()` to accept an `AudioDesc` whose `AudioFormat::isCompressed()` is true.
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

- [ ] Parse APP2 ICC profile from reassembled JFIF when present — extract primaries/matrix/TRC and map to the closest PixelFormat variant (Rec.601 vs Rec.709, full vs limited).
- [ ] Parse APP14 Adobe marker for the color transform byte as a secondary signal.
- [ ] Consider a `MediaConfig::VideoColorModel` override key so callers can force Rec.709 / limited range for broadcast sources that use JPEG transport without color metadata.

---

## ContentLightLevel / MasteringDisplay: missing `fromString()` parsers

**Files:** `include/promeki/contentlightlevel.h`, `include/promeki/masteringdisplay.h`, `src/core/variantserialize.cpp`, `demos/promeki-pipeline/frontend/src/components/SpecField.vue`
**FIXME:** `ContentLightLevel` and `MasteringDisplay` don't expose `fromString()` (or `Variant`-friendly JSON) round-trip parsers. The pipeline-demo frontend currently constructs the canonical formatted string (e.g. `MaxCLL=1000 MaxFALL=400 cd/m²`) and regex-parses it back in `SpecField.vue`'s composite editors, which is good enough within a session but loses information when the formatted layout changes. Fix: add `ContentLightLevel::fromString(const String &, Error*)` and `MasteringDisplay::fromString(...)`, or wire both types into `Variant`'s JSON serializers (`TypeContentLightLevel`, `TypeMasteringDisplay`) so they round-trip through the schema cleanly. Removes the regex parser from `SpecField.vue`'s composite editors.

- [ ] Add `ContentLightLevel::fromString(const String &, Error*)` symmetric with `toString()`.
- [ ] Add `MasteringDisplay::fromString(const String &, Error*)` symmetric with `toString()`.
- [ ] Alternatively, teach `Variant`'s JSON serializer to emit / consume both types as structured objects rather than relying on the toString form as the wire shape.
- [ ] Once landed, drop the `parseCll` / `parseMd` regexes from `SpecField.vue` in the pipeline demo and let the editor read fields from a structured value.

---

## CMake: libjpeg-turbo ExternalProject did not forward NASM compiler path

**File:** `CMakeLists.txt` (libjpeg-turbo ExternalProject_Add)
**FIXME (fixed):** The vendored libjpeg-turbo ExternalProject build did not forward `CMAKE_ASM_NASM_COMPILER` to the sub-build, causing `check_language(ASM_NASM)` to fail and SIMD to be silently disabled. Fixed by adding `find_program(PROMEKI_NASM_PATH NAMES nasm yasm)` and passing `-DCMAKE_ASM_NASM_COMPILER=${PROMEKI_NASM_PATH}` to the ExternalProject. Performance impact: libjpeg-turbo without SIMD is significantly slower for encode/decode.

- [x] Forward NASM path to libjpeg-turbo ExternalProject (fixed 2026-04-09)

