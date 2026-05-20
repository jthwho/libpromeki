/**
 * @file      anccodec_cea708_hlssei.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * ATSC A/53 SEI user_data wrapper around CEA-708 cc_data triples.
 * Carries CEA-708 captions inside an H.264 / HEVC
 * user_data_registered_itu_t_t35 SEI message.  This is the
 * practical caption delivery path for YouTube Live, Twitch, and
 * any modern CDN that ingests H.264 over RTMP / HLS / SRT.
 *
 * Wire format (ATSC A/53 Part 4 §6.2.2):
 *
 *   byte 0   : itu_t_t35_country_code (0xB5 = USA)
 *   bytes 1-2: itu_t_t35_provider_code (0x0031 = ATSC, big-endian)
 *   bytes 3-6: user_identifier ("GA94", 0x47413934)
 *   byte 7   : user_data_type_code (0x03 = cc_data)
 *   byte 8   : reserved(1)=1 | process_cc_data_flag(1)=1 | zero_bit(1)=0 | cc_count(5)
 *   byte 9   : em_data (reserved, 0xFF)
 *   bytes 10..: cc_count × 3 bytes:
 *                  byte 0 : marker_bits(5)=0x1F | cc_valid(1) | cc_type(2)
 *                  bytes 1-2: cc_data_1, cc_data_2
 *   trailing : marker_bits (0xFF)
 *
 * The parser returns a minimal @ref Cea708Cdp populated with only
 * the cc_data triples (frame-rate code 0, no timecode, sequence
 * counter 0) — the rest of the CDP fields aren't carried in the
 * SEI and aren't recoverable.  The builder consumes the
 * @ref Cea708Cdp::ccData triples; everything else is dropped.
 */

