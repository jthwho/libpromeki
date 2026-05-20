/**
 * @file      anccodec_st2020audio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 2020-1:2014 / ST 2020-2:2014 Method A Dolby audio metadata
 * codec for @c AncTransport::St291.  ST 2020-2 maps the asynchronous
 * serial metadata frame described in ST 2020-1 §5 into VANC packets
 * at DID=0x45 with a wildcard SDID range (0x01..0x09 per ST 2020-1
 * §7.1 Table 1, selecting the associated audio channel pair).  See
 * @ref AncSt2020Audio for the value-type field layout.
 *
 * Method A wire layout (ST 2020-2 §5):
 *
 *  | UDW byte | Field                                                |
 *  |----------|------------------------------------------------------|
 *  | 0        | Payload Descriptor (§5.4 Table 1)                    |
 *  | 1..DC-1  | Metadata Frame bytes (up to 254 per packet)          |
 *
 * Payload Descriptor bit layout (§5.4 Table 1):
 *  - bit 7  : COMPATIBILITY = 0
 *  - bit 6  : Reserved
 *  - bit 5  : Reserved
 *  - bits 4..3 : VERSION = 01b (current syntax)
 *  - bit 2  : DOUBLE_PKT — set when MDF > 254 bytes splits across two
 *             consecutive VANC packets (§5.4.3)
 *  - bit 1  : SECOND_PKT — set on the second packet of a pair
 *  - bit 0  : DUPLICATE_PKT — set when this packet duplicates the
 *             previous frame's metadata (§5.4.4)
 *
 * Metadata frames longer than 254 bytes are emitted across two
 * consecutive packets sharing the same SDID; the framework's
 * multi-packet parse path reassembles them.
 *
 * Per ST 2020-1 §8 ("When the ANC packets are carried in a high
 * definition signal they shall be carried in the Y stream") the codec
 * emits with @c cBit=false.
 *
 * ST 2110-40 forbids ST 2020 audio packets over the smpte291 RTP
 * carriage (§5.2.1) — that filter lives in @ref RtpPayloadAnc, not in
 * this codec.  Here we just convert between the typed Variant and the
 * underlying ST 291 packets.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancst2020audio.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/datatype.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -- Payload descriptor helpers ---------------------------------

        inline uint8_t makePayloadDescriptor(bool doublePkt, bool secondPkt, bool duplicate) {
                uint8_t b = AncSt2020Audio::PayloadDescriptorVersionV1;
                if (doublePkt) b |= AncSt2020Audio::PayloadDescriptorDoubleBit;
                if (secondPkt) b |= AncSt2020Audio::PayloadDescriptorSecondBit;
                if (duplicate) b |= AncSt2020Audio::PayloadDescriptorDuplicateBit;
                return b;
        }

        // Returns true when the descriptor byte's version field matches
        // the current ST 2020-2 syntax (bits 4..3 == 01b).  Receivers
        // are warned via promekiWarn but the library accepts other
        // versions because §5.4.2 Table 2 notes legacy implementations
        // exist that used different version codes.
        inline bool descriptorVersionIsV1(uint8_t b) {
                return (b & AncSt2020Audio::PayloadDescriptorVersionMask) ==
                       AncSt2020Audio::PayloadDescriptorVersionV1;
        }

        // -- Build path -------------------------------------------------

        AncTranslator::PacketsResult buildSt2020AudioSt291(const Variant &v, const AncTranslateConfig &cfg) {
                if (v.type() != DataTypeSt2020Audio) {
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }
                AncSt2020Audio meta = v.get<AncSt2020Audio>();

                const Buffer &mdf = meta.metadataFrame();
                const size_t  mdfSize = mdf.size();
                if (mdfSize > AncSt2020Audio::MaxMetadataFrameBytes) {
                        // §5.3 caps the metadata frame at 2 × 254 bytes
                        // across two packets.  Refuse longer frames at
                        // this layer so the wire never silently drops
                        // the tail.
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }
                if (meta.channelPair() == 0) {
                        // §7.1 Table 1: SDID 0x00 is reserved (collides
                        // with RFC 8331 §3.1's Type-1 sentinel).  Refuse
                        // to emit a packet that would be unidentifiable
                        // on the wire.
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                AncPacket::List out;

                const bool needTwoPackets = mdfSize > AncSt2020Audio::MaxSinglePacketBytes;
                if (!needTwoPackets) {
                        // Single-packet emit.
                        List<uint16_t> udw;
                        udw.resize(mdfSize + 1);
                        udw[0] = makePayloadDescriptor(/*doublePkt=*/false, /*secondPkt=*/false,
                                                       meta.duplicate());
                        const uint8_t *src = static_cast<const uint8_t *>(mdf.data());
                        for (size_t i = 0; i < mdfSize; ++i) {
                                udw[1 + i] = static_cast<uint16_t>(src[i]);
                        }
                        St291Packet pkt = St291Packet::buildRaw(0x45, meta.channelPair(), udw, line,
                                                                 St291Packet::UnspecifiedHOffset, fieldB, cBit);
                        out.pushToBack(pkt.packet());
                        return makeResult<AncPacket::List>(std::move(out));
                }

                // Two-packet emit (§5.3): split at MaxSinglePacketBytes so
                // pkt 1 carries 254 bytes and pkt 2 carries the rest.
                const size_t  firstHalf = AncSt2020Audio::MaxSinglePacketBytes;
                const size_t  secondHalf = mdfSize - firstHalf;
                const uint8_t sdid = meta.channelPair();
                const uint8_t *src = static_cast<const uint8_t *>(mdf.data());

                List<uint16_t> udw1;
                udw1.resize(firstHalf + 1);
                udw1[0] = makePayloadDescriptor(/*doublePkt=*/true, /*secondPkt=*/false,
                                                meta.duplicate());
                for (size_t i = 0; i < firstHalf; ++i) udw1[1 + i] = static_cast<uint16_t>(src[i]);

                List<uint16_t> udw2;
                udw2.resize(secondHalf + 1);
                udw2[0] = makePayloadDescriptor(/*doublePkt=*/true, /*secondPkt=*/true,
                                                meta.duplicate());
                for (size_t i = 0; i < secondHalf; ++i) {
                        udw2[1 + i] = static_cast<uint16_t>(src[firstHalf + i]);
                }

                St291Packet pkt1 = St291Packet::buildRaw(0x45, sdid, udw1, line,
                                                         St291Packet::UnspecifiedHOffset, fieldB, cBit);
                St291Packet pkt2 = St291Packet::buildRaw(0x45, sdid, udw2, line,
                                                         St291Packet::UnspecifiedHOffset, fieldB, cBit);
                out.pushToBack(pkt1.packet());
                out.pushToBack(pkt2.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        // -- Single-packet parse (single-packet metadata only) ----------

        AncTranslator::ParseResult parseSt2020AudioSt291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
                if (rp.second().isError()) return makeError<Variant>(rp.second());

                const St291Packet &p = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < 1) return makeError<Variant>(Error::CorruptData);

                uint8_t desc = static_cast<uint8_t>(udw[0] & 0xFF);

                // ST 2020-1 Annex C (normative): Method A and Method B
                // are distinguished by the first UDW byte.  Method A
                // starts with a Payload Descriptor whose bit 7 = 0;
                // Method B carries a raw asynchronous-serial bitstream
                // whose first byte is either 0x00 (pre-pad) or 0xCF
                // (sync segment ID CF FCh from ST 2020-1 §5.1).  This
                // codec is scoped to Method A — refuse Method B
                // explicitly so we don't silently misparse a CF byte
                // as a Payload Descriptor with COMPATIBILITY=1.
                if (desc == 0x00 || desc == 0xCF) {
                        promekiWarn("AncSt2020Audio parse: first UDW byte 0x%02X matches "
                                    "ST 2020-1 Annex C Method B; this codec is scoped to "
                                    "Method A — refusing",
                                    static_cast<unsigned>(desc));
                        return makeError<Variant>(Error::NotSupported);
                }
                // ST 2020-2 §5.4.1: "Bit 7 of the Payload Descriptor
                // byte shall be set to logical zero."  A packet with
                // bit 7 = 1 is malformed (and would also collide with
                // the Method B 0xCF discriminator above when paired
                // with version 0b01).
                if ((desc & AncSt2020Audio::PayloadDescriptorCompatibilityBit) != 0) {
                        promekiWarn("AncSt2020Audio parse: Payload Descriptor bit 7 "
                                    "(COMPATIBILITY) is set; ST 2020-2 §5.4.1 requires it to be 0");
                        return makeError<Variant>(Error::CorruptData);
                }
                if (!descriptorVersionIsV1(desc)) {
                        // Legacy / future syntax — accept but warn so
                        // downstream consumers can decide whether to
                        // pass the bytes through.  §5.4.2 explicitly
                        // notes legacy implementations exist with other
                        // version codes; we don't bail.
                        promekiWarn("AncSt2020Audio parse: descriptor version 0x%02X != 0x01 (legacy / "
                                    "future syntax)",
                                    static_cast<unsigned>(desc &
                                                          AncSt2020Audio::PayloadDescriptorVersionMask));
                }
                const bool doublePkt = (desc & AncSt2020Audio::PayloadDescriptorDoubleBit) != 0;
                const bool secondPkt = (desc & AncSt2020Audio::PayloadDescriptorSecondBit) != 0;
                const bool duplicate = (desc & AncSt2020Audio::PayloadDescriptorDuplicateBit) != 0;
                if (doublePkt || secondPkt) {
                        // This is half of a multi-packet message — the
                        // multi-parser is the right entry point for that
                        // case.  Surface a specific error so the
                        // dispatcher can route the packet through
                        // parseGroup instead.
                        return makeError<Variant>(Error::InsufficientContext);
                }

                AncSt2020Audio out;
                out.setChannelPair(p.sdid());
                out.setDuplicate(duplicate);
                const size_t mdfSize = udw.size() - 1;
                if (mdfSize > 0) {
                        Buffer mdf(mdfSize);
                        mdf.setSize(mdfSize);
                        uint8_t *dst = static_cast<uint8_t *>(mdf.data());
                        for (size_t i = 0; i < mdfSize; ++i) {
                                dst[i] = static_cast<uint8_t>(udw[1 + i] & 0xFF);
                        }
                        out.setMetadataFrame(std::move(mdf));
                }
                return makeResult<Variant>(Variant(out));
        }

        // -- Multi-packet parse (reassembles a split metadata frame) ----

        AncTranslator::ParseResult parseSt2020AudioSt291Multi(const AncPacket::List &pkts,
                                                    const AncTranslateConfig &cfg) {
                if (pkts.size() == 0) return makeError<Variant>(Error::InvalidArgument);
                if (pkts.size() == 1) return parseSt2020AudioSt291(pkts[0], cfg);
                if (pkts.size() > 2) {
                        // §5.3 only defines split across two packets.
                        // Refuse three-or-more rather than concatenate
                        // unspecified bytes.
                        return makeError<Variant>(Error::CorruptData);
                }

                // Both packets must agree on SDID (same channel pair),
                // and the descriptor bits must indicate a proper
                // (DOUBLE=1, SECOND=0) → (DOUBLE=1, SECOND=1) pair.
                AncChecksumPolicy policy = cfg.checksumPolicy();
                Result<St291Packet> r1 = St291Packet::from(pkts[0], policy);
                Result<St291Packet> r2 = St291Packet::from(pkts[1], policy);
                if (r1.second().isError()) return makeError<Variant>(r1.second());
                if (r2.second().isError()) return makeError<Variant>(r2.second());

                const St291Packet &p1 = r1.first();
                const St291Packet &p2 = r2.first();
                if (p1.sdid() != p2.sdid()) {
                        promekiWarn("AncSt2020Audio multi-parse: SDID mismatch 0x%02X / 0x%02X",
                                    static_cast<unsigned>(p1.sdid()), static_cast<unsigned>(p2.sdid()));
                        return makeError<Variant>(Error::CorruptData);
                }

                List<uint16_t> udw1 = p1.udw();
                List<uint16_t> udw2 = p2.udw();
                if (udw1.size() < 1 || udw2.size() < 1) return makeError<Variant>(Error::CorruptData);

                uint8_t desc1 = static_cast<uint8_t>(udw1[0] & 0xFF);
                uint8_t desc2 = static_cast<uint8_t>(udw2[0] & 0xFF);
                const bool d1Double = (desc1 & AncSt2020Audio::PayloadDescriptorDoubleBit) != 0;
                const bool d1Second = (desc1 & AncSt2020Audio::PayloadDescriptorSecondBit) != 0;
                const bool d2Double = (desc2 & AncSt2020Audio::PayloadDescriptorDoubleBit) != 0;
                const bool d2Second = (desc2 & AncSt2020Audio::PayloadDescriptorSecondBit) != 0;
                if (!d1Double || !d2Double || d1Second || !d2Second) {
                        // §5.4.3: first packet must have (Double=1, Second=0);
                        // second packet must have (Double=1, Second=1).
                        return makeError<Variant>(Error::CorruptData);
                }

                const bool duplicate = (desc1 & AncSt2020Audio::PayloadDescriptorDuplicateBit) != 0;
                const size_t mdfSize = (udw1.size() - 1) + (udw2.size() - 1);
                Buffer       mdf(mdfSize);
                mdf.setSize(mdfSize);
                uint8_t *dst = static_cast<uint8_t *>(mdf.data());
                size_t off = 0;
                for (size_t i = 1; i < udw1.size(); ++i) {
                        dst[off++] = static_cast<uint8_t>(udw1[i] & 0xFF);
                }
                for (size_t i = 1; i < udw2.size(); ++i) {
                        dst[off++] = static_cast<uint8_t>(udw2[i] & 0xFF);
                }

                AncSt2020Audio out;
                out.setChannelPair(p1.sdid());
                out.setDuplicate(duplicate);
                out.setMetadataFrame(std::move(mdf));
                return makeResult<Variant>(Variant(out));
        }

        // Helper: rewrite an ST 2020 packet's Payload Descriptor byte
        // (UDW[0]) so the DUPLICATE_PKT bit is set, leaving every other
        // byte unchanged.  Used by the Repeat[idx>0] sync path so
        // re-emitted packets correctly signal §5.4.4 duplicate
        // semantics ("the second physical frame of a 50/60 Hz pair").
        AncPacket markPacketAsDuplicate(const AncPacket &src) {
                Result<St291Packet> rp = St291Packet::from(src);
                if (rp.second().isError()) return src;
                const St291Packet &p = rp.first();
                List<uint16_t> udw = p.udw();
                if (udw.isEmpty()) return src;
                udw[0] = static_cast<uint16_t>(udw[0] | AncSt2020Audio::PayloadDescriptorDuplicateBit);
                St291Packet rebuilt = St291Packet::buildRaw(
                        p.did(), p.sdid(), udw, p.line(), p.hOffset(),
                        p.fieldB(), p.cBit(), p.streamNum());
                if (!rebuilt.isValid()) return src;
                return rebuilt.packet();
        }

        // -- Frame-sync policy ------------------------------------------

        // ST 2020 audio metadata describes a specific frame's audio
        // payload (Dolby E / DD+ / etc.).  Repeating the same packet on
        // a held output frame is permitted by §5.4.4, which says
        // "DUPLICATE_PKT bit shall be set to 1 if this packet contains
        // the same payload data as the previous packet for this frame
        // pair" — i.e. the second physical frame of a 50/60 Hz pair.
        // On Repeat[idx>0] we set the bit so a downstream receiver can
        // de-duplicate; Repeat[idx==0] and Play pass through verbatim
        // (the held packet's bit is whatever the source already set).
        // Dropping on FrameSyncDisposition::Drop discards the metadata
        // for that frame; the audio still rides through but downstream
        // Dolby decoders fall back to default metadata for that segment,
        // which is acceptable.
        AncTranslator::PacketsResult syncPolicySt2020Audio(const AncPacket &pkt, FrameSyncDisposition d,
                                                       uint8_t repeatIndex,
                                                       const AncTranslateConfig & /*cfg*/) {
                AncPacket::List out;
                if (d.kind() == FrameSyncDisposition::Drop) {
                        return makeResult<AncPacket::List>(std::move(out));
                }
                if (d.kind() == FrameSyncDisposition::Repeat && repeatIndex > 0) {
                        out.pushToBack(markPacketAsDuplicate(pkt));
                } else {
                        out.pushToBack(pkt);
                }
                return makeResult<AncPacket::List>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Smpte2020Audio_St291, Smpte2020Audio, ::promeki::AncTransport::St291,
                             ::promeki::parseSt2020AudioSt291)
PROMEKI_REGISTER_ANC_MULTI_PARSER(Smpte2020Audio_St291, Smpte2020Audio,
                                   ::promeki::AncTransport::St291,
                                   ::promeki::parseSt2020AudioSt291Multi)
PROMEKI_REGISTER_ANC_BUILDER(Smpte2020Audio_St291, Smpte2020Audio, ::promeki::AncTransport::St291,
                              ::promeki::buildSt2020AudioSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Smpte2020Audio, Smpte2020Audio, ::promeki::syncPolicySt2020Audio)
