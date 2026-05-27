/**
 * @file      anccodec_op47sdp.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * RDD 8:2008 / OP-47 Subtitling Distribution Packet (SDP) parser and
 * builder for @c AncTransport::St291.  SDP packets are Type-2 ST 291
 * packets at DID=0x43, SDID=0x02 (RDD 8 §4.2 (ii) / §5.1).  Each SDP
 * carries 0..5 ITU-R BT.653 / EBU World System Teletext (WST) VBI
 * lines plus a 16-bit Footer Sequence Counter and an 8-bit SDP
 * Checksum.  See @ref AncOp47Sdp for the value-type field layout.
 *
 * Wire layout (RDD 8 §5.1) — 13 + 45*N UDW bytes total:
 *
 *  | UDW byte index             | Field                                   |
 *  |----------------------------|-----------------------------------------|
 *  | 0                          | IDENTIFIER 1 (0x51)                     |
 *  | 1                          | IDENTIFIER 2 (0x15)                     |
 *  | 2                          | LENGTH (total UDW byte count)           |
 *  | 3                          | FORMAT CODE = 0x02 (WST teletext)       |
 *  | 4..8                       | VBI Packet 1..5 Structure A descriptors |
 *  | 9..(9 + 45N - 1)           | Structure B payloads (45 bytes each)    |
 *  | 9 + 45N                    | FOOTER ID (0x74)                        |
 *  | 9 + 45N + 1, +2            | FOOTER SEQUENCE COUNTER (MSB, LSB)      |
 *  | 9 + 45N + 3                | SDP CHECKSUM                            |
 *
 * The SDP CHECKSUM byte makes the arithmetic sum from IDENTIFIER 1
 * through SDP CHECKSUM inclusive equal zero modulo 256 (RDD 8 §5.3).
 *
 * Per RDD 8 §3 ("SMPTE 344M provides that the VANC data shall be
 * carried in the Y stream") the codec emits with @c cBit=false.
 */

