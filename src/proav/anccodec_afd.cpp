/**
 * @file      anccodec_afd.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE 2016-3 AFD (Active Format Description) parser and builder for
 * @c AncTransport::St291.  AFD packets are DID=0x41, SDID=0x05, DC=8
 * fixed; the 8 UDWs (ST 2016-3 §4.1 Table 1) carry both the AFD code +
 * AR flag and the companion ST 2016-1 letterbox / pillarbox Bar Data:
 *
 *  | UDW | Bits 7–0                                                   |
 *  |-----|------------------------------------------------------------|
 *  | 1   | '0' | a3 | a2 | a1 | a0 | AR | '0' | '0'                   |
 *  | 2   | Reserved (zero)                                            |
 *  | 3   | Reserved (zero)                                            |
 *  | 4   | Bar-data flags: Top | Bot | Left | Right | '0' | '0' | '0' | '0' |
 *  | 5-6 | Bar Data Value 1 (16 bits, MSB-first across the two UDWs)  |
 *  | 7-8 | Bar Data Value 2 (16 bits, MSB-first across the two UDWs)  |
 *
 * The codec round-trips every wire field through an @ref AncAfd value
 * type — the AFD code, the AR flag, the four bar-data edge flags
 * (Top / Bottom / Left / Right) in UDW 4, and the two 16-bit values
 * spanning UDWs 5-6 / 7-8.  Round-tripping a captured AFD packet that
 * carried bar data preserves it byte-for-byte rather than the
 * pre-F4 "stripped bar variant" emitted by the legacy uint8_t-Variant
 * path.
 *
 * @par Note on Pan-Scan
 *
 * ST 2016-3 has @em no separate Bar-Data ANC packet — both AFD and
 * Bar Data ride in the single combined packet shown above.  SDID 0x06
 * (the destination of the pre-F4 mis-registered @c AncFormat::BarData)
 * is ST 2016-4 Pan-Scan, now correctly registered as
 * @c AncFormat::PanScan and handled by a separate codec when that
 * lands.
 */

