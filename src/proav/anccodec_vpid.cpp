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

        // Full human-readable analysis of an ST 352 VPID packet.  Always
        // returns a populated AncDetails — a packet that cannot be decoded
        // still surfaces its framing fields plus an Error issue, and an
        // unrecognised byte 1 code surfaces a Warning while still rendering
        // every (best-effort) decoded field.  Every byte field defined by
        // ST 352:2013 (and the ST 2081-10 / 2082-10 extended-schema
        // re-use of the reserved bits) is rendered, alongside the framing,
        // the packed instrument-style word, and conformance diagnostics.
        AncDetails detailVpidSt291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                AncDetails d;

                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) {
                        d.addError(String("ST 291 framing decode failed: ") + rp.second().desc());
                        return d;
                }
                const St291Packet &p = rp.first();
                // -- ST 291-1 framing (ST 352:2013 §6.1, Table 4) -----
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
                                "ST 352 §6.1 mandates DC=4; got DC=%u", p.dataCount()));
                }

                AncTranslator::ParseResult parsed = parseVpidSt291(pkt, cfg);
                if (parsed.second().isError()) {
                        d.addError(String("VPID decode failed: ") + parsed.second().desc());
                        return d;
                }
                SdiVpid vpid = parsed.first().get<SdiVpid>();

                // -- Raw payload --------------------------------------
                d.addField("Bytes", vpid.toString());
                // The big-endian packed word is how SDI test sets /
                // monitors display a VPID (e.g. 0x89CA8001).
                d.addField("PackedWord", String::sprintf("0x%08X", vpid.toUint32BE()));

                // -- Byte 1: payload + digital interface (§5.2) -------
                d.addField("Version", vpid.isCurrentVersion() ? String("ST 352:2013 (current)")
                                                              : String("Pre-2008 (Annex C)"));
                d.addField("Byte1", String::sprintf("0x%02X", vpid.byte1()));
                d.addField("Standard", vpid.payloadDescription());
                d.addField("LinkStandard", vpid.linkStandard().valueName());
                if (!vpid.isValid()) {
                        d.addWarning(String::sprintf(
                                "Byte 1 code 0x%02X is not a recognised SDI payload identifier; "
                                "decoded fields below are best-effort", vpid.byte1()));
                }
                // Historical (version 0) packets carry an Annex C byte
                // 2/3/4 layout that differs from the modern Table 1b
                // schema this decoder applies, so flag that the field
                // decode below may be misleading (ST 352:2013 §5.1 /
                // Annex C).
                if (!vpid.isCurrentVersion()) {
                        d.addWarning("Pre-2008 historical payload (byte 1 b7 = 0); bytes 2-4 follow "
                                     "the Annex C layout but are decoded here with the modern "
                                     "Table 1b layout and may be misleading - consult the raw bytes");
                }

                // -- Byte 2: picture rate + scanning (§5.3, Table 2) --
                FrameRate rate = vpid.pictureRate();
                d.addField("PictureRate", rate.isValid()
                                                  ? rate.toString()
                                                  : String::sprintf("Unknown (code 0x%X)",
                                                                    vpid.pictureRateCode()));
                d.addField("ScanMode", vpid.videoScanMode().valueName());
                if (vpid.isSdSchema()) {
                        // Annex B.1: byte 2 b7 (transport) is Reserved;
                        // only b6 (picture) is meaningful for SD.
                        d.addField("Picture", vpid.isProgressivePicture() ? String("Progressive")
                                                                          : String("Interlaced"));
                        d.addInfo("SD (Annex B.1) schema: byte 2 b7 (transport) is Reserved; only "
                                  "byte 2 b6 (picture scan) is defined");
                } else {
                        d.addField("Transport", vpid.isProgressiveTransport() ? String("Progressive")
                                                                              : String("Interlaced"));
                        d.addField("Picture", vpid.isProgressivePicture() ? String("Progressive")
                                                                          : String("Interlaced"));
                        if (!vpid.isProgressiveTransport() && vpid.isProgressivePicture()) {
                                d.addInfo("PsF: a progressive picture carried as two segments over an "
                                          "interlaced transport (ST 352:2013 §5.3)");
                        }
                        if (!vpid.isProgressivePicture() && rate.isValid()) {
                                d.addInfo("Picture rate is the source frame rate; an interlaced "
                                          "signal's field rate is twice this (ST 352:2013 §5.3 note 2)");
                        }
                }

                // -- Byte 3: sampling + aspect (§5.4, Table 3) --------
                d.addField("AspectRatio", vpid.is16x9() ? String("16:9") : String("4:3"));
                VpidSampling samp = vpid.sampling();
                d.addField("Sampling", samp == VpidSampling::Unknown
                                               ? String::sprintf("Reserved (0x%X)", vpid.samplingCode())
                                               : samp.valueDisplayName());
                // The +A / +D variants share a wire shape but differ in what
                // the fourth component carries (ST 352:2013 Table 3 note 2).
                if (samp == VpidSampling::YCbCrA_4224 || samp == VpidSampling::YCbCrA_4444 ||
                    samp == VpidSampling::RGBA_4444) {
                        d.addInfo("Fourth component is a picture (alpha) channel");
                } else if (samp == VpidSampling::YCbCrD_4224 || samp == VpidSampling::YCbCrD_4444 ||
                           samp == VpidSampling::RGBD_4444) {
                        d.addInfo("Fourth component is a non-picture data channel (ST 352 Table 3 note 2)");
                }
                // Byte 3 b6 is re-used per schema: SD active-luma sample
                // count (720/960), or 6G/12G sub-image width (1920/2048).
                if (vpid.isSdSchema()) {
                        d.addField("HorizontalLumaSamples",
                                   vpid.sdHas960Samples() ? String("960") : String("720"));
                } else if (vpid.isExtendedSchema()) {
                        d.addField("SubImageWidth",
                                   vpid.has2048Samples() ? String("2048") : String("1920"));
                }

                // -- Byte 4: special options (§5.5) -------------------
                int bd = vpid.bitDepth();
                d.addField("BitDepth", bd > 0 ? String::sprintf("%d-bit", bd) : String("Reserved"));
                d.addField("WireFormat", vpid.wireFormat().valueName());
                // channelAssignment() is 0-based on the wire (0 = single-link
                // / channel 1); present it 1-based as humans count channels.
                uint8_t ch = vpid.channelAssignment();
                d.addField("Channel", ch == 0 ? String("1 (single-link or ch1 of multi-channel)")
                                              : String::number(ch + 1));

                // The 6G/12G payloads repurpose the ST 352:2013 reserved bits
                // for HDR / WCG signalling; only surface those fields when the
                // byte 1 code actually names an extended-schema payload.
                if (vpid.isExtendedSchema()) {
                        d.addField("Transfer", vpid.transferCharacteristic().valueName());
                        d.addField("Colorimetry", vpid.colorimetry().valueName());
                        d.addField("SignalType", vpid.isIctcp() ? String("ICtCp") : String("Y'CbCr"));
                        d.addField("QuantizationRange",
                                   vpid.isFullRange() ? String("Full") : String("Narrow (legal)"));
                        // For RGB / RGBA sampling the luminance signal-type
                        // bit is not meaningful (ST 2081-10 / 2082-10).
                        if (vpid.isIctcp() && (samp == VpidSampling::RGB_444 ||
                                               samp == VpidSampling::RGBA_4444)) {
                                d.addInfo("ICtCp signal-type bit set on an R'G'B' sampling structure; "
                                          "the bit may be ignored for RGB payloads (ST 2081-10)");
                        }
                }

                // ST 352 §5 reserves the inter-field bits (default 0) but
                // explicitly allows application documents to re-use them.
                // For a plain (non-SD, non-extended, current-version)
                // payload there is no such re-use, so a non-zero reserved
                // bit is worth surfacing.
                if (vpid.isCurrentVersion() && vpid.isValid() && !vpid.isSdSchema() &&
                    !vpid.isExtendedSchema()) {
                        uint8_t r2 = static_cast<uint8_t>(vpid.byte2() & 0x30);
                        uint8_t r3 = static_cast<uint8_t>(vpid.byte3() & 0x70);
                        uint8_t r4 = static_cast<uint8_t>(vpid.byte4() & 0x1C);
                        if (r2 || r3 || r4) {
                                d.addInfo(String::sprintf(
                                        "Reserved bits are non-zero (byte2[5:4]=0x%X, byte3[6:4]=0x%X, "
                                        "byte4[4:2]=0x%X); ST 352 §5 defaults these to 0 but permits "
                                        "application-specific re-use",
                                        r2 >> 4, r3 >> 4, r4 >> 2));
                        }
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