#include <promeki/ancformat.h>
#include <promeki/ancop47sdp.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/datatype.h>
#include <promeki/enums_anc.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Smallest legal UDW byte count: identifiers (2) + length (1) +
        // format code (1) + 5 descriptors (5) + footer id (1) + FSC (2)
        // + checksum (1) = 13.  N=0 packets is a valid empty SDP per
        // §5.4.2.
        constexpr size_t kMinSdpUdwBytes = 13;

        // Offsets into the UDW byte stream (constant for any N).
        constexpr size_t kIdentifier1Offset = 0;
        constexpr size_t kIdentifier2Offset = 1;
        constexpr size_t kLengthOffset      = 2;
        constexpr size_t kFormatCodeOffset  = 3;
        constexpr size_t kDescriptorsOffset = 4;

        // -- Structure A descriptor byte helpers -------------------------

        inline uint8_t encodeDescriptor(const AncOp47Sdp::VbiPacket &p) {
                uint8_t b = static_cast<uint8_t>(p.lineNumber & AncOp47Sdp::LineMask);
                // Reserved bits b5..b6 round-trip through the low two
                // bits of the value-type's reservedBits field.
                b = static_cast<uint8_t>(b | ((p.reservedBits & 0x03) << 5));
                if (p.fieldOne) b = static_cast<uint8_t>(b | AncOp47Sdp::FieldOneBit);
                return b;
        }

        inline void decodeDescriptor(uint8_t b, AncOp47Sdp::VbiPacket &p) {
                p.lineNumber = static_cast<uint8_t>(b & AncOp47Sdp::LineMask);
                p.reservedBits = static_cast<uint8_t>((b >> 5) & 0x03);
                p.fieldOne = (b & AncOp47Sdp::FieldOneBit) != 0;
        }

        // -- Parse path --------------------------------------------------

        AncTranslator::ParseResult parseOp47SdpSt291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
                if (rp.second().isError()) return makeError<Variant>(rp.second());

                const St291Packet &p = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < kMinSdpUdwBytes) return makeError<Variant>(Error::CorruptData);

                // RDD 8 §5.1 identifiers + format code.
                if (static_cast<uint8_t>(udw[kIdentifier1Offset] & 0xFF) !=
                            AncOp47Sdp::Identifier1 ||
                    static_cast<uint8_t>(udw[kIdentifier2Offset] & 0xFF) !=
                            AncOp47Sdp::Identifier2) {
                        return makeError<Variant>(Error::CorruptData);
                }
                if (static_cast<uint8_t>(udw[kFormatCodeOffset] & 0xFF) !=
                    AncOp47Sdp::FormatCodeWstTeletext) {
                        // The library scope is WST teletext; other
                        // FORMAT CODE values (none currently defined)
                        // surface as CorruptData rather than silently
                        // truncating their payload.
                        return makeError<Variant>(Error::CorruptData);
                }

                // RDD 8 §5.1 LENGTH — full UDW byte count (including
                // itself and the trailing SDP CHECKSUM).  When the
                // declared length disagrees with the wire DC we reject;
                // truncating to the smaller of the two would silently
                // accept malformed payloads.
                size_t declaredLength = static_cast<size_t>(udw[kLengthOffset] & 0xFF);
                if (declaredLength != udw.size()) return makeError<Variant>(Error::CorruptData);

                // RDD 8 §5.4.2: descriptors are a prefix — once a zero
                // descriptor appears, every following descriptor shall
                // also be zero.  Count the prefix of non-zero
                // descriptors to know how many Structure B payloads we
                // expect to find.
                size_t activePackets = 0;
                bool   seenZero = false;
                for (size_t i = 0; i < AncOp47Sdp::MaxVbiPackets; ++i) {
                        uint8_t d = static_cast<uint8_t>(
                                udw[kDescriptorsOffset + i] & 0xFF);
                        if (d == 0) {
                                seenZero = true;
                        } else {
                                if (seenZero) {
                                        // Non-zero descriptor following a
                                        // zero descriptor violates §5.4.2;
                                        // reject rather than silently
                                        // re-order.
                                        return makeError<Variant>(Error::CorruptData);
                                }
                                ++activePackets;
                        }
                }

                const size_t expected = kMinSdpUdwBytes + activePackets * AncOp47Sdp::WstPacketSize;
                if (udw.size() != expected) return makeError<Variant>(Error::CorruptData);

                // RDD 8 §5.3 SDP checksum — arithmetic sum of bytes 0..end
                // inclusive shall be 0 mod 256.  Validate before
                // returning the parsed value type so a captured bit
                // error doesn't ride past this codec.
                uint32_t sum = 0;
                for (size_t i = 0; i < udw.size(); ++i) {
                        sum += static_cast<uint32_t>(udw[i] & 0xFF);
                }
                if ((sum & 0xFFu) != 0) return makeError<Variant>(Error::CorruptData);

                AncOp47Sdp out;

                // Read the five descriptors and the corresponding
                // Structure B blocks.  Inactive descriptors (descriptor
                // == 0) carry no Structure B in the wire stream.
                size_t bOffset = kDescriptorsOffset + AncOp47Sdp::MaxVbiPackets;
                for (size_t i = 0; i < activePackets; ++i) {
                        AncOp47Sdp::VbiPacket vp;
                        uint8_t               desc = static_cast<uint8_t>(
                                          udw[kDescriptorsOffset + i] & 0xFF);
                        decodeDescriptor(desc, vp);
                        for (size_t b = 0; b < AncOp47Sdp::WstPacketSize; ++b) {
                                vp.wstData[b] =
                                        static_cast<uint8_t>(udw[bOffset + b] & 0xFF);
                        }
                        bOffset += AncOp47Sdp::WstPacketSize;
                        out.addPacket(vp);
                }

                // Footer ID + FSC.
                uint8_t footerId = static_cast<uint8_t>(udw[bOffset] & 0xFF);
                if (footerId != AncOp47Sdp::FooterId) return makeError<Variant>(Error::CorruptData);
                uint8_t fscHigh = static_cast<uint8_t>(udw[bOffset + 1] & 0xFF);
                uint8_t fscLow = static_cast<uint8_t>(udw[bOffset + 2] & 0xFF);
                out.setFooterSequenceCounter(
                        static_cast<uint16_t>((static_cast<uint16_t>(fscHigh) << 8) | fscLow));

                return makeResult<Variant>(Variant(out));
        }

        // -- Build path --------------------------------------------------

        AncTranslator::PacketsResult buildOp47SdpSt291(const Variant &v, const AncTranslateConfig &cfg) {
                if (v.type() != DataTypeAncOp47Sdp) {
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }
                AncOp47Sdp sdp = v.get<AncOp47Sdp>();

                if (sdp.packets().size() > AncOp47Sdp::MaxVbiPackets) {
                        // §5.1 caps the SDP at five carried packets;
                        // refuse to emit a longer list at this layer so
                        // the wire never silently drops the tail.
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }

                const size_t n = sdp.packets().size();
                const size_t total = kMinSdpUdwBytes + n * AncOp47Sdp::WstPacketSize;
                // §5.1 caps the SDP at five carried packets which lands
                // at 238 bytes — well inside the 255 UDW max — but
                // assert the bound defensively in case a future caller
                // bumps MaxVbiPackets without rechecking.
                if (total > 0xFFu) {
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }

                List<uint16_t> udw;
                udw.resize(total);

                udw[kIdentifier1Offset] = AncOp47Sdp::Identifier1;
                udw[kIdentifier2Offset] = AncOp47Sdp::Identifier2;
                udw[kLengthOffset]      = static_cast<uint16_t>(total);
                udw[kFormatCodeOffset]  = AncOp47Sdp::FormatCodeWstTeletext;

                // Structure A descriptors: first N from sdp.packets(),
                // remainder zero per §5.4.2.
                for (size_t i = 0; i < AncOp47Sdp::MaxVbiPackets; ++i) {
                        uint8_t b = (i < n) ? encodeDescriptor(sdp.packets()[i]) : uint8_t(0);
                        udw[kDescriptorsOffset + i] = b;
                }

                // Structure B payloads — 45 bytes per active packet,
                // emitted in the same order as the descriptors.
                size_t off = kDescriptorsOffset + AncOp47Sdp::MaxVbiPackets;
                for (size_t i = 0; i < n; ++i) {
                        const AncOp47Sdp::VbiPacket &p = sdp.packets()[i];
                        for (size_t b = 0; b < AncOp47Sdp::WstPacketSize; ++b) {
                                udw[off + b] = static_cast<uint16_t>(p.wstData[b]);
                        }
                        off += AncOp47Sdp::WstPacketSize;
                }

                // Footer ID + FSC (MSB first per §5.2).
                udw[off]     = AncOp47Sdp::FooterId;
                udw[off + 1] = static_cast<uint16_t>((sdp.footerSequenceCounter() >> 8) & 0xFF);
                udw[off + 2] = static_cast<uint16_t>(sdp.footerSequenceCounter() & 0xFF);

                // SDP CHECKSUM (§5.3): pick the byte that makes the
                // sum from IDENTIFIER 1 through SDP CHECKSUM
                // inclusive land on 0 mod 256.
                uint32_t sum = 0;
                for (size_t i = 0; i < total - 1; ++i) {
                        sum += static_cast<uint32_t>(udw[i] & 0xFF);
                }
                udw[total - 1] = static_cast<uint16_t>((0u - sum) & 0xFFu);

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                St291Packet     pkt = St291Packet::build(AncFormat(AncFormat::Op47Sdp), udw, line,
                                                         St291Packet::UnspecifiedHOffset, fieldB, cBit);
                AncPacket::List out;
                out.pushToBack(pkt.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        // -- Frame-sync policy ------------------------------------------

        // OP-47 SDPs carry per-frame teletext data plus a 16-bit Footer
        // Sequence Counter that increments per SDP.  Pure timing-domain
        // semantics: dropping an SDP under FrameSyncDisposition::Drop
        // skips a teletext line on the output frame (loss-acceptable;
        // teletext is best-effort); repeating would re-issue an
        // identical FSC which would confuse downstream receivers
        // tracking the counter for loss detection.  Therefore Repeat
        // also drops — only Play passes the packet through.
        AncTranslator::PacketsResult syncPolicyOp47Sdp(const AncPacket &pkt, FrameSyncDisposition d,
                                                    uint8_t /*repeatIndex*/,
                                                    const AncTranslateConfig & /*cfg*/) {
                AncPacket::List out;
                if (d.kind() == FrameSyncDisposition::Play) {
                        out.pushToBack(pkt);
                }
                return makeResult<AncPacket::List>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Op47Sdp_St291, Op47Sdp, ::promeki::AncTransport::St291,
                             ::promeki::parseOp47SdpSt291)
PROMEKI_REGISTER_ANC_BUILDER(Op47Sdp_St291, Op47Sdp, ::promeki::AncTransport::St291,
                              ::promeki::buildOp47SdpSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Op47Sdp, Op47Sdp, ::promeki::syncPolicyOp47Sdp)