#include <promeki/ancafd.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/datatype.h>
#include <promeki/enums_anc.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr size_t kAfdUdwCount = 8;

        // -- Wire helpers --------------------------------------------------

        // Promotes a Variant carrying either AncAfd or the legacy
        // packed-uint8_t shape (bits 3..6 = AFD code, bit 7 = AR) into
        // an AncAfd with zero bar-data fields.  The uint8_t form was
        // the pre-F4 codec contract — surface a thin compatibility
        // shim so existing builders that don't yet have bar-data
        // available continue to compile and emit valid packets.
        AncAfd variantToAncAfd(const Variant &v) {
                if (v.type() == DataTypeAncAfd) return v.get<AncAfd>();
                uint8_t packed = v.get<uint8_t>();
                uint8_t code = static_cast<uint8_t>((packed >> 3) & 0x0F);
                bool    ar = (packed & 0x80) != 0;
                return AncAfd(code, ar);
        }

        // Builds UDW 1 from the AFD code (bits 6..3) and AR flag
        // (bit 2) per ST 2016-3 Table 1; bits 7, 1, 0 are reserved-zero.
        inline uint8_t makeUdw1(uint8_t afdCode, bool ar) {
                uint8_t v = static_cast<uint8_t>((afdCode & 0x0F) << 3);
                if (ar) v = static_cast<uint8_t>(v | 0x04);
                return v;
        }

        AncTranslator::ParseResult parseAfdSt291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
                if (rp.second().isError()) return makeError<Variant>(rp.second());

                const St291Packet &p = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < 1) return makeError<Variant>(Error::CorruptData);

                // ST 2016-3 §4 / Table 1: DC shall be 8.  Senders that
                // truncate are non-conformant; warn so the divergence
                // surfaces in diagnostics, but Postel-tolerate the AFD
                // code + AR flag from UDW 1 which is what the rest of
                // this codec needs to make forward progress.  Strict
                // receivers should reject the packet; the library
                // defers that to a follow-up StrictValidate policy.
                if (udw.size() != kAfdUdwCount) {
                        promekiWarn("anccodec_afd: ST 2016-3 §4 mandates DC=8; received DC=%zu — "
                                    "Postel-tolerated, bar-data omitted on short packets",
                                    udw.size());
                }

                // UDW 1: AFD code in bits 6..3, AR in bit 2 (ST 2016-3
                // Table 1).  Reserved bits 7, 1, 0 are not checked
                // here — the library treats reserved-zero violations
                // as forward-compatible bits we ignore.
                uint8_t first = static_cast<uint8_t>(udw[0] & 0xFF);
                uint8_t code = static_cast<uint8_t>((first >> 3) & 0x0F);
                bool    ar = (first & 0x04) != 0;

                AncAfd out(code, ar);

                // Bar-data only when the packet actually carries the
                // full 8-UDW payload.  ST 2016-3 mandates DC=8, but a
                // truncated source still surfaces the AFD code through
                // the partial parse above.
                if (udw.size() >= kAfdUdwCount) {
                        uint8_t barFlags = static_cast<uint8_t>(udw[3] & 0xF0);
                        uint16_t v1 = static_cast<uint16_t>(((udw[4] & 0xFFu) << 8) |
                                                             (udw[5] & 0xFFu));
                        uint16_t v2 = static_cast<uint16_t>(((udw[6] & 0xFFu) << 8) |
                                                             (udw[7] & 0xFFu));
                        out.setBarFlags(barFlags);
                        out.setBarValue1(v1);
                        out.setBarValue2(v2);
                }

                return makeResult<Variant>(Variant(out));
        }

        AncTranslator::PacketsResult buildAfdSt291(const Variant &v, const AncTranslateConfig &cfg) {
                AncAfd afd = variantToAncAfd(v);

                List<uint16_t> udw;
                udw.resize(kAfdUdwCount);
                udw[0] = makeUdw1(afd.afdCode(), afd.arFlag());
                udw[1] = 0;
                udw[2] = 0;
                udw[3] = static_cast<uint16_t>(afd.barFlags() & 0xF0);
                udw[4] = static_cast<uint16_t>((afd.barValue1() >> 8) & 0xFF);
                udw[5] = static_cast<uint16_t>(afd.barValue1() & 0xFF);
                udw[6] = static_cast<uint16_t>((afd.barValue2() >> 8) & 0xFF);
                udw[7] = static_cast<uint16_t>(afd.barValue2() & 0xFF);

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                St291Packet     p = St291Packet::build(AncFormat(AncFormat::Afd), udw, line,
                                                        St291Packet::UnspecifiedHOffset, fieldB, cBit);
                AncPacket::List out;
                out.pushToBack(p.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        // AFD is sticky / idempotent — the AR + AFD code describe the
        // current frame's framing intent and don't carry any per-frame
        // sequence state.  Repeating an AFD packet on a held output
        // frame is correct (downstream consumer keeps the same framing
        // decision); dropping it is also fine (the next surviving AFD
        // packet re-establishes intent).
        AncTranslator::PacketsResult syncPolicyAfd(const AncPacket &pkt, FrameSyncDisposition d,
                                               uint8_t /*repeatIndex*/, const AncTranslateConfig & /*cfg*/) {
                AncPacket::List out;
                if (d.kind() != FrameSyncDisposition::Drop) {
                        out.pushToBack(pkt);
                }
                return makeResult<AncPacket::List>(std::move(out));
        }

        // -- Detailer ---------------------------------------------------------

        // Maps the 4-bit AFD code to its ST 2016-1 active-image description
        // (the descriptions assume a 16:9 coded frame — the common case;
        // the coded-frame aspect itself is reported separately from the AR
        // flag).  Reserved codes surface as raw hex.
        String afdCodeName(uint8_t code) {
                switch (code & 0x0F) {
                        case 0x00: return String("Undefined");
                        case 0x02: return String("Box 16:9 (top)");
                        case 0x03: return String("Box 14:9 (top)");
                        case 0x04: return String("Box > 16:9 (center)");
                        case 0x08: return String("Full frame (same as coded frame)");
                        case 0x09: return String("4:3 (center)");
                        case 0x0A: return String("16:9 (center)");
                        case 0x0B: return String("14:9 (center)");
                        case 0x0D: return String("4:3 (with alternative 14:9 center)");
                        case 0x0E: return String("16:9 (with alternative 14:9 center)");
                        case 0x0F: return String("16:9 (with alternative 4:3 center)");
                        default:   return String::sprintf("Reserved (0x%X)", code & 0x0F);
                }
        }

        // Full human-readable analysis of an ST 2016-3 AFD packet.  Always
        // returns a populated AncDetails — a packet that cannot be decoded
        // still surfaces its framing fields plus an Error issue.
        AncDetails detailAfdSt291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                AncDetails d;

                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) {
                        d.addError(String("ST 291 framing decode failed: ") + rp.second().desc());
                        return d;
                }
                const St291Packet &p = rp.first();
                d.addField("DID", String::sprintf("0x%02X", p.did()));
                d.addField("SDID", String::sprintf("0x%02X", p.sdid()));
                d.addField("DataCount", String::number(p.dataCount()));
                d.addField("Line", String::number(p.line()));
                if (!p.checksumValid()) {
                        d.addWarning(String::sprintf(
                                "Stored checksum 0x%03X does not match computed 0x%03X",
                                p.checksum(), p.computedChecksum()));
                }
                if (p.udw().size() != kAfdUdwCount) {
                        d.addWarning(String::sprintf(
                                "ST 2016-3 §4 mandates DC=8; got %zu UDWs — bar data omitted on "
                                "short packets", p.udw().size()));
                }

                AncTranslator::ParseResult parsed = parseAfdSt291(pkt, cfg);
                if (parsed.second().isError()) {
                        d.addError(String("AFD decode failed: ") + parsed.second().desc());
                        return d;
                }
                AncAfd afd = parsed.first().get<AncAfd>();

                d.addField("AfdCode", String::sprintf("0x%X (%s)", afd.afdCode(),
                                                      afdCodeName(afd.afdCode()).cstr()));
                d.addField("CodedFrameAspect", afd.arFlag() ? String("16:9") : String("4:3"));
                d.addField("BarDataPresent", String::number(afd.hasBarData()));
                if (afd.hasBarData()) {
                        StringList edges;
                        if (afd.topBar())    edges.pushToBack(String("Top"));
                        if (afd.bottomBar()) edges.pushToBack(String("Bottom"));
                        if (afd.leftBar())   edges.pushToBack(String("Left"));
                        if (afd.rightBar())  edges.pushToBack(String("Right"));
                        d.addField("BarEdges", edges.join(", "));
                        d.addField("BarValue1", String::number(afd.barValue1()));
                        d.addField("BarValue2", String::number(afd.barValue2()));
                }

                return d;
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Afd_St291, Afd, ::promeki::AncTransport::St291, ::promeki::parseAfdSt291)
PROMEKI_REGISTER_ANC_BUILDER(Afd_St291, Afd, ::promeki::AncTransport::St291, ::promeki::buildAfdSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Afd, Afd, ::promeki::syncPolicyAfd)
PROMEKI_REGISTER_ANC_DETAILER(Afd_St291, Afd, ::promeki::AncTransport::St291, ::promeki::detailAfdSt291)
