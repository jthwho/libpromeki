/**
 * @file      sdivpid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdivpid.h>

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/char.h>
#include <promeki/datastream.h>
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/st291packet.h>
#include <promeki/string.h>
#include <promeki/videoformat.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SdiVpid::SdiVpid(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4) {
        _bytes[0] = b1;
        _bytes[1] = b2;
        _bytes[2] = b3;
        _bytes[3] = b4;
}

SdiVpid::SdiVpid(const ByteArray &bytes) : _bytes(bytes) {}

// ---------------------------------------------------------------------------
// Validity
// ---------------------------------------------------------------------------

bool SdiVpid::isValid() const {
        switch (_bytes[0]) {
                case Byte1_SL_SD:
                case Byte1_SL_HD_720:
                case Byte1_SL_HD_1080:
                case Byte1_DL_HD:
                case Byte1_SL_3GA_720:
                case Byte1_SL_3GA_1080:
                case Byte1_SL_3GB:
                case Byte1_SL_6G_2160:
                case Byte1_SL_6G_1080:
                case Byte1_SL_12G_2160:
                case Byte1_SL_12G_1080:
                // Annex C historical codes — recognised so that
                // legacy packets aren't dismissed as invalid noise.
                // The field decoders still apply Table 1b layout,
                // which is wrong for these — see Annex C
                // documentation.
                case Byte1_AnnexC_BT601:
                case Byte1_AnnexC_BT1358:
                case Byte1_AnnexC_ST347:
                case Byte1_AnnexC_ST274:
                case Byte1_AnnexC_ST296:
                case Byte1_AnnexC_ST349:
                        return true;
                default:
                        return false;
        }
}

bool SdiVpid::isAnnexC() const {
        switch (_bytes[0]) {
                case Byte1_AnnexC_BT601:
                case Byte1_AnnexC_BT1358:
                case Byte1_AnnexC_ST347:
                case Byte1_AnnexC_ST274:
                case Byte1_AnnexC_ST296:
                case Byte1_AnnexC_ST349:
                        return true;
                default:
                        return false;
        }
}

bool SdiVpid::isExtendedSchema() const {
        switch (_bytes[0]) {
                case Byte1_SL_6G_2160:
                case Byte1_SL_6G_1080:
                case Byte1_SL_12G_2160:
                case Byte1_SL_12G_1080:
                        return true;
                default:
                        return false;
        }
}

// ---------------------------------------------------------------------------
// Decoded fields
// ---------------------------------------------------------------------------

SdiLinkStandard SdiVpid::linkStandard() const {
        switch (_bytes[0]) {
                case Byte1_SL_SD:        return SdiLinkStandard::SL_SD;
                case Byte1_SL_HD_720:    return SdiLinkStandard::SL_HD;
                case Byte1_SL_HD_1080:   return SdiLinkStandard::SL_HD;
                case Byte1_DL_HD:        return SdiLinkStandard::DL_HD;
                case Byte1_SL_3GA_720:   return SdiLinkStandard::SL_3GA;
                case Byte1_SL_3GA_1080:  return SdiLinkStandard::SL_3GA;
                case Byte1_SL_3GB:       return SdiLinkStandard::SL_3GB;
                case Byte1_SL_6G_2160:   return SdiLinkStandard::SL_6G;
                case Byte1_SL_6G_1080:   return SdiLinkStandard::SL_6G;
                case Byte1_SL_12G_2160:  return SdiLinkStandard::SL_12G;
                case Byte1_SL_12G_1080:  return SdiLinkStandard::SL_12G;
                // Annex C legacy codes — promote each to its modern
                // SdiLinkStandard equivalent so consumers see a
                // consistent standard regardless of which spec era
                // generated the VPID.
                case Byte1_AnnexC_BT601:  return SdiLinkStandard::SL_SD;
                case Byte1_AnnexC_BT1358: return SdiLinkStandard::SL_SD;
                case Byte1_AnnexC_ST347:  return SdiLinkStandard::SL_SD;
                case Byte1_AnnexC_ST274:  return SdiLinkStandard::SL_HD;
                case Byte1_AnnexC_ST296:  return SdiLinkStandard::SL_HD;
                case Byte1_AnnexC_ST349:  return SdiLinkStandard::SL_HD;
                default:                 return SdiLinkStandard::Auto;
        }
}

SdiWireFormat SdiVpid::wireFormat() const {
        // Gate on validity so a default-constructed (all-zero) VPID
        // doesn't claim a fabricated wire payload.
        if (!isValid()) return SdiWireFormat::Auto;

        const uint8_t samp = samplingCode();

        // Bit-depth decoding depends on schema: ST 352:2013 has
        // 0=8/1=10/2=12, while ST 2081-10 / ST 2082-10 have
        // 0=10-Full/1=10/2=12/3=12-Full.  Use bitDepth() to fold both
        // schemas into a single bits-per-component integer.
        const int bits = bitDepth();
        const bool is10 = (bits == 10);
        const bool is12 = (bits == 12);
        // 8-bit payloads aren't modelled as SdiWireFormat (no SDI
        // enumerator covers them), and the reserved code (3h in the
        // 2160-line 6G/12G schema) decodes to 0 — both collapse to
        // Auto.
        if (!is10 && !is12) return SdiWireFormat::Auto;

        switch (samp) {
                case Sampling_YCbCr_422:
                        return is10 ? SdiWireFormat::YCbCr_422_10 : SdiWireFormat::YCbCr_422_12;
                case Sampling_YCbCr_444:
                        return is10 ? SdiWireFormat::YCbCr_444_10 : SdiWireFormat::YCbCr_444_12;
                case Sampling_RGB_444:
                        return is10 ? SdiWireFormat::RGB_444_10 : SdiWireFormat::RGB_444_12;
                case Sampling_RGBA_4444:
                        // Only the 10-bit RGBA wire format is defined;
                        // 12-bit RGBA is not standardised on SDI.
                        return is10 ? SdiWireFormat::RGBA_444_10 : SdiWireFormat::Auto;
                case Sampling_YCbCr_420:
                case Sampling_YCbCrA_4224:
                case Sampling_YCbCrA_4444:
                default:
                        // 4:2:0 and YCbCr+alpha samplings exist in ST
                        // 352 but the library doesn't expose them as
                        // SdiWireFormat enumerators — no SDI carrier
                        // defined at this layer.
                        return SdiWireFormat::Auto;
        }
}

FrameRate SdiVpid::pictureRate() const {
        // ST 352:2013 Table 2 + ST 2081-10 / 2082-10 Table 4 extension.
        switch (pictureRateCode()) {
                case Rate_95_90: return FrameRate(FrameRate::FPS_95_90);
                case Rate_23_98: return FrameRate(FrameRate::FPS_23_98);
                case Rate_24:    return FrameRate(FrameRate::FPS_24);
                case Rate_47_95: return FrameRate(FrameRate::FPS_47_95);
                case Rate_25:    return FrameRate(FrameRate::FPS_25);
                case Rate_29_97: return FrameRate(FrameRate::FPS_29_97);
                case Rate_30:    return FrameRate(FrameRate::FPS_30);
                case Rate_48:    return FrameRate(FrameRate::FPS_48);
                case Rate_50:    return FrameRate(FrameRate::FPS_50);
                case Rate_59_94: return FrameRate(FrameRate::FPS_59_94);
                case Rate_60:    return FrameRate(FrameRate::FPS_60);
                case Rate_96:    return FrameRate(FrameRate::FPS_96);
                case Rate_100:   return FrameRate(FrameRate::FPS_100);
                case Rate_119_88: return FrameRate(FrameRate::FPS_119_88);
                case Rate_120:   return FrameRate(FrameRate::FPS_120);
                default:         return FrameRate();
        }
}

VideoScanMode SdiVpid::videoScanMode() const {
        if (isSdSchema()) {
                // Annex B.1: byte 2 bit 7 is Reserved; only bit 6
                // marks I/P picture.  PsF isn't representable in this
                // schema — SD signals are pure interlaced or pure
                // progressive.
                return isProgressivePicture() ? VideoScanMode::Progressive
                                              : VideoScanMode::Interlaced;
        }
        // Default Table 1b layout (HD/3G/6G/12G):
        //  PT=1, PS=1 → Progressive
        //  PT=0, PS=1 → PsF
        //  PT=0, PS=0 → Interlaced
        //  PT=1, PS=0 → Reserved (treat as Progressive)
        const bool transportProg = isProgressiveTransport();
        const bool pictureProg   = isProgressivePicture();
        if (transportProg && pictureProg) return VideoScanMode::Progressive;
        if (!transportProg && pictureProg) return VideoScanMode::PsF;
        if (!transportProg && !pictureProg) return VideoScanMode::Interlaced;
        return VideoScanMode::Progressive;
}

int SdiVpid::bitDepth() const {
        // Same gating rationale as wireFormat — an invalid VPID has
        // no meaningful payload-shape data.
        if (!isValid()) return 0;
        const uint8_t code = bitDepthCode();
        if (isExtendedSchema()) {
                // ST 2081-10 / ST 2082-10 codes:
                //   0 = 10-bit Full Range
                //   1 = 10-bit (narrow)
                //   2 = 12-bit (narrow)
                //   3 = 12-bit Full Range (only 1080-line modes;
                //       reserved in 2160-line modes — we still report
                //       12 since the bit-depth is the same).
                switch (code) {
                        case 0: return 10;
                        case 1: return 10;
                        case 2: return 12;
                        case 3: return 12;
                        default: return 0;
                }
        }
        // ST 352:2013 codes:
        //   0 = 8-bit, 1 = 10-bit, 2 = 12-bit, 3 = Reserved.
        switch (code) {
                case BitDepth_8:  return 8;
                case BitDepth_10: return 10;
                case BitDepth_12: return 12;
                default:          return 0;
        }
}

bool SdiVpid::isFullRange() const {
        if (!isValid()) return false;
        if (!isExtendedSchema()) {
                // ST 352:2013 carries no explicit range flag — narrow
                // is the conventional default for broadcast SDR.
                return false;
        }
        // ST 2081-10 / 2082-10: codes 0h and 3h are "Full Range"
        // variants (10-bit and 12-bit respectively).
        const uint8_t code = bitDepthCode();
        return code == 0 || code == 3;
}

TransferCharacteristics SdiVpid::transferCharacteristic() const {
        if (!isExtendedSchema()) return TransferCharacteristics::Unspecified;
        switch (transferCode()) {
                case Transfer_SDR:         return TransferCharacteristics::BT709;
                case Transfer_HLG:         return TransferCharacteristics::ARIB_STD_B67;
                case Transfer_PQ:          return TransferCharacteristics::SMPTE2084;
                case Transfer_Unspecified: return TransferCharacteristics::Unspecified;
                default:                   return TransferCharacteristics::Unspecified;
        }
}

ColorPrimaries SdiVpid::colorimetry() const {
        if (!isExtendedSchema()) return ColorPrimaries::Unspecified;
        switch (colorimetryCode()) {
                case Colorimetry_Rec709:  return ColorPrimaries::BT709;
                case Colorimetry_VANC:    return ColorPrimaries::Unspecified;
                case Colorimetry_UHDTV:   return ColorPrimaries::BT2020;
                case Colorimetry_Unknown: return ColorPrimaries::Unspecified;
                default:                  return ColorPrimaries::Unspecified;
        }
}

// ---------------------------------------------------------------------------
// Encoders
// ---------------------------------------------------------------------------

uint8_t SdiVpid::encodePictureRateCode(const FrameRate &rate) {
        if (!rate.isValid()) return Rate_Unknown;
        switch (rate.wellKnownRate()) {
                case FrameRate::FPS_23_98: return Rate_23_98;
                case FrameRate::FPS_24:    return Rate_24;
                case FrameRate::FPS_47_95: return Rate_47_95;
                case FrameRate::FPS_25:    return Rate_25;
                case FrameRate::FPS_29_97: return Rate_29_97;
                case FrameRate::FPS_30:    return Rate_30;
                case FrameRate::FPS_48:    return Rate_48;
                case FrameRate::FPS_50:    return Rate_50;
                case FrameRate::FPS_59_94: return Rate_59_94;
                case FrameRate::FPS_60:    return Rate_60;
                // HFR codes (ST 2081-10 / 2082-10 extensions).
                case FrameRate::FPS_95_90: return Rate_95_90;
                case FrameRate::FPS_96:    return Rate_96;
                case FrameRate::FPS_100:   return Rate_100;
                case FrameRate::FPS_119_88: return Rate_119_88;
                case FrameRate::FPS_120:   return Rate_120;
                default:                   return Rate_Unknown;
        }
}

namespace {

// Pick the byte 1 code for a given link standard.  HD and 3G Level A
// need the raster height to disambiguate the 720-line vs 1080-line
// payload identifier; other standards have a single canonical code.
uint8_t encodeByte1(const SdiLinkStandard &standard, uint32_t rasterHeight) {
        if (standard == SdiLinkStandard::SL_SD)  return SdiVpid::Byte1_SL_SD;
        if (standard == SdiLinkStandard::SL_HD) {
                return rasterHeight == 720 ? SdiVpid::Byte1_SL_HD_720
                                           : SdiVpid::Byte1_SL_HD_1080;
        }
        if (standard == SdiLinkStandard::DL_HD)  return SdiVpid::Byte1_DL_HD;
        if (standard == SdiLinkStandard::SL_3GA) {
                return rasterHeight == 720 ? SdiVpid::Byte1_SL_3GA_720
                                           : SdiVpid::Byte1_SL_3GA_1080;
        }
        if (standard == SdiLinkStandard::SL_3GB) return SdiVpid::Byte1_SL_3GB;
        // Quad-link 3G variants ride their per-sub-image VPID using
        // the matching 3G Level A code; promote to SL_3GA_1080 since
        // 2SI sub-images carry the Level A wire format and the only
        // application is 2160-line video distributed across sub-images.
        if (standard == SdiLinkStandard::QL_3G_SQD) return SdiVpid::Byte1_SL_3GA_1080;
        if (standard == SdiLinkStandard::QL_3G_2SI) return SdiVpid::Byte1_SL_3GA_1080;
        // DL_3G / DL_3GB carry per-sub-link VPIDs using the matching
        // 3G Level A / Level B single-link code respectively.
        if (standard == SdiLinkStandard::DL_3G)  return SdiVpid::Byte1_SL_3GA_1080;
        if (standard == SdiLinkStandard::DL_3GB) return SdiVpid::Byte1_SL_3GB;
        // 6G / 12G split on raster height into 2160-line (UHD) vs
        // 1080-line (HD or HFR) variants — see ST 2081-10 / 2082-10
        // mode definitions.
        if (standard == SdiLinkStandard::SL_6G) {
                return rasterHeight == 1080 ? SdiVpid::Byte1_SL_6G_1080
                                            : SdiVpid::Byte1_SL_6G_2160;
        }
        if (standard == SdiLinkStandard::SL_12G) {
                return rasterHeight == 1080 ? SdiVpid::Byte1_SL_12G_1080
                                            : SdiVpid::Byte1_SL_12G_2160;
        }
        // 24G-SDI byte 1 code is not defined in ST 352:2013 or in any
        // of ST 2081-10 / ST 2082-10; the SMPTE-RA registry would be
        // the source for that value.  Until that's wired in, the
        // encoder reports Unknown.
        if (standard == SdiLinkStandard::SL_24G) return SdiVpid::Byte1_Unknown;
        return SdiVpid::Byte1_Unknown;
}

// Build byte 2 from a picture-rate code + a VideoScanMode.
//
// Default Table 1b schema (HD / 3G / 6G / 12G):
//   bit 7 = I/P transport (1 = progressive)
//   bit 6 = I/P picture (1 = progressive)
//   bits 5-4 = reserved (0)
//   bits 3-0 = picture rate code
//
// Annex B.1 schema (SD, byte 1 = 0x81):
//   bit 7 = Reserved (must be 0)
//   bit 6 = I/P picture (1 = progressive, 0 = interlaced)
//   bits 5-4 = Reserved (must be 0)
//   bits 3-0 = picture rate code
uint8_t encodeByte2(uint8_t byte1, uint8_t rateCode, VideoScanMode scan) {
        const bool isSd = (byte1 == SdiVpid::Byte1_SL_SD);
        bool transportProg = false;
        bool pictureProg   = false;
        if (scan == VideoScanMode::Progressive) {
                transportProg = true;
                pictureProg   = true;
        } else if (scan == VideoScanMode::PsF) {
                // Progressive picture transported as two field-like
                // segments — picture progressive, transport interlaced.
                transportProg = false;
                pictureProg   = true;
        } else {
                // Interlaced / Even-first / Odd-first / Unknown all
                // ride as interlaced transport + interlaced picture.
                transportProg = false;
                pictureProg   = false;
        }
        uint8_t b = static_cast<uint8_t>(rateCode & 0x0F);
        // SD signals leave bit 7 at 0 (Reserved per Annex B.1); only
        // bit 6 carries the I/P picture flag.
        if (!isSd && transportProg) b = static_cast<uint8_t>(b | 0x80);
        if (pictureProg)            b = static_cast<uint8_t>(b | 0x40);
        return b;
}

// Build byte 3 from a wire format + aspect.
//   bit 7 = image aspect ratio (1 = 16:9)
//   bits 6-4 = reserved (0)
//   bits 3-0 = sampling structure
//
// Returns 0x00 for SdiWireFormat::Auto (no wire payload to encode);
// the aspect bit is set to 16:9 since every modern HD+ SDI signal is
// widescreen.  SD 4:3 callers can clear the bit via setByte3.
uint8_t encodeByte3(const SdiWireFormat &wf) {
        if (wf == SdiWireFormat::Auto) return 0x00;
        uint8_t samp = 0;
        if (wf == SdiWireFormat::YCbCr_422_10 || wf == SdiWireFormat::YCbCr_422_12) {
                samp = SdiVpid::Sampling_YCbCr_422;
        } else if (wf == SdiWireFormat::YCbCr_444_10 || wf == SdiWireFormat::YCbCr_444_12) {
                samp = SdiVpid::Sampling_YCbCr_444;
        } else if (wf == SdiWireFormat::RGB_444_10 || wf == SdiWireFormat::RGB_444_12) {
                samp = SdiVpid::Sampling_RGB_444;
        } else if (wf == SdiWireFormat::RGBA_444_10) {
                samp = SdiVpid::Sampling_RGBA_4444;
        } else {
                return 0x00;
        }
        // 0x80 bit = 16:9 aspect default.
        return static_cast<uint8_t>(0x80 | (samp & 0x0F));
}

// Build byte 4 from a wire format.
//   bits 7-5 = channel assignment (default 0 = single-link / ch1)
//   bits 4-2 = reserved (0)
//   bits 1-0 = bit depth (0=8, 1=10, 2=12, 3=reserved)
//
// Callers driving multi-channel sub-image links can override the
// channel field via setByte4 after the encode.
uint8_t encodeByte4(const SdiWireFormat &wf) {
        if (wf == SdiWireFormat::Auto) return 0x00;
        uint8_t depth = SdiVpid::BitDepth_10;
        if (wf == SdiWireFormat::YCbCr_422_12 || wf == SdiWireFormat::YCbCr_444_12 ||
            wf == SdiWireFormat::RGB_444_12) {
                depth = SdiVpid::BitDepth_12;
        }
        return static_cast<uint8_t>(depth & 0x03);
}

} // namespace

SdiVpid SdiVpid::encode(const VideoFormat &videoFormat,
                        const SdiWireFormat &wireFormat,
                        const SdiLinkStandard &standard) {
        const uint32_t height = videoFormat.raster().height();
        const uint8_t  b1     = encodeByte1(standard, height);
        const uint8_t  b2     = encodeByte2(b1, encodePictureRateCode(videoFormat.frameRate()),
                                            videoFormat.videoScanMode());
        const uint8_t  b3     = encodeByte3(wireFormat);
        const uint8_t  b4     = encodeByte4(wireFormat);
        return SdiVpid(b1, b2, b3, b4);
}

SdiVpid SdiVpid::encode(const VideoFormat &videoFormat,
                        const SdiWireFormat &wireFormat,
                        const SdiLinkStandard &standard,
                        int channelIndex) {
        SdiVpid v = encode(videoFormat, wireFormat, standard);
        v.setChannelAssignment(static_cast<uint8_t>(channelIndex < 0 ? 0 : channelIndex));
        return v;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

Error SdiVpid::validate() const {
        // byte 1: must be a known payload identifier.
        if (!isValid()) return Error::InvalidArgument;

        const bool extended = isExtendedSchema();

        // byte 2 [3:0]: picture rate code.
        // ST 352:2013 Table 2 declares 1h and Ch-Fh Reserved, while
        // ST 2081-10 / 2082-10 Table 4 redefines them as the HFR
        // codes 96/1.001, 96, 100, 120/1.001, 120.  We accept them
        // unconditionally — any code 0h-Bh is defined in both
        // schemas, and 1h/Ch-Fh are defined in at least one schema,
        // so refusing them is overly strict for forensic round-trip.
        // 0h ("no defined value") stays an accepted "unspecified
        // rate" marker.

        // byte 3 [3:0]: sampling code.  Defined codes per ST 352:2013
        // Table 3 (also covered by ST 2081-10 / 2082-10 Table 5):
        //   0h-6h, 7h = ST 2048-2 FS (or Reserved in the 6G/12G
        //   variant — accept either way), 8h-Ah, Eh.
        //   Bh, Ch, Dh, Fh are Reserved across both schemas.
        const uint8_t samp = samplingCode();
        if (samp == 0xB || samp == 0xC || samp == 0xD || samp == 0xF) {
                return Error::InvalidArgument;
        }

        // byte 4 [1:0]: bit depth code.
        //   ST 352:2013:        3h Reserved
        //   ST 2081-10/2082-10: 3h = 12-bit Full Range (1080-line
        //                       modes) or Reserved (2160-line modes).
        // We accept 3h unconditionally — distinguishing the 2160-line
        // reserved case requires consulting byte 1 and applies only
        // in narrow circumstances.  Callers who need that strict
        // check can inspect @ref bitDepthCode and @ref byte1 manually.
        (void)extended;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Integer conversion
// ---------------------------------------------------------------------------

uint32_t SdiVpid::toUint32BE() const {
        return (static_cast<uint32_t>(_bytes[0]) << 24) |
               (static_cast<uint32_t>(_bytes[1]) << 16) |
               (static_cast<uint32_t>(_bytes[2]) << 8)  |
               static_cast<uint32_t>(_bytes[3]);
}

SdiVpid SdiVpid::fromUint32BE(uint32_t v) {
        return SdiVpid(static_cast<uint8_t>((v >> 24) & 0xFF),
                       static_cast<uint8_t>((v >> 16) & 0xFF),
                       static_cast<uint8_t>((v >> 8) & 0xFF),
                       static_cast<uint8_t>(v & 0xFF));
}

// ---------------------------------------------------------------------------
// ANC packet round-trip
// ---------------------------------------------------------------------------

St291Packet SdiVpid::toSt291Packet(uint16_t line, bool fieldB) const {
        List<uint16_t> udw;
        udw.reserve(4);
        for (int i = 0; i < 4; ++i) {
                udw.pushToBack(static_cast<uint16_t>(_bytes[i]));
        }
        return St291Packet::build(AncFormat(AncFormat::Vpid), udw, line,
                                  St291Packet::UnspecifiedHOffset, fieldB, false, 0);
}

Result<SdiVpid> SdiVpid::fromSt291Packet(const St291Packet &pkt) {
        if (pkt.did() != 0x41 || pkt.sdid() != 0x01) {
                return makeError<SdiVpid>(Error::InvalidArgument);
        }
        if (pkt.dataCount() != 0x04) {
                return makeError<SdiVpid>(Error::InvalidArgument);
        }
        const List<uint16_t> udw = pkt.udw();
        if (udw.size() != 4) {
                return makeError<SdiVpid>(Error::InvalidArgument);
        }
        ByteArray bytes{};
        for (int i = 0; i < 4; ++i) {
                // Strip parity bits — only the low 8 of each 10-bit
                // ST 291 word are the data byte.
                bytes[i] = static_cast<uint8_t>(udw.at(i) & 0xFF);
        }
        return makeResult<SdiVpid>(SdiVpid(bytes));
}

// ---------------------------------------------------------------------------
// Recommended VANC line lookup (ST 352:2013 §6.2)
// ---------------------------------------------------------------------------

int SdiVpid::recommendedAncLine(const VideoFormat &fmt, int field) {
        if (!fmt.isValid()) return 0;
        if (field != 1 && field != 2) return 0;

        // Map raster height → spec's line-count family.  The spec
        // talks about 525-line / 625-line / 750-line / 1125-line
        // total raster, but our VideoFormat carries the active-line
        // height (486 / 576 / 720 / 1080).
        const uint32_t height        = fmt.raster().height();
        const VideoScanMode scan     = fmt.videoScanMode();
        const bool          isInterlacedLike =
                scan == VideoScanMode::Interlaced ||
                scan == VideoScanMode::InterlacedEvenFirst ||
                scan == VideoScanMode::InterlacedOddFirst ||
                scan == VideoScanMode::PsF;

        if (height == 486) {
                // 525-line family.
                if (isInterlacedLike) return field == 1 ? 13 : 276;
                return field == 1 ? 13 : 0;
        }
        if (height == 576) {
                // 625-line family.
                if (isInterlacedLike) return field == 1 ? 9 : 322;
                return field == 1 ? 9 : 0;
        }
        if (height == 720) {
                // 750-line family — progressive only in practice.
                return field == 1 ? 10 : 0;
        }
        if (height == 1080) {
                // 1125-line family — interlaced / PsF use field 1 = 10,
                // field 2 = 572; progressive uses line 10.
                if (isInterlacedLike) return field == 1 ? 10 : 572;
                return field == 1 ? 10 : 0;
        }
        // UHD / 8K / DCI 2K / DCI 4K / custom — not covered by
        // ST 352:2013 §6.2.  Backends substitute their own default.
        return 0;
}

// ---------------------------------------------------------------------------
// String form
// ---------------------------------------------------------------------------

namespace {

// Returns the lower-case hex digit for a value 0-15.
char hexDigit(uint8_t v) {
        const char *digits = "0123456789abcdef";
        return digits[v & 0x0F];
}

} // namespace

String SdiVpid::toString() const {
        char buf[12];
        for (int i = 0; i < 4; ++i) {
                buf[i * 3 + 0] = hexDigit(static_cast<uint8_t>(_bytes[i] >> 4));
                buf[i * 3 + 1] = hexDigit(static_cast<uint8_t>(_bytes[i] & 0x0F));
                buf[i * 3 + 2] = ':';
        }
        // Drop the trailing colon.
        return String(buf, 11);
}

Result<SdiVpid> SdiVpid::fromString(const String &s) {
        String trimmed = s.trim();
        if (trimmed.isEmpty()) return makeError<SdiVpid>(Error::InvalidArgument);

        // Parse four hex bytes separated by colons.  Whitespace around
        // each byte is tolerated for friendly hand-edited input.
        ByteArray   bytes{};
        size_t      tokIdx = 0;
        size_t      start  = 0;
        const size_t n     = trimmed.length();
        for (size_t i = 0; i <= n; ++i) {
                if (i == n || trimmed.charAt(i) == ':') {
                        if (tokIdx >= 4) return makeError<SdiVpid>(Error::InvalidArgument);
                        String tok = trimmed.substr(start, i - start).trim();
                        if (tok.length() == 0 || tok.length() > 2) {
                                return makeError<SdiVpid>(Error::InvalidArgument);
                        }
                        int value = 0;
                        for (size_t j = 0; j < tok.length(); ++j) {
                                const int d = tok.charAt(j).hexDigitValue();
                                if (d < 0) return makeError<SdiVpid>(Error::InvalidArgument);
                                value = (value << 4) | d;
                        }
                        bytes[tokIdx++] = static_cast<uint8_t>(value);
                        start = i + 1;
                }
        }
        if (tokIdx != 4) return makeError<SdiVpid>(Error::InvalidArgument);
        return makeResult<SdiVpid>(SdiVpid(bytes));
}

// ---------------------------------------------------------------------------
// DataStream
// ---------------------------------------------------------------------------

Error SdiVpid::writeToStream(DataStream &s) const {
        for (int i = 0; i < 4; ++i) s << _bytes[i];
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<SdiVpid> SdiVpid::readFromStream<1>(DataStream &s) {
        ByteArray bytes{};
        for (int i = 0; i < 4; ++i) s >> bytes[i];
        if (s.status() != DataStream::Ok) return makeError<SdiVpid>(s.toError());
        return makeResult<SdiVpid>(SdiVpid(bytes));
}

PROMEKI_NAMESPACE_END
