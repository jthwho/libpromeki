/**
 * @file      anccodec_hdrstatic_st291.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 2108-1:2018 HDR/WCG Metadata Ancillary Data Packet codec
 * for @c AncFormat::HdrStatic2086 on @c AncTransport::St291.
 *
 * Wire structure (DID = @c 0x41, SDID = @c 0x0C, Type-2 ST 291 packet):
 *
 * @code
 * UDW =
 *   { Frame Type (u8) | Frame Length j (u8) | Data Byte 1 .. Data Byte j }
 *   { Frame Type (u8) | Frame Length j (u8) | Data Byte 1 .. Data Byte j }
 *   ...
 * @endcode
 *
 * Multiple metadata frames may share one ANC packet — this codec emits
 * one Mastering-Display frame (Frame Type @c 0, when the
 * @c MasteringDisplay leaf is valid) and one Content-Light-Level frame
 * (Frame Type @c 1, when the @c ContentLightLevel leaf is valid) into
 * a single ANC packet.  On parse it walks every frame in the UDW and
 * folds known frame types into one @c HdrStaticMetadata Variant.
 *
 * Frame Type 0 — Static Metadata Type 1 (Mastering Display)
 *   Frame Length = 0x1A (26).  Frame Data Bytes encode an H.265
 *   @c mastering_display_colour_volume() SEI message:
 *     Byte 1 = 0x89 (SEI @c payloadType = 137)
 *     Byte 2 = 0x18 (SEI @c payloadSize = 24)
 *     Bytes 3..26 = mastering_display_colour_volume() body, big-endian:
 *       display_primaries_x/y[0..2]  6 × u(16), chromaticity * 50000
 *       white_point_x / y            2 × u(16)
 *       max_display_mastering_luminance u(32)  units 0.0001 cd/m²
 *       min_display_mastering_luminance u(32)  units 0.0001 cd/m²
 *
 * Frame Type 1 — Static Metadata Type 2 (Content Light Level)
 *   Frame Length = 0x06 (6).  Frame Data Bytes encode an H.265
 *   @c content_light_level_info() SEI message:
 *     Byte 1 = 0x90 (SEI @c payloadType = 144)
 *     Byte 2 = 0x04 (SEI @c payloadSize = 4)
 *     Bytes 3..6 = content_light_level_info() body, big-endian:
 *       max_content_light_level       u(16)  units cd/m²
 *       max_pic_average_light_level   u(16)  units cd/m²
 *
 * @par EOTF fidelity gap
 * ST 2108-1 does not carry the transfer characteristic (EOTF); HDR
 * signalling on SDI relies on the SMPTE ST 352 Payload Identifier
 * for that.  A round-trip through this codec therefore preserves the
 * mastering display + content light level leaves of
 * @ref HdrStaticMetadata exactly but resets @c eotf to
 * @c TransferCharacteristics::Unspecified on parse.
 *
 * @par Primary ordering
 * The H.265 spec's "suggested" convention of (c=0 green, c=1 blue,
 * c=2 red) is non-normative — implementations differ.  This codec
 * mirrors the existing CTA-861.3 DRM-InfoFrame codec in
 * @c anccodec_hdrstatic.cpp by treating (c=0 red, c=1 green, c=2
 * blue), keeping every wire form's interpretation of
 * @ref MasteringDisplay::red / @c green / @c blue consistent across
 * the library.
 */

