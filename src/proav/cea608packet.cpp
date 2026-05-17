/**
 * @file      cea608packet.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cea608.h>
#include <promeki/cea608packet.h>
#include <promeki/datastream.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Maps a @ref Cea608Packet::Channel onto its CDP
        ///        @c cc_type — 0 for field 1 (CC1 / CC2), 1 for
        ///        field 2 (CC3 / CC4).
        uint8_t ccTypeFor(Cea608Packet::Channel ch) {
                switch (ch) {
                        case Cea608Packet::Channel::CC1:
                        case Cea608Packet::Channel::CC2: return 0;
                        case Cea608Packet::Channel::CC3:
                        case Cea608Packet::Channel::CC4: return 1;
                }
                return 0;
        }

        /// @brief Returns @c true when @p ch is the second channel of
        ///        its field — CC2 (field 1) or CC4 (field 2).  The
        ///        intra-field channel selector lives in bit 3 of
        ///        @c cc_data.b1 after parity strip: clear for
        ///        CC1 / CC3, set for CC2 / CC4.
        bool isSecondChannelOfField(Cea608Packet::Channel ch) {
                return ch == Cea608Packet::Channel::CC2
                       || ch == Cea608Packet::Channel::CC4;
        }

        /// @brief @c true when @p triple's intra-field channel bit
        ///        matches @p ch.
        ///
        /// The channel bit only meaningfully addresses *control* bytes
        /// (b1 in @c 0x10..0x1F).  For character pairs (b1 in
        /// @c 0x20..0x7F) the receiver applies the bytes to the
        /// channel context of the most recently received control code,
        /// so the per-triple channel filter cannot tell which channel
        /// a character pair belongs to.  This filter optimistically
        /// includes character pairs in every channel's extraction —
        /// caller code that wants strict separation should use
        /// @ref Cea608Decoder which tracks channel context across
        /// triples.
        bool tripleMatchesChannel(const Cea708Cdp::CcData &t, Cea608Packet::Channel ch) {
                const uint8_t b1 = static_cast<uint8_t>(t.b1 & 0x7F); // strip parity
                if (b1 >= 0x10 && b1 <= 0x1F) {
                        const bool secondInField = (b1 & 0x08) != 0;
                        return secondInField == isSecondChannelOfField(ch);
                }
                // Character pair — no channel info on the wire.  Pass
                // through; downstream code applies the prior channel
                // context.
                return true;
        }

} // namespace

Cea608Packet Cea608Packet::fromCdp(const Cea708Cdp &cdp, Channel channel) {
        Cea608Packet                 out;
        out.channel = channel;
        const uint8_t wantCcType = ccTypeFor(channel);
        for (size_t i = 0; i < cdp.ccData.size(); ++i) {
                const Cea708Cdp::CcData &t = cdp.ccData[i];
                if (t.type != wantCcType) continue;
                if (!tripleMatchesChannel(t, channel)) continue;
                out.ccData.pushToBack(t);
        }
        return out;
}

Cea708Cdp Cea608Packet::toCdp(uint8_t frameRateCode, uint16_t sequenceCounter) const {
        Cea708Cdp cdp(frameRateCode, ccData, sequenceCounter);
        return cdp;
}

String Cea608Packet::channelName(Channel c) {
        switch (c) {
                case Channel::CC1: return String("CC1");
                case Channel::CC2: return String("CC2");
                case Channel::CC3: return String("CC3");
                case Channel::CC4: return String("CC4");
        }
        return String("CC?");
}

JsonObject Cea608Packet::toJson() const {
        JsonObject obj;
        obj.set("channel", channelName(channel));
        JsonArray arr;
        for (size_t i = 0; i < ccData.size(); ++i) {
                const Cea708Cdp::CcData &t = ccData[i];
                JsonObject               row;
                row.set("valid", t.valid);
                row.set("type", static_cast<int64_t>(t.type));
                row.set("b1", static_cast<int64_t>(t.b1));
                row.set("b2", static_cast<int64_t>(t.b2));
                arr.add(row);
        }
        obj.set("ccData", arr);
        return obj;
}

String Cea608Packet::toString() const {
        String s = "Cea608Packet(";
        s += channelName(channel);
        s += ", triples=";
        s += String::number(static_cast<int64_t>(ccData.size()));
        s += ")";
        return s;
}

// ============================================================================
// DataStream operators
// ============================================================================
//
// Wire layout:
//   uint8_t   channel   (TypeU8)
//   uint32_t  count     (TypeU32 — number of cc_data triples)
//   for each triple:
//     uint8_t valid     (TypeU8 — 0/1)
//     uint8_t type      (TypeU8 — 0..3)
//     uint8_t b1        (TypeU8)
//     uint8_t b2        (TypeU8)

Error Cea608Packet::writeToStream(DataStream &s) const {
        s << static_cast<uint8_t>(channel);
        s << static_cast<uint32_t>(ccData.size());
        for (size_t i = 0; i < ccData.size(); ++i) {
                const Cea708Cdp::CcData &t = ccData[i];
                s << static_cast<uint8_t>(t.valid ? 1 : 0);
                s << static_cast<uint8_t>(t.type);
                s << t.b1;
                s << t.b2;
        }
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<Cea608Packet> Cea608Packet::readFromStream<1>(DataStream &s) {
        Cea608Packet pkt;
        uint8_t      ch    = 0;
        uint32_t     count = 0;
        s >> ch >> count;
        if (s.status() != DataStream::Ok) return makeError<Cea608Packet>(s.toError());
        pkt.channel = static_cast<Cea608Packet::Channel>(ch);
        for (uint32_t i = 0; i < count; ++i) {
                Cea708Cdp::CcData t;
                uint8_t           validByte = 0;
                uint8_t           typeByte  = 0;
                s >> validByte >> typeByte >> t.b1 >> t.b2;
                if (s.status() != DataStream::Ok) return makeError<Cea608Packet>(s.toError());
                t.valid = (validByte != 0);
                t.type  = typeByte;
                pkt.ccData.pushToBack(t);
        }
        return makeResult(std::move(pkt));
}

PROMEKI_NAMESPACE_END
