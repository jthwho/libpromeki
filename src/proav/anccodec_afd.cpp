/**
 * @file      anccodec_afd.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE 2016-3 AFD (Active Format Description) parser and builder for
 * @c AncTransport::St291.  AFD is the smallest possible ANC payload —
 * 8 UDWs:
 *
 *  | UDW | Bits 0–7                                                  |
 *  |-----|-----------------------------------------------------------|
 *  | 1   | AFD code in bits 3–6 (the only field application code cares about) |
 *  |     | AR bit in bit 2 (aspect-ratio coded format)               |
 *  | 2   | Reserved (zero)                                           |
 *  | 3–8 | Bar data (reserved zero when AFD is emitted standalone)   |
 *
 * AFD is exposed as a @c uint8_t Variant payload — the 4-bit AFD code
 * with the SMPTE 2016 "AR" bit packed into bit 7.  Higher-level
 * callers that want richer accessors can layer a typed @c Afd value
 * type on top in Phase 3; this codec is the wire seam.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr size_t kAfdUdwCount = 8;

        Result<Variant> parseAfdSt291(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<Variant>(rp.second());

                const St291Packet &p = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < 1) return makeError<Variant>(Error::CorruptData);

                // Pack AFD code (4 bits) and AR flag (1 bit) into a single
                // u8: bits 3..6 = AFD code, bit 7 = AR.  The rest of the
                // wire payload (bar data) is not surfaced here; downstream
                // typed parsers can decode the remaining UDWs when they need
                // it.
                uint8_t first = static_cast<uint8_t>(udw[0] & 0xFF);
                uint8_t afdCode = static_cast<uint8_t>((first >> 3) & 0x0F);
                uint8_t arBit = static_cast<uint8_t>((first >> 2) & 0x01);
                uint8_t out = static_cast<uint8_t>((arBit << 7) | (afdCode << 3));
                return makeResult<Variant>(Variant(out));
        }

        Result<List<AncPacket>> buildAfdSt291(const Variant &v, const AncTranslateConfig &cfg) {
                uint8_t packed = v.get<uint8_t>();
                uint8_t afdCode = static_cast<uint8_t>((packed >> 3) & 0x0F);
                uint8_t arBit = static_cast<uint8_t>((packed >> 7) & 0x01);
                uint8_t first = static_cast<uint8_t>((afdCode << 3) | (arBit << 2));

                List<uint16_t> udw;
                udw.resize(kAfdUdwCount);
                udw[0] = first;
                // UDWs 1..7 are zero (no bar data carried in this codec).

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine, uint16_t(0));
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);

                St291Packet     p = St291Packet::build(AncFormat(AncFormat::Afd), udw, line,
                                                        St291Packet::UnspecifiedHOffset, fieldB);
                List<AncPacket> out;
                out.pushToBack(p.packet());
                return makeResult<List<AncPacket>>(std::move(out));
        }

        // AFD is sticky / idempotent — the AR + AFD code describe the
        // current frame's framing intent and don't carry any per-frame
        // sequence state.  Repeating an AFD packet on a held output
        // frame is correct (downstream consumer keeps the same framing
        // decision); dropping it is also fine (the next surviving AFD
        // packet re-establishes intent).
        Result<List<AncPacket>> syncPolicyAfd(const AncPacket &pkt, FrameSyncDisposition d,
                                               uint8_t /*repeatIndex*/, const AncTranslateConfig & /*cfg*/) {
                List<AncPacket> out;
                if (d.kind() != FrameSyncDisposition::Drop) {
                        out.pushToBack(pkt);
                }
                return makeResult<List<AncPacket>>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Afd_St291, Afd, ::promeki::AncTransport::St291, ::promeki::parseAfdSt291)
PROMEKI_REGISTER_ANC_BUILDER(Afd_St291, Afd, ::promeki::AncTransport::St291, ::promeki::buildAfdSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Afd, Afd, ::promeki::syncPolicyAfd)