#include <cmath>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/contentlightlevel.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/list.h>
#include <promeki/masteringdisplay.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ST 2108-1 frame types ---------------------------------------
        constexpr uint8_t kFrameTypeMasteringDisplay  = 0;
        constexpr uint8_t kFrameTypeContentLightLevel = 1;

        // SEI payload type bytes --------------------------------------
        constexpr uint8_t kSeiPayloadTypeMasteringDisplay = 0x89; // 137
        constexpr uint8_t kSeiPayloadTypeContentLight     = 0x90; // 144
        constexpr uint8_t kSeiPayloadSizeMasteringDisplay = 0x18; // 24
        constexpr uint8_t kSeiPayloadSizeContentLight     = 0x04; // 4

        // Frame Length values per spec --------------------------------
        constexpr uint8_t kFrameLengthMasteringDisplay  = 0x1A; // 26
        constexpr uint8_t kFrameLengthContentLightLevel = 0x06; // 6

        constexpr double kChromaticityScale = 50000.0;
        constexpr double kHevcLumScale      = 10000.0; // u(32) units of 0.0001 cd/m²

        uint16_t encodeChromaticityBe(double v) {
                if (!std::isfinite(v) || v < 0.0) return 0;
                double s = std::round(v * kChromaticityScale);
                if (s > 65535.0) return 65535;
                return static_cast<uint16_t>(s);
        }

        double decodeChromaticity(uint16_t raw) { return static_cast<double>(raw) / kChromaticityScale; }

        uint32_t encodeLumU32(double v) {
                if (!std::isfinite(v) || v < 0.0) return 0;
                double s = std::round(v * kHevcLumScale);
                if (s > 4294967295.0) return 4294967295u;
                return static_cast<uint32_t>(s);
        }

        void writeU16Be(List<uint16_t> &udw, uint16_t v) {
                udw.pushToBack(static_cast<uint16_t>((v >> 8) & 0xFF));
                udw.pushToBack(static_cast<uint16_t>(v & 0xFF));
        }

        void writeU32Be(List<uint16_t> &udw, uint32_t v) {
                udw.pushToBack(static_cast<uint16_t>((v >> 24) & 0xFF));
                udw.pushToBack(static_cast<uint16_t>((v >> 16) & 0xFF));
                udw.pushToBack(static_cast<uint16_t>((v >> 8) & 0xFF));
                udw.pushToBack(static_cast<uint16_t>(v & 0xFF));
        }

        // Append the H.265 mastering_display_colour_volume() SEI body
        // (24 bytes) to @p udw as 8-bit data values in uint16_t slots.
        void appendMasteringDisplaySeiBody(List<uint16_t> &udw, const MasteringDisplay &md) {
                writeU16Be(udw, encodeChromaticityBe(md.red().x()));
                writeU16Be(udw, encodeChromaticityBe(md.red().y()));
                writeU16Be(udw, encodeChromaticityBe(md.green().x()));
                writeU16Be(udw, encodeChromaticityBe(md.green().y()));
                writeU16Be(udw, encodeChromaticityBe(md.blue().x()));
                writeU16Be(udw, encodeChromaticityBe(md.blue().y()));
                writeU16Be(udw, encodeChromaticityBe(md.whitePoint().x()));
                writeU16Be(udw, encodeChromaticityBe(md.whitePoint().y()));
                writeU32Be(udw, encodeLumU32(md.maxLuminance()));
                writeU32Be(udw, encodeLumU32(md.minLuminance()));
        }

        // Append the H.265 content_light_level_info() SEI body
        // (4 bytes) to @p udw.
        void appendContentLightLevelSeiBody(List<uint16_t> &udw, const ContentLightLevel &cll) {
                writeU16Be(udw, cll.maxCLL());
                writeU16Be(udw, cll.maxFALL());
        }

        // Append a complete ST 2108-1 metadata frame:
        //   [Type | Length | Data Byte 1 .. Data Byte j]
        // where the Data Bytes here are pre-pended with the H.265 SEI
        // payloadType + payloadSize bytes per the spec.
        void appendMetadataFrame(List<uint16_t> &udw, uint8_t frameType, uint8_t frameLength,
                                  uint8_t seiPayloadType, uint8_t seiPayloadSize,
                                  void (*emitSeiBody)(List<uint16_t> &, const void *), const void *src) {
                udw.pushToBack(frameType);
                udw.pushToBack(frameLength);
                udw.pushToBack(seiPayloadType);
                udw.pushToBack(seiPayloadSize);
                emitSeiBody(udw, src);
        }

        void emitMdSei(List<uint16_t> &udw, const void *src) {
                appendMasteringDisplaySeiBody(udw, *static_cast<const MasteringDisplay *>(src));
        }
        void emitCllSei(List<uint16_t> &udw, const void *src) {
                appendContentLightLevelSeiBody(udw, *static_cast<const ContentLightLevel *>(src));
        }

        // -----------------------------------------------------------------------
        // Parser
        // -----------------------------------------------------------------------

        uint16_t readU16Be(const List<uint16_t> &udw, size_t offset) {
                return static_cast<uint16_t>(((udw[offset] & 0xFF) << 8) | (udw[offset + 1] & 0xFF));
        }

        uint32_t readU32Be(const List<uint16_t> &udw, size_t offset) {
                return static_cast<uint32_t>((udw[offset] & 0xFF) << 24) |
                       static_cast<uint32_t>((udw[offset + 1] & 0xFF) << 16) |
                       static_cast<uint32_t>((udw[offset + 2] & 0xFF) << 8) |
                       static_cast<uint32_t>(udw[offset + 3] & 0xFF);
        }

        // Decodes one Mastering-Display SEI body (24 bytes starting at
        // @p off in @p udw) into @p out.
        void decodeMasteringDisplaySei(const List<uint16_t> &udw, size_t off, MasteringDisplay &out) {
                CIEPoint red  (decodeChromaticity(readU16Be(udw, off + 0)),
                                decodeChromaticity(readU16Be(udw, off + 2)));
                CIEPoint green(decodeChromaticity(readU16Be(udw, off + 4)),
                                decodeChromaticity(readU16Be(udw, off + 6)));
                CIEPoint blue (decodeChromaticity(readU16Be(udw, off + 8)),
                                decodeChromaticity(readU16Be(udw, off + 10)));
                CIEPoint wp   (decodeChromaticity(readU16Be(udw, off + 12)),
                                decodeChromaticity(readU16Be(udw, off + 14)));
                double maxL = static_cast<double>(readU32Be(udw, off + 16)) / kHevcLumScale;
                double minL = static_cast<double>(readU32Be(udw, off + 20)) / kHevcLumScale;
                out = MasteringDisplay(red, green, blue, wp, minL, maxL);
        }

        void decodeContentLightLevelSei(const List<uint16_t> &udw, size_t off, ContentLightLevel &out) {
                out = ContentLightLevel(readU16Be(udw, off + 0), readU16Be(udw, off + 2));
        }

        Result<Variant> parseHdrStaticSt291(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<Variant>(rp.second());
                List<uint16_t> udw = rp.first().udw();

                // Walk every (Type, Length, Data) triple in the UDW.
                MasteringDisplay  mdSeen;
                ContentLightLevel cllSeen;
                bool              haveMd = false;
                bool              haveCll = false;

                size_t i = 0;
                while (i + 1 < udw.size()) {
                        uint8_t frameType = static_cast<uint8_t>(udw[i] & 0xFF);
                        uint8_t frameLen = static_cast<uint8_t>(udw[i + 1] & 0xFF);
                        size_t  dataStart = i + 2;
                        size_t  dataEnd = dataStart + frameLen;
                        if (dataEnd > udw.size()) {
                                return makeError<Variant>(Error::CorruptData);
                        }

                        switch (frameType) {
                                case kFrameTypeMasteringDisplay: {
                                        if (frameLen < kFrameLengthMasteringDisplay) {
                                                return makeError<Variant>(Error::CorruptData);
                                        }
                                        uint8_t pt = static_cast<uint8_t>(udw[dataStart] & 0xFF);
                                        uint8_t ps = static_cast<uint8_t>(udw[dataStart + 1] & 0xFF);
                                        if (pt != kSeiPayloadTypeMasteringDisplay ||
                                            ps != kSeiPayloadSizeMasteringDisplay) {
                                                return makeError<Variant>(Error::CorruptData);
                                        }
                                        decodeMasteringDisplaySei(udw, dataStart + 2, mdSeen);
                                        haveMd = true;
                                        break;
                                }
                                case kFrameTypeContentLightLevel: {
                                        if (frameLen < kFrameLengthContentLightLevel) {
                                                return makeError<Variant>(Error::CorruptData);
                                        }
                                        uint8_t pt = static_cast<uint8_t>(udw[dataStart] & 0xFF);
                                        uint8_t ps = static_cast<uint8_t>(udw[dataStart + 1] & 0xFF);
                                        if (pt != kSeiPayloadTypeContentLight ||
                                            ps != kSeiPayloadSizeContentLight) {
                                                return makeError<Variant>(Error::CorruptData);
                                        }
                                        decodeContentLightLevelSei(udw, dataStart + 2, cllSeen);
                                        haveCll = true;
                                        break;
                                }
                                default:
                                        // Unknown / reserved frame type — skip per ST 2108-1
                                        // forward-compatibility (Dynamic Metadata Types 1/5,
                                        // reserved 3/4/5, vendor extensions).
                                        break;
                        }
                        i = dataEnd;
                }

                if (!haveMd && !haveCll) {
                        return makeError<Variant>(Error::NotSupported);
                }
                // ST 2108-1 carries no EOTF — left as Unspecified.
                HdrStaticMetadata md(TransferCharacteristics::Unspecified,
                                     haveMd ? mdSeen : MasteringDisplay(),
                                     haveCll ? cllSeen : ContentLightLevel());
                return makeResult<Variant>(Variant(md));
        }

        Result<List<AncPacket>> buildHdrStaticSt291(const Variant &v, const AncTranslateConfig &cfg) {
                HdrStaticMetadata md = v.get<HdrStaticMetadata>();

                List<uint16_t> udw;
                const MasteringDisplay  &mdLeaf = md.masteringDisplay();
                const ContentLightLevel &cllLeaf = md.contentLightLevel();

                // ST 2108-1 §5.3.2: at most one Type-0 frame per video
                // frame.  We emit it when the mastering display is valid;
                // otherwise omit (encoder choice, both legal per spec).
                if (mdLeaf.isValid()) {
                        appendMetadataFrame(udw, kFrameTypeMasteringDisplay, kFrameLengthMasteringDisplay,
                                            kSeiPayloadTypeMasteringDisplay, kSeiPayloadSizeMasteringDisplay,
                                            emitMdSei, &mdLeaf);
                }
                if (cllLeaf.isValid()) {
                        appendMetadataFrame(udw, kFrameTypeContentLightLevel, kFrameLengthContentLightLevel,
                                            kSeiPayloadTypeContentLight, kSeiPayloadSizeContentLight,
                                            emitCllSei, &cllLeaf);
                }
                if (udw.isEmpty()) {
                        // No round-trippable content (e.g. EOTF-only).
                        return makeError<List<AncPacket>>(Error::InvalidArgument);
                }

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);

                St291Packet     p = St291Packet::build(AncFormat(AncFormat::HdrStatic2086), udw, line,
                                                        St291Packet::UnspecifiedHOffset, fieldB);
                List<AncPacket> out;
                out.pushToBack(p.packet());
                return makeResult<List<AncPacket>>(std::move(out));
        }

        // HDR static metadata is sticky — Mastering Display + Content Light
        // Level describe the source's grading environment and don't carry
        // any per-frame sequence state.  Repeating the packet on a held
        // output frame is correct (downstream consumer keeps the same
        // grading metadata); dropping it is also fine (the next surviving
        // packet re-establishes it).
        Result<List<AncPacket>> syncPolicyHdrStatic2086(const AncPacket &pkt, FrameSyncDisposition d,
                                                         uint8_t /*repeatIndex*/,
                                                         const AncTranslateConfig & /*cfg*/) {
                List<AncPacket> out;
                if (d.kind() != FrameSyncDisposition::Drop) {
                        out.pushToBack(pkt);
                }
                return makeResult<List<AncPacket>>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(HdrStatic_St291, HdrStatic2086, ::promeki::AncTransport::St291,
                             ::promeki::parseHdrStaticSt291)
PROMEKI_REGISTER_ANC_BUILDER(HdrStatic_St291, HdrStatic2086, ::promeki::AncTransport::St291,
                              ::promeki::buildHdrStaticSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(HdrStatic2086, HdrStatic2086, ::promeki::syncPolicyHdrStatic2086)
