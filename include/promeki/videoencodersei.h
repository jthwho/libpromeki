/**
 * @file      videoencodersei.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class Frame;
class MasteringDisplay;
class ContentLightLevel;
class AncTranslator;

/**
 * @brief Backend-neutral assembly of H.264 / HEVC SEI payloads.
 * @ingroup proav
 *
 * Both hardware (NVENC) and software (x264) encoders attach
 * Supplemental Enhancement Information as a list of
 * @c (payloadType, payload-bytes) pairs — NVENC through
 * @c NV_ENC_SEI_PAYLOAD, x264 through @c x264_sei_payload_t.  This class
 * builds the *payload body* bytes (the application-layer SEI content,
 * without the SEI message header or emulation-prevention bytes, which
 * each codec adds itself) so the per-codec plumbing reduces to copying a
 * @ref SeiPayload into the codec's own descriptor.
 *
 * Three sources are covered here, lifted from / shared with the NVENC
 * backend:
 *
 *  - **Closed captions** (payloadType 4,
 *    @c user_data_registered_itu_t_t35): walks a source @ref Frame's ANC
 *    and translates CEA-708 packets onto the H.264 / HEVC HLS-SEI wire
 *    transport.
 *  - **HDR mastering display** (payloadType 137,
 *    @c mastering_display_colour_volume).
 *  - **HDR content light level** (payloadType 144,
 *    @c content_light_level_info).
 *
 * @par Thread Safety
 * Stateless aside from the caller-owned @ref AncTranslator passed to
 * @ref captions; all methods are otherwise pure.
 */
class VideoEncoderSei {
        public:
                /** @brief SEI payloadType 4 — user_data_registered_itu_t_t35 (captions / AFD / bar data). */
                static constexpr int TypeUserDataRegistered = 4;
                /** @brief SEI payloadType 137 — mastering_display_colour_volume. */
                static constexpr int TypeMasteringDisplay = 137;
                /** @brief SEI payloadType 144 — content_light_level_info. */
                static constexpr int TypeContentLightLevel = 144;

                /**
                 * @brief One SEI message: its payloadType and body bytes.
                 *
                 * @c bytes holds only the SEI payload content; the codec
                 * wraps it with the SEI message header (type + size) and
                 * emulation-prevention bytes.
                 */
                struct SeiPayload {
                                int    type = 0; ///< SEI payloadType.
                                Buffer bytes;    ///< SEI payload body bytes.
                };

                /**
                 * @brief Builds caption SEI payloads (type 4) from a source Frame's ANC.
                 *
                 * Collects the frame's CEA-708 ANC via
                 * @c VideoEncoder::selectAncForSei (pairing on
                 * @p videoStreamIndex, accepting unbound -1 streams) and
                 * runs each packet through @p translator to the
                 * @c AncTransport::HlsSei carrier
                 * (ATSC A/53 @c user_data_registered_itu_t_t35).  Returns
                 * one @ref SeiPayload per produced packet; empty when the
                 * frame carries no matching captions.
                 *
                 * @param source           The source Frame being encoded.
                 * @param videoStreamIndex The video stream the encoder is
                 *                         encoding (ANC paired to this index
                 *                         or unbound is in scope).
                 * @param translator       Caller-owned translator session
                 *                         (the Cea708 → HlsSei builder is
                 *                         pure; a shared instance avoids
                 *                         per-frame construction cost).
                 */
                static List<SeiPayload> captions(const Frame &source, int videoStreamIndex, AncTranslator &translator);

                /**
                 * @brief Builds a mastering_display_colour_volume SEI (type 137).
                 *
                 * Emits the 24-byte payload body in the spec's green / blue
                 * / red primary order, chromaticity in 0.00002 units and
                 * luminance in 0.0001 cd/m² units.  Returns an empty
                 * payload (@c bytes.size() == 0) when @p md is invalid.
                 */
                static SeiPayload masteringDisplay(const MasteringDisplay &md);

                /**
                 * @brief Builds a content_light_level_info SEI (type 144).
                 *
                 * Emits the 4-byte payload body (max_content_light_level,
                 * max_pic_average_light_level), each clamped to 16 bits.
                 * Returns an empty payload when @p cll is invalid.
                 */
                static SeiPayload contentLightLevel(const ContentLightLevel &cll);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
