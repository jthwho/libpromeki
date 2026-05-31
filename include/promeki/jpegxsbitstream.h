/**
 * @file      jpegxsbitstream.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/pixelformat.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level JPEG XS (ISO/IEC 21122) codestream marker helpers.
 * @ingroup proav
 *
 * JPEG XS bitstreams are a sequence of two-byte markers, each of which
 * is either delimiter-only (SOC, EOC) or carries a 16-bit length and a
 * payload.  This class parses the small handful of markers needed to
 * recover the picture's width / height / component layout / bit depth
 * — enough for an MPEG-TS or RTP receiver to populate the right
 * @ref PixelFormat variant on the produced @ref ImageDesc.
 *
 * @par Marker codes (ISO/IEC 21122 §A.5)
 *  - @c SOC (Start of codestream)              — @c 0xFF10, delimiter
 *  - @c PIH (Picture header)                   — @c 0xFF12, with length
 *  - @c CDT (Component table)                  — @c 0xFF13, with length
 *  - @c WGT (Weights table)                    — @c 0xFF14, with length
 *  - @c COM (Comment)                          — @c 0xFF15, with length
 *  - @c NLT (Non-linearity table)              — @c 0xFF16, with length
 *  - @c CWD (Component-dependent decomp params) — @c 0xFF17, with length
 *  - @c CTS (Colour transform spec)            — @c 0xFF18, with length
 *  - @c CRG (Component registration)           — @c 0xFF19, with length
 *  - @c CAP (Capabilities)                     — @c 0xFF50, with length
 *  - @c EOC (End of codestream)                — @c 0xFF11, delimiter
 *
 * Markers other than PIH / CDT are walked over to find the ones we
 * care about — if a stream omits PIH the parser returns
 * @c Error::NotFound.
 *
 * @par Thread Safety
 * All methods are pure functions of their arguments.
 */
class JpegXsBitstream {
        public:
                /** @brief Two-byte marker tags. */
                enum Marker : uint16_t {
                        MarkerSoc = 0xFF10, ///< Start of codestream (delimiter).
                        MarkerEoc = 0xFF11, ///< End of codestream (delimiter).
                        MarkerPih = 0xFF12, ///< Picture header.
                        MarkerCdt = 0xFF13, ///< Component table.
                        MarkerWgt = 0xFF14, ///< Weights table.
                        MarkerCom = 0xFF15, ///< Comment.
                        MarkerNlt = 0xFF16, ///< Non-linearity table.
                        MarkerCwd = 0xFF17, ///< Component-dependent decomp params.
                        MarkerCts = 0xFF18, ///< Colour transform spec.
                        MarkerCrg = 0xFF19, ///< Component registration.
                        MarkerCap = 0xFF50, ///< Capabilities.
                };

                /**
                 * @brief Fields decoded from the PIH + CDT markers.
                 *
                 * The PIH (Picture header) carries the picture
                 * geometry and per-frame parameters (profile, level,
                 * frame width/height, bit depth, component count,
                 * colour space hint).  The CDT (Component table) gives
                 * per-component sub-sampling ratios.
                 *
                 * @par Common-case decoded values
                 *  - @c YUV 4:2:2 10-bit Rec.709 (broadcast contribution):
                 *    @c width=1920 @c height=1080 @c numComponents=3
                 *    @c bitDepth=10 @c hSub=[1,2,2] @c vSub=[1,1,1]
                 *  - @c YUV 4:2:0 8-bit Rec.709:
                 *    @c numComponents=3 @c bitDepth=8 @c hSub=[1,2,2]
                 *    @c vSub=[1,2,2]
                 *  - @c RGB 8-bit sRGB:
                 *    @c numComponents=3 @c bitDepth=8 @c hSub=[1,1,1]
                 *    @c vSub=[1,1,1] @c colourSpace=RGB
                 */
                struct PictureInfo {
                                uint16_t width = 0;
                                uint16_t height = 0;
                                uint8_t  numComponents = 0;
                                uint8_t  bitDepth = 0;                   ///< Bits per component (Y plane / first component).
                                uint16_t profile = 0;                    ///< @c Ppih from the PIH.
                                uint16_t level = 0;                      ///< @c Plev from the PIH.
                                uint8_t  hSubsampling[4] = {1, 1, 1, 1}; ///< @c Sx per component (luma=1, chroma 2 for 4:2:x).
                                uint8_t  vSubsampling[4] = {1, 1, 1, 1}; ///< @c Sy per component (luma=1, chroma 2 for 4:2:0).
                                uint8_t  perComponentBitDepth[4] = {0, 0, 0, 0}; ///< Bit depth per component from CDT.
                                bool     hasCdt = false; ///< @c true when a CDT marker was found.
                };

                /**
                 * @brief Parses the PIH + CDT markers at the start of a
                 *        JPEG XS codestream.
                 *
                 * Walks markers from the SOC (or the first byte if the
                 * SOC has been stripped by the caller) until it has
                 * either decoded PIH + CDT, hit EOC, or run out of
                 * input.  Skips markers it doesn't recognise, so
                 * forward-compatible bitstreams that grow new tables
                 * still parse cleanly.
                 *
                 * @param view  Codestream bytes.  May or may not start
                 *              with the @c SOC delimiter — both forms
                 *              are accepted.
                 * @param out   Receives the parsed fields.
                 * @return @c Error::Ok on success,
                 *         @c Error::NotFound when no PIH marker is
                 *         found before EOC / end of input,
                 *         @c Error::CorruptData when a marker's
                 *         length runs past @p view,
                 *         @c Error::InvalidArgument when @p view is
                 *         empty.
                 */
                static Error parsePictureHeader(const BufferView &view, PictureInfo &out);

                /**
                 * @brief Maps a parsed @ref PictureInfo onto the most
                 *        appropriate well-known JPEG XS @ref PixelFormat.
                 *
                 * Currently distinguishes:
                 *  - YUV 4:2:2 8/10/12-bit Rec.709
                 *  - YUV 4:2:0 8/10/12-bit Rec.709
                 *  - RGB 8-bit sRGB
                 *
                 * Returns @ref PixelFormat::Invalid for combinations
                 * that aren't first-class (4:4:4 YUV, exotic component
                 * counts) — the caller should fall back to the generic
                 * @c PixelFormat::JPEG_XS_YUV10_422_Rec709 placeholder
                 * in that case.
                 */
                static PixelFormat::ID pixelFormatFor(const PictureInfo &info);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
