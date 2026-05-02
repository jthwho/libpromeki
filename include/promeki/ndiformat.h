/**
 * @file      ndiformat.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <cstdint>
#include <promeki/pixelformat.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Translation helpers between NDI FourCC values and promeki PixelFormat::IDs.
 * @ingroup proav
 *
 * NDI carries video as a small set of FourCC tags
 * (`NDIlib_FourCC_video_type_e`) that combine pixel layout, chroma
 * subsampling, and bit depth.  These helpers translate between those
 * raw FourCC values and the appropriate `PixelFormat::ID`, so callers
 * can keep all NDI plumbing in one place and pass typed promeki
 * formats through the rest of the pipeline.
 *
 * The FourCC values are passed and returned as @c uint32_t so this
 * header does not need to pull in the NDI SDK headers.  Callers that
 * already include `<Processing.NDI.structs.h>` can pass the enum
 * values directly — they implicitly convert.
 *
 * @par 10 / 12 / 16-bit content via P216
 *
 * NDI's only high-bit-depth FourCC is **P216**, a 16-bit-container
 * 4:2:2 semi-planar layout.  The semantic precision (10 / 12 / 16
 * bit) is *not* signalled by the FourCC — the SDK convention places
 * the meaningful bits in the high bits of each 16-bit sample.  The
 * helpers in this header use a separate @ref NdiBitDepth parameter
 * to disambiguate when needed; promeki's
 * `YUV{10,12,16}_422_SemiPlanar_LE_Rec709` IDs share the same wire
 * layout and translate to / from P216 byte-for-byte.
 */
class NdiFormat {
        public:
                /**
                 * @brief Bit-depth selector for FourCCs whose precision is implicit.
                 *
                 * Only meaningful for @c P216 / @c PA16 — the 8-bit FourCCs
                 * (UYVY / NV12 / I420 / BGRA / …) ignore this argument.
                 */
                enum BitDepth {
                        BitDepthAuto = 0, ///< Default — pick @c 16 for receivers, infer from format for senders.
                        BitDepth10   = 10,
                        BitDepth12   = 12,
                        BitDepth16   = 16,
                };

                /**
                 * @brief Map an NDI video FourCC to a promeki PixelFormat::ID.
                 *
                 * @param fourcc   The NDI FourCC value
                 *                 (`NDIlib_FourCC_video_type_e`).
                 * @param bitDepth Disambiguates @c P216 — controls whether the
                 *                 returned ID is the 10 / 12 / 16-bit semi-planar
                 *                 4:2:2 entry.  Ignored for FourCCs whose
                 *                 precision is explicit in the tag.
                 * @return The matching @ref PixelFormat::ID, or
                 *         @c PixelFormat::Invalid when the FourCC is unsupported
                 *         (e.g. YV12, UYVA, PA16 — see docs/ndi.md for the
                 *         deferred set).
                 */
                static PixelFormat::ID fourccToPixelFormat(uint32_t fourcc, BitDepth bitDepth = BitDepthAuto);

                /**
                 * @brief Map a promeki PixelFormat::ID to an NDI video FourCC.
                 *
                 * @param id The promeki format identifier.
                 * @return The matching NDI FourCC, or @c 0 when the format has
                 *         no equivalent in NDI's wire vocabulary.  Callers should
                 *         treat @c 0 as "rejection" and either convert upstream
                 *         (via the CSC framework) or fall back to a supported
                 *         format.
                 */
                static uint32_t pixelFormatToFourcc(PixelFormat::ID id);

                /**
                 * @brief Human-readable diagnostic for an NDI FourCC.
                 *
                 * Returns the four ASCII bytes (e.g. "UYVY", "P216") for known
                 * FourCCs, or a hex fallback for unknown values.  Useful in log
                 * messages and error reports.
                 */
                static String fourccToString(uint32_t fourcc);

                NdiFormat() = delete;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
