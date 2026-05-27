/**
 * @file      anccodec_cea708.cpp
 * @copyright Jason Howard. All rights reserved.
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

        AncTranslator::ParseResult parseCea708St291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
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

        AncTranslator::PacketsResult buildCea708St291(const Variant &v, const AncTranslateConfig &cfg) {
                Cea708Cdp cdp = v.get<Cea708Cdp>();
                Buffer    wire = cdp.toBuffer();
                const size_t sz = wire.size();
                if (sz == 0) return makeError<AncPacket::List>(Error::CorruptData);

                // ST 291 DataCount is one byte — cap CDP size at 255.  The
                // standard allows much larger CDPs split across multiple
                // ANC packets; that lands as a follow-up when a real
                // source produces over-255-byte CDPs.  Until then, signal
                // the error explicitly rather than silently truncating.
                if (sz > 255) return makeError<AncPacket::List>(Error::OutOfRange);

                List<uint16_t> udw;
                udw.resize(sz);
                const uint8_t *src = static_cast<const uint8_t *>(wire.data());
                for (size_t i = 0; i < sz; ++i) udw[i] = src[i];

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                St291Packet     p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, line,
                                                        St291Packet::UnspecifiedHOffset, fieldB, cBit);
                AncPacket::List out;
                out.pushToBack(p.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        // Sync policy: CEA-708 caption data is per-frame and stateful — a
        // CDP carries DTVCC service blocks whose decoder commands
        // (DefineWindow, SetCurrentWindow, SetPenAttributes, …) must not
        // re-fire when the same picture is held over multiple output
        // slots.  But the receiver also expects a CDP every frame at the
        // session's cdp_frame_rate, so we can't just stop emitting CDPs
        // either.
        //
        // The policy: on Repeat[idx>0] emit a *framing-only* CDP that
        // preserves the CDP envelope (same cc_count, same header / footer
        // shape, sequence_counter advanced by repeatIndex per SMPTE 334-2
        // §5.1.5) but invalidates every cc_data triple
        // (cc_valid = false).  Receivers see a normal CDP cadence but
        // ingest no caption commands from the held frames.  On Drop the
        // CDP is lost — the caption decoder may glitch one frame; the
        // next surviving CDP re-establishes state.  On Play and
        // Repeat[idx=0] the original packet copies through verbatim.
        AncTranslator::PacketsResult syncPolicyCea708(const AncPacket &pkt, FrameSyncDisposition d,
                                                  uint8_t repeatIndex, const AncTranslateConfig &cfg) {
                AncPacket::List out;
                if (d.kind() == FrameSyncDisposition::Drop) {
                        return makeResult<AncPacket::List>(std::move(out));
                }
                if (d.kind() == FrameSyncDisposition::Play || repeatIndex == 0) {
                        out.pushToBack(pkt);
                        return makeResult<AncPacket::List>(std::move(out));
                }

                // Repeat[idx>0]: decode, advance sequence_counter, blank
                // cc_data triples, re-encode.
                AncTranslator::ParseResult parsed = parseCea708St291(pkt, cfg);
                if (parsed.second().isError()) return makeError<AncPacket::List>(parsed.second());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();

                // Wrap is fine — sequence_counter is u16 in the wire and
                // the spec expects it to roll over.
                cdp.sequenceCounter = static_cast<uint16_t>(cdp.sequenceCounter + repeatIndex);
                for (Cea708Cdp::CcData &cc : cdp.ccData) {
                        cc.valid = false;
                        cc.type  = 0;
                        cc.b1    = 0;
                        cc.b2    = 0;
                }

                // Preserve the source packet's line / fieldB (same rationale
                // as the ATC sync policy — wire framing should match the
                // source, not whatever defaults are in the held cfg).
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<AncPacket::List>(rp.second());
                AncTranslateConfig overrideCfg = cfg;
                overrideCfg.set(AncTranslateConfig::St291BuildLine, rp.first().line());
                overrideCfg.set(AncTranslateConfig::St291FieldB, rp.first().fieldB());

                return buildCea708St291(Variant(cdp), overrideCfg);
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Cea708_St291, Cea708, ::promeki::AncTransport::St291,
                             ::promeki::parseCea708St291)
PROMEKI_REGISTER_ANC_BUILDER(Cea708_St291, Cea708, ::promeki::AncTransport::St291,
                              ::promeki::buildCea708St291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Cea708, Cea708, ::promeki::syncPolicyCea708)
