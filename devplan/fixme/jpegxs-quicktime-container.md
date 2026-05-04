# JPEG XS: QuickTime / ISO-BMFF container support (`jxsm` sample entry)

**Files:** `src/proav/quicktime_writer.cpp` (visual sample entry
writer, `quickTimeCodecFourCC`), `src/proav/quicktime_reader.cpp`
(video sample-entry parse path and `pixelFormatForQuickTimeFourCC`),
`src/proav/mediaiotask_quicktime.cpp` (`pickStorageFormat`)

**FIXME:** `JpegXsImageCodec` can encode and decode JPEG XS and
every `JPEG_XS_*` PixelFormat already carries
`fourccList = { "jxsm" }`, but the QuickTime reader/writer does not
implement the ISO/IEC 21122-3 Annex C sample entry. The writer
currently closes the `jxsm` sample entry immediately after the base
`VisualSampleEntry` header with no codec-specific child boxes
(`quicktime_writer.cpp:847-903`), and the reader's
`pixelFormatForQuickTimeFourCC()` (`quicktime_reader.cpp:33-41`) does
a linear scan that always returns the first `JPEG_XS_*` variant
(id 153) regardless of the actual codestream — the
`VisualSampleEntry.depth` field is skipped
(`quicktime_reader.cpp:466`) and the sample-entry parser never
walks child boxes inside the entry (the remainder is
`(void)entryRemain;` at line 531).

**Blocked on ISO/IEC 21122-3:2024 procurement.** Implementation
will be a byte-exact spec-compliant reader and writer per
Annex C ("Use of JPEG XS codestreams in the ISOBMFF — Motion
JPEG XS"). Registered 4CCs per the MP4 Registration Authority
(`github.com/mp4ra/mp4ra.github.io`): `jxsm` (sample entry), `jxpl`
(Profile and Level), `jpvi` (Video Information), `jpvs` (Video
Support), `jptp` (Video Transport Parameter).

## Tasks

- [ ] Obtain ISO/IEC 21122-3:2024
  (`iso.org/standard/86420.html`).
- [ ] Implement the `jxsm` sample-entry writer in
  `quicktime_writer.cpp` with all mandatory child boxes (`jxpl`,
  `jpvi`, and whatever else Annex C requires) byte-exact to the
  spec. Cite clause numbers in code comments.
- [ ] Implement sample-entry child-box parsing in
  `quicktime_reader.cpp`: walk child boxes until `entrySize` is
  exhausted (replacing the `(void)entryRemain;` at line 531), parse
  the JPEG XS boxes and the standard `colr` box, actually read
  `VisualSampleEntry.depth` (currently skipped at line 466).
- [ ] Map the parsed sample entry back to the correct `JPEG_XS_*`
  PixelFormat variant using the spec's fields (bit depth, sampling,
  colour model).
- [ ] Route `JPEG_XS_*` PixelFormats through
  `QuickTimeMediaIO::pickStorageFormat()` as their own storage
  format.
- [ ] Round-trip test `tests/quicktime_jpegxs.cpp`: write → read for
  each of the seven `JPEG_XS_*` PixelFormats, verify decoded frame
  matches source within codec tolerance.
- [ ] External interop test: verify the written `.mov` opens in
  whatever JPEG XS-in-MOV consumers exist at the time of
  implementation.