#include <cstdint>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/result.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr uint8_t  T35CountryUsa = 0xB5;
        constexpr uint16_t T35ProviderAtsc = 0x0031;
        constexpr uint32_t UserIdentifierGa94 = 0x47413934; // "GA94"
        constexpr uint8_t  UserDataTypeCodeCcData = 0x03;

        AncTranslator::ParseResult parseCea708HlsSei(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                const Buffer &data = pkt.data();
                if (data.size() < 10) {
                        // Minimum: country(1) + provider(2) + user_id(4) +
                        // type_code(1) + flags(1) + em_data(1) = 10 bytes
                        // before any cc_data triple.
                        return makeError<Variant>(Error::CorruptData);
                }
                const auto *p = static_cast<const uint8_t *>(data.data());
                size_t      i = 0;
                if (p[i++] != T35CountryUsa) {
                        return makeError<Variant>(Error::CorruptData);
                }
                const uint16_t provider = static_cast<uint16_t>((p[i] << 8) | p[i + 1]);
                i += 2;
                if (provider != T35ProviderAtsc) {
                        return makeError<Variant>(Error::CorruptData);
                }
                const uint32_t userId = (static_cast<uint32_t>(p[i]) << 24)
                                        | (static_cast<uint32_t>(p[i + 1]) << 16)
                                        | (static_cast<uint32_t>(p[i + 2]) << 8)
                                        | static_cast<uint32_t>(p[i + 3]);
                i += 4;
                if (userId != UserIdentifierGa94) {
                        return makeError<Variant>(Error::CorruptData);
                }
                const uint8_t userDataTypeCode = p[i++];
                if (userDataTypeCode != UserDataTypeCodeCcData) {
                        return makeError<Variant>(Error::CorruptData);
                }
                const uint8_t flagsByte = p[i++];
                const uint8_t ccCount = static_cast<uint8_t>(flagsByte & 0x1F);
                ++i; // em_data (reserved 0xFF) — skipped, no validation.
                if (i + static_cast<size_t>(ccCount) * 3 > data.size()) {
                        return makeError<Variant>(Error::CorruptData);
                }
                Cea708Cdp::CcDataList ccData;
                for (uint8_t k = 0; k < ccCount; ++k) {
                        const uint8_t b0 = p[i];
                        const uint8_t b1 = p[i + 1];
                        const uint8_t b2 = p[i + 2];
                        i += 3;
                        Cea708Cdp::CcData t;
                        t.valid = (b0 & 0x04) != 0;
                        t.type = static_cast<uint8_t>(b0 & 0x03);
                        t.b1 = b1;
                        t.b2 = b2;
                        ccData.pushToBack(t);
                }
                Cea708Cdp cdp;
                cdp.ccData = std::move(ccData);
                cdp.ccDataPresent = !cdp.ccData.isEmpty();
                cdp.captionServiceActive = cdp.ccDataPresent;
                return makeResult<Variant>(Variant(cdp));
        }

        AncTranslator::PacketsResult buildCea708HlsSei(const Variant &v, const AncTranslateConfig & /*cfg*/) {
                Cea708Cdp                    cdp = v.get<Cea708Cdp>();
                const Cea708Cdp::CcDataList &ccData = cdp.ccData;
                // SEI carries the cc_count in 5 bits, so up to 31 triples
                // per access unit.  Real-world frame budgets are
                // considerably smaller (≤ 20 typical) so this is rarely
                // a constraint; surface the error if a producer pushes
                // past it rather than silently truncating.
                if (ccData.size() > 31) {
                        promekiWarn("Cea708HlsSei builder: %zu cc_data triples exceeds the SEI "
                                    "5-bit cc_count cap (31)",
                                    ccData.size());
                        return makeError<AncPacket::List>(Error::OutOfRange);
                }
                List<uint8_t> bytes;
                bytes.reserve(11 + ccData.size() * 3);
                bytes.pushToBack(T35CountryUsa);
                bytes.pushToBack(static_cast<uint8_t>((T35ProviderAtsc >> 8) & 0xFF));
                bytes.pushToBack(static_cast<uint8_t>(T35ProviderAtsc & 0xFF));
                bytes.pushToBack(static_cast<uint8_t>((UserIdentifierGa94 >> 24) & 0xFF));
                bytes.pushToBack(static_cast<uint8_t>((UserIdentifierGa94 >> 16) & 0xFF));
                bytes.pushToBack(static_cast<uint8_t>((UserIdentifierGa94 >> 8) & 0xFF));
                bytes.pushToBack(static_cast<uint8_t>(UserIdentifierGa94 & 0xFF));
                bytes.pushToBack(UserDataTypeCodeCcData);
                // flags byte: reserved(1)=1 | process_cc_data_flag(1)=1 |
                //              zero_bit(1)=0 | cc_count(5)
                const uint8_t flagsByte =
                        static_cast<uint8_t>(0xC0 | (static_cast<uint8_t>(ccData.size()) & 0x1F));
                bytes.pushToBack(flagsByte);
                bytes.pushToBack(0xFF); // em_data (reserved)
                for (size_t k = 0; k < ccData.size(); ++k) {
                        const Cea708Cdp::CcData &t = ccData[k];
                        const uint8_t            b0 = static_cast<uint8_t>(
                                0xF8 /* marker bits */ | (t.valid ? 0x04 : 0x00)
                                | (t.type & 0x03));
                        bytes.pushToBack(b0);
                        bytes.pushToBack(t.b1);
                        bytes.pushToBack(t.b2);
                }
                bytes.pushToBack(0xFF); // trailing marker_bits.

                Buffer wire(bytes.size());
                wire.setSize(bytes.size());
                if (!bytes.isEmpty()) wire.copyFrom(bytes.data(), bytes.size(), 0);

                AncPacket pkt(AncFormat(AncFormat::Cea708), AncTransport::HlsSei, std::move(wire),
                               Metadata());
                AncPacket::List out;
                out.pushToBack(std::move(pkt));
                return makeResult<AncPacket::List>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Cea708_HlsSei, Cea708, ::promeki::AncTransport::HlsSei,
                             ::promeki::parseCea708HlsSei)
PROMEKI_REGISTER_ANC_BUILDER(Cea708_HlsSei, Cea708, ::promeki::AncTransport::HlsSei,
                              ::promeki::buildCea708HlsSei)
