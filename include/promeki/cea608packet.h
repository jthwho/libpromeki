/**
 * @file      cea608packet.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/cea708cdp.h>
#include <promeki/enums.h>
#include <promeki/json.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Per-frame CEA-608 channel data.
 * @ingroup proav
 *
 * Value-type analogue of @ref Cea708Cdp for CEA-608: holds a
 * @ref CaptionChannel selector plus the @c cc_data triples that
 * target it on a given frame.  Unlike @ref Cea708Cdp the
 * @ref Cea608Packet does not represent its own wire packet — CEA-608
 * cc_data always rides inside a CDP on every transport (SDI, ST
 * 2110-40, RTMP, HLS SEI).  @ref Cea608Packet exists as the typed
 * application-facing handle for 608-only data, paired with cheap
 * @ref fromCdp / @ref toCdp converters to bridge a parsed @ref Cea708Cdp.
 *
 * Per-frame, per-channel — one @ref Cea608Packet carries the byte
 * pair for one specific @ref CaptionChannel.  Multi-channel streams
 * (e.g. CC1 + CC2 in field 1) extract two @ref Cea608Packet values
 * from the same @ref Cea708Cdp via @ref fromCdp with two different
 * channel arguments.
 *
 * @par Wire mapping
 *
 *  - Each entry of @ref ccData has the spec's @c cc_valid, @c cc_type
 *    (0 = field 1, 1 = field 2 — CC1/CC2 in field 1; CC3/CC4 in field
 *    2), and the two parity-stamped data bytes.
 *  - @ref channel disambiguates the intra-field channel via bit 3 of
 *    the first byte: CC1/CC3 have bit 3 clear; CC2/CC4 have bit 3 set.
 *  - @ref fromCdp filters @ref Cea708Cdp::ccData by @c cc_type and
 *    channel-bit and stuffs the matching triples into @ref ccData.
 *  - @ref toCdp wraps @ref ccData into a fresh @ref Cea708Cdp with
 *    the supplied @c frameRateCode + sequence counter.
 *
 * @par Variant / DataStream integration
 *
 * Registered as @c Variant::TypeCea608 with tag
 * @c DataStream::TypeCea608 (@c 0x5F).
 *
 * @par Thread Safety
 * Plain value type — copies are independent.  Distinct instances may
 * be used concurrently; concurrent access to a single instance is
 * not internally synchronised.
 *
 * @see Cea708Cdp, Cea608Encoder, Cea608Decoder, AncTranslator
 */
class Cea608Packet {
        public:
                /** @brief Channel selector (mirror of the encoder /
                 *         decoder Channel enums for the typed-value
                 *         interchange path).  CC1 / CC2 live in field
                 *         1, CC3 / CC4 in field 2. */
                enum class Channel : uint8_t {
                        CC1 = 0, ///< Field 1, channel 1.
                        CC2 = 1, ///< Field 1, channel 2.
                        CC3 = 2, ///< Field 2, channel 1.
                        CC4 = 3, ///< Field 2, channel 2.
                };

                /// @brief Channel this packet's @c ccData targets.
                Channel channel = Channel::CC1;

                /// @brief cc_data triples for this channel on this
                ///        frame.  Typically zero or one entry per
                ///        packet — 608 carries at most one byte pair
                ///        per channel per frame, but the list shape
                ///        is preserved for symmetry with
                ///        @ref Cea708Cdp::ccData.
                Cea708Cdp::CcDataList ccData;

                Cea608Packet() = default;

                /**
                 * @brief Constructs a packet with the given channel
                 *        and pre-built triple list.  No filtering —
                 *        the caller has already extracted the
                 *        channel-matching triples.
                 */
                Cea608Packet(Channel channel, Cea708Cdp::CcDataList ccData)
                    : channel(channel), ccData(std::move(ccData)) {}

                // -- Conversion to / from Cea708Cdp -----------------------

                /**
                 * @brief Extracts the @p channel-targeted triples from
                 *        @p cdp into a fresh @ref Cea608Packet.
                 *
                 * Filters @ref Cea708Cdp::ccData by:
                 *  - @c cc_type — 0 for CC1 / CC2, 1 for CC3 / CC4.
                 *  - intra-field channel selector — bit 3 of @c b1
                 *    after parity strip: clear for CC1 / CC3, set for
                 *    CC2 / CC4.
                 *
                 * Returns @c Error::Ok with an empty @ref ccData when
                 * @p cdp carries no triples for the requested channel.
                 *
                 * @param cdp     Source CDP (typically a fresh
                 *                @ref AncTranslator::parse result).
                 * @param channel Which 608 channel to extract.
                 */
                static Cea608Packet fromCdp(const Cea708Cdp &cdp, Channel channel);

                /**
                 * @brief Wraps @ref ccData into a fresh @ref Cea708Cdp
                 *        that's ready to feed back into
                 *        @ref AncTranslator::build for wire emission.
                 *
                 * @param frameRateCode SMPTE 334-2 frame-rate code
                 *                      (1..8); pass 0 when the
                 *                      destination doesn't care
                 *                      about the field.
                 * @param sequenceCounter Header / footer sequence
                 *                        counter; the caller manages
                 *                        the value across consecutive
                 *                        packets.
                 */
                Cea708Cdp toCdp(uint8_t frameRateCode = 0, uint16_t sequenceCounter = 0) const;

                // -- JSON dump --------------------------------------------

                /**
                 * @brief Produces a JSON representation for inspection.
                 *
                 * Shape: @c {channel, ccData: [{valid, type, b1, b2}, ...]}.
                 * Used by the @ref InspectorMediaIO dump and any
                 * tooling that wants a stable typed view of 608 data
                 * separate from the CDP wrapping.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Short human-readable summary.
                 *
                 * Reports the channel name + triple count; designed
                 * for log lines, not machine consumption.
                 */
                String toString() const;

                // -- Comparison -------------------------------------------

                bool operator==(const Cea608Packet &o) const {
                        return channel == o.channel && ccData == o.ccData;
                }
                bool operator!=(const Cea608Packet &o) const { return !(*this == o); }

                /**
                 * @brief Returns @c "CC1" / @c "CC2" / @c "CC3" /
                 *        @c "CC4" for the @c Channel enum value.
                 */
                static String channelName(Channel c);
};

/** @brief Writes a @ref Cea608Packet to a @ref DataStream. */
DataStream &operator<<(DataStream &stream, const Cea608Packet &pkt);

/** @brief Reads a @ref Cea608Packet from a @ref DataStream. */
DataStream &operator>>(DataStream &stream, Cea608Packet &pkt);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV