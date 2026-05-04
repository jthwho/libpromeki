# JPEG XS: SVT-JPEG-XS `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` validation bug

**Files:** SVT-JPEG-XS
`Source/Lib/Encoder/Codec/EncHandle.c`
(`svt_jpeg_xs_encoder_send_picture`, line ~958)

**FIXME:** SVT-JPEG-XS advertises
`COLOUR_FORMAT_PACKED_YUV444_OR_RGB` for interleaved 8/10-bit RGB
input (the encoder deinterleaves to planar internally with
AVX2/AVX512 fast paths). However,
`svt_jpeg_xs_encoder_send_picture()` has a validation bug that
prevents it from working: the validation loop at line ~958 iterates
`pi->comps_num` (which is 3 for RGB — set by
`format_get_sampling_factory` in `Pi.c:540`) and checks `stride[c]`
/ `alloc_size[c]` for all 3 logical components. But
`svt_jpeg_xs_image_buffer_alloc()` (in `ImageBuffer.c:32`) only
fills `stride[0]` / `alloc_size[0]` / `data_yuv[0]` for packed RGB,
leaving components 1 and 2 at zero. The validation computes
`min_size = 0 * (height-1) + width * pixel_size = width`, finds
`alloc_size[1] = 0 < width`, and returns
`SvtJxsErrorBadParameter`.

**Current workaround:** The codec uses
`COLOUR_FORMAT_PLANAR_YUV444_OR_RGB` with `RGB8_Planar_sRGB` as the
native encode/decode format. The CSC system provides
Highway-accelerated fast paths for `RGB8_sRGB` ↔ `RGB8_Planar_sRGB`
interleaving, so the user-visible pipeline (RGB8 → JPEG XS → RGB8)
works correctly — the deinterleave just happens in promeki's CSC
layer instead of inside the SVT encoder.

**Performance impact:** The CSC deinterleave adds one extra pass
over the pixel data before encode. For the decode path there is no
penalty — SVT always outputs planar regardless of the input format.

## Tasks

- [ ] Monitor SVT-JPEG-XS upstream for a fix to the `send_picture`
  validation (the bug is in the mismatch between `pi->comps_num = 3`
  and `image_buffer_alloc` filling only component 0 for packed
  formats).
- [ ] Once fixed upstream: add `RGB8_sRGB` →
  `COLOUR_FORMAT_PACKED_YUV444_OR_RGB` back to `classifyInput()` and
  update `JPEG_XS_RGB8_sRGB` `encodeSources` to include `RGB8_sRGB`
  directly, bypassing the CSC deinterleave on encode.
- [ ] Optional: add 10/12-bit planar RGB encode/decode (SVT supports
  `PLANAR_YUV444_OR_RGB` at all bit depths; needs new
  `P_444_3x10_LE` PixelMemLayout + `RGB10_Planar_LE_sRGB`
  PixelFormat + CSC fast paths).
