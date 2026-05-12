/**
 * @file      anccodec_cea708.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE 334-2 CDP parser and builder for @c AncTransport::St291.
 * Encodes / decodes @ref Cea708Cdp values to / from the ST 291 wire
 * form (DID 0x61 / SDID 0x01).  Each CDP byte becomes one user-data
 * word; @ref Cea708Cdp::toBuffer / @ref Cea708Cdp::fromBuffer own the
 * structural layout, the checksum, and the footer-sequence mirror.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/cea708cdp.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        Result<Variant> parseCea708St291(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<Variant>(rp.second());
                const St291Packet &p = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < 11) {
                        // Minimum CDP is 7-byte header + 4-byte footer.
                        return makeError<Variant>(Error::CorruptData);
                }
                // Each UDW carries one data byte in its low 8 bits.
                List<uint8_t> bytes;
                bytes.resize(udw.size());
                for (size_t i = 0; i < udw.size(); ++i) {
                        bytes[i] = static_cast<uint8_t>(udw[i] & 0xFF);
                }
                Result<Cea708Cdp> rc = Cea708Cdp::fromBuffer(bytes.data(), bytes.size());
                if (rc.second().isError()) return makeError<Variant>(rc.second());
                return makeResult<Variant>(Variant(rc.first()));
        }

        Result<AncPacket> buildCea708St291(const Variant &v, const AncTranslateConfig &cfg) {
                Cea708Cdp cdp = v.get<Cea708Cdp>();
                Buffer    wire = cdp.toBuffer();
                const size_t sz = wire.size();
                if (sz == 0) return makeError<AncPacket>(Error::CorruptData);

                // ST 291 DataCount is one byte — cap CDP size at 255.  The
                // standard allows much larger CDPs split across multiple
                // ANC packets; that lands as a follow-up when a real
                // source produces over-255-byte CDPs.  Until then, signal
                // the error explicitly rather than silently truncating.
                if (sz > 255) return makeError<AncPacket>(Error::OutOfRange);

                List<uint16_t> udw;
                udw.resize(sz);
                const uint8_t *src = static_cast<const uint8_t *>(wire.data());
                for (size_t i = 0; i < sz; ++i) udw[i] = src[i];

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine, uint16_t(0));
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);

                St291Packet p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, line,
                                                    St291Packet::UnspecifiedHOffset, fieldB);
                return makeResult<AncPacket>(p.packet());
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Cea708_St291, Cea708, ::promeki::AncTransport::St291,
                             ::promeki::parseCea708St291)
PROMEKI_REGISTER_ANC_BUILDER(Cea708_St291, Cea708, ::promeki::AncTransport::St291,
                              ::promeki::buildCea708St291)
