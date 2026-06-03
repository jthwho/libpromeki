/**
 * @file      anccodec_vpid.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 352 Video Payload Identifier (VPID) parser and builder for
 * @c AncTransport::St291.  VPID is a fixed 4-byte ANC payload at
 * DID @c 0x41 / SDID @c 0x01 (Type-2 packet, DC=4) carrying the
 * raster, scan, transport, and colourimetry of the underlying SDI
 * link.  The full byte layout lives on the @ref SdiVpid value type
 * (see @c sdivpid.h); this codec is a thin wrapper that bridges the
 * value type into the @ref AncTranslator dispatch framework.
 *
 * Round-trip contract: a @c Variant<SdiVpid> goes out as one
 * @c AncPacket and parses back into an identical @c SdiVpid.  The
 * codec delegates wire-level work to
 * @ref SdiVpid::toSt291Packet and @ref SdiVpid::fromSt291Packet so
 * the byte order, checksum, and DID/SDID enforcement are shared
 * with direct callers of those helpers.
 *
 * @par Line / field threading
 *
 * The build path honours @c AncTranslateConfig::St291BuildLine and
 * @c AncTranslateConfig::St291FieldB.  ST 352:2013 §6.2 gives a
 * recommended VANC line per raster / scan; callers wanting that
 * placement should query @ref SdiVpid::recommendedAncLine for the
 * surrounding @ref VideoFormat and stamp the result into the cfg
 * before calling @ref AncTranslator::build.  When no line is
 * supplied the codec falls back to @c St291Packet::UnspecifiedLine
 * (0x7FE), matching every other built-in codec.
 *
 * @par Frame-sync policy
 *
 * VPID is a steady-state description of the SDI link; the bytes are
 * the same on every frame until the link reconfigures.  No
 * sequence counter or per-frame state lives inside the packet.
 * Repeating a VPID under @c FrameSyncDisposition::Repeat is the
 * correct way to hold a stalled output frame (downstream consumers
 * keep the same payload-id assumption); dropping it under
 * @c FrameSyncDisposition::Drop is also fine (the next surviving
 * VPID re-establishes the assumption).  Same shape as the AFD
 * sync policy.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/datatype.h>
#include <promeki/enums_anc.h>
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/sdivpid.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        AncTranslator::ParseResult parseVpidSt291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
                if (rp.second().isError()) return makeError<Variant>(rp.second());

                Result<SdiVpid> rv = SdiVpid::fromSt291Packet(rp.first());
                if (rv.second().isError()) return makeError<Variant>(rv.second());

                return makeResult<Variant>(Variant(rv.first()));
        }

        AncTranslator::PacketsResult buildVpidSt291(const Variant &v, const AncTranslateConfig &cfg) {
                if (v.type() != DataTypeSdiVpid) {
                        return makeError<AncPacket::List>(Error::InvalidArgument);
                }
                SdiVpid vpid = v.get<SdiVpid>();

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                St291Packet     p = vpid.toSt291Packet(line, fieldB, cBit);
                AncPacket::List out;
                out.pushToBack(p.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        AncTranslator::PacketsResult syncPolicyVpid(const AncPacket &pkt, FrameSyncDisposition d,
                                                uint8_t /*repeatIndex*/, const AncTranslateConfig & /*cfg*/) {
                AncPacket::List out;
                if (d.kind() != FrameSyncDisposition::Drop) {
                        out.pushToBack(pkt);
                }
                return makeResult<AncPacket::List>(std::move(out));
        }

        // -- Detailer ---------------------------------------------------------

        // Names the byte 3 [3:0] sampling-structure code (ST 352:2013
        // Table 3).  The well-known sampling combinations are spelled out;
        // reserved / unmodelled codes surface as raw hex so a future
        // assignment still shows up in diagnostics.
        String vpidSamplingName(uint8_t code) {
                switch (code) {
                        case SdiVpid::Sampling_YCbCr_422:   return String("Y'CbCr 4:2:2");
                        case SdiVpid::Sampling_YCbCr_444:   return String("Y'CbCr 4:4:4");
                        case SdiVpid::Sampling_RGB_444:     return String("R'G'B' 4:4:4");
                        case SdiVpid::Sampling_YCbCr_420:   return String("Y'CbCr 4:2:0");
                        case SdiVpid::Sampling_YCbCrA_4224: return String("Y'CbCr+A 4:2:2:4");
                        case SdiVpid::Sampling_YCbCrA_4444: return String("Y'CbCr+A 4:4:4:4");
                        case SdiVpid::Sampling_RGBA_4444:   return String("R'G'B'A 4:4:4:4");
                        default:                            return String::sprintf("Reserved (0x%X)",
                                                                                   code & 0x0F);
                }
        }

        // Full human-readable analysis of an ST 352 VPID packet.  Always
        // returns a populated AncDetails — a packet that cannot be decoded
        // still surfaces its framing fields plus an Error issue, and an
        // unrecognised byte 1 code surfaces a Warning while still rendering
        // every (best-effort) decoded field.
        AncDetails detailVpidSt291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
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
                if (p.dataCount() != 4) {
                        d.addWarning(String::sprintf(
                                "ST 352 mandates DC=4; got DC=%u", p.dataCount()));
                }

                AncTranslator::ParseResult parsed = parseVpidSt291(pkt, cfg);
                if (parsed.second().isError()) {
                        d.addError(String("VPID decode failed: ") + parsed.second().desc());
                        return d;
                }
                SdiVpid vpid = parsed.first().get<SdiVpid>();

                d.addField("Bytes", vpid.toString());
                d.addField("Version", vpid.isCurrentVersion() ? String("ST 352:2013 (current)")
                                                              : String("Pre-2008 (Annex C)"));
                if (!vpid.isValid()) {
                        d.addWarning(String::sprintf(
                                "Byte 1 code 0x%02X is not a recognised SDI payload identifier; "
                                "decoded fields below are best-effort", vpid.byte1()));
                }

                d.addField("LinkStandard", vpid.linkStandard().valueName());
                FrameRate rate = vpid.pictureRate();
                d.addField("PictureRate", rate.isValid()
                                                  ? rate.toString()
                                                  : String::sprintf("Unknown (code 0x%X)",
                                                                    vpid.pictureRateCode()));
                d.addField("ScanMode", vpid.videoScanMode().valueName());
                d.addField("AspectRatio", vpid.is16x9() ? String("16:9") : String("4:3"));
                d.addField("Sampling", vpidSamplingName(vpid.samplingCode()));
                int bd = vpid.bitDepth();
                d.addField("BitDepth", bd > 0 ? String::sprintf("%d-bit", bd) : String("Reserved"));
                d.addField("WireFormat", vpid.wireFormat().valueName());
                // channelAssignment() is 0-based on the wire (0 = single-link
                // / channel 1); present it 1-based as humans count channels.
                d.addField("Channel", String::number(vpid.channelAssignment() + 1));

                // The 6G/12G payloads repurpose the ST 352:2013 reserved bits
                // for HDR / WCG signalling; only surface those fields when the
                // byte 1 code actually names an extended-schema payload.
                if (vpid.isExtendedSchema()) {
                        d.addField("Transfer", vpid.transferCharacteristic().valueName());
                        d.addField("Colorimetry", vpid.colorimetry().valueName());
                        d.addField("SignalType", vpid.isIctcp() ? String("ICtCp") : String("Y'CbCr"));
                        d.addField("FullRange", String::number(vpid.isFullRange()));
                }

                if (Error ve = vpid.validate(); ve.isError()) {
                        d.addWarning(String("VPID self-validation reports a reserved / out-of-range "
                                            "field: ") + ve.desc());
                }

                return d;
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Vpid_St291, Vpid, ::promeki::AncTransport::St291, ::promeki::parseVpidSt291)
PROMEKI_REGISTER_ANC_BUILDER(Vpid_St291, Vpid, ::promeki::AncTransport::St291, ::promeki::buildVpidSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Vpid, Vpid, ::promeki::syncPolicyVpid)
PROMEKI_REGISTER_ANC_DETAILER(Vpid_St291, Vpid, ::promeki::AncTransport::St291,
                              ::promeki::detailVpidSt291)
