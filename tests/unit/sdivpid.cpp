/**
 * @file      sdivpid.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/framerate.h>
#include <promeki/sdivpid.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>
#include <promeki/videoformat.h>

using namespace promeki;

// ============================================================================
// Construction + raw byte accessors
// ============================================================================

TEST_CASE("SdiVpid: default-construct yields an invalid all-zero VPID") {
        SdiVpid v;
        CHECK_FALSE(v.isValid());
        CHECK(v.byte1() == 0);
        CHECK(v.byte2() == 0);
        CHECK(v.byte3() == 0);
        CHECK(v.byte4() == 0);
        CHECK(v.linkStandard() == SdiLinkStandard::Auto);
        CHECK(v.wireFormat() == SdiWireFormat::Auto);
        CHECK_FALSE(v.pictureRate().isValid());
        CHECK(v.bitDepth() == 0);
}

TEST_CASE("SdiVpid: 4-byte constructor populates the raw bytes verbatim") {
        SdiVpid v(0x85, 0xC6, 0x81, 0x01);
        CHECK(v.byte1() == 0x85);
        CHECK(v.byte2() == 0xC6);
        CHECK(v.byte3() == 0x81);
        CHECK(v.byte4() == 0x01);
        CHECK(v.bytes()[0] == 0x85);
        CHECK(v.bytes()[3] == 0x01);
}

TEST_CASE("SdiVpid: setters individually update each byte") {
        SdiVpid v;
        v.setByte1(0x85);
        v.setByte2(0xC6);
        v.setByte3(0x81);
        v.setByte4(0x01);
        CHECK(v.byte1() == 0x85);
        CHECK(v.byte2() == 0xC6);
        CHECK(v.byte3() == 0x81);
        CHECK(v.byte4() == 0x01);
}

// ============================================================================
// isValid + linkStandard
// ============================================================================

TEST_CASE("SdiVpid::isValid: accepts every recognised payload identifier") {
        CHECK(SdiVpid(SdiVpid::Byte1_SL_SD,        0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_720,    0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080,   0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_DL_HD,        0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_3GA_720,   0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_3GA_1080,  0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_3GB,       0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160,   0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_1080,   0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_12G_2160,  0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_12G_1080,  0, 0, 0).isValid());
}

TEST_CASE("SdiVpid::isValid: rejects unknown byte 1 codes") {
        CHECK_FALSE(SdiVpid(0x00, 0, 0, 0).isValid());
        CHECK_FALSE(SdiVpid(0x42, 0, 0, 0).isValid());
        CHECK_FALSE(SdiVpid(0xFF, 0, 0, 0).isValid());
}

TEST_CASE("SdiVpid::linkStandard: maps every well-known byte 1 to SdiLinkStandard") {
        CHECK(SdiVpid(SdiVpid::Byte1_SL_SD,       0, 0, 0).linkStandard() == SdiLinkStandard::SL_SD);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_720,   0, 0, 0).linkStandard() == SdiLinkStandard::SL_HD);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_HD);
        CHECK(SdiVpid(SdiVpid::Byte1_DL_HD,       0, 0, 0).linkStandard() == SdiLinkStandard::DL_HD);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_3GA_720,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_3GA);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_3GA_1080, 0, 0, 0).linkStandard() == SdiLinkStandard::SL_3GA);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_3GB,      0, 0, 0).linkStandard() == SdiLinkStandard::SL_3GB);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_6G);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_1080,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_6G);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_12G_2160, 0, 0, 0).linkStandard() == SdiLinkStandard::SL_12G);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_12G_1080, 0, 0, 0).linkStandard() == SdiLinkStandard::SL_12G);
}

TEST_CASE("SdiVpid::linkStandard: unknown byte 1 returns Auto") {
        CHECK(SdiVpid(0xFF, 0, 0, 0).linkStandard() == SdiLinkStandard::Auto);
        CHECK(SdiVpid(0x42, 0, 0, 0).linkStandard() == SdiLinkStandard::Auto);
}

// ============================================================================
// wireFormat (byte 3 sampling + byte 4 bit depth)
// ============================================================================

TEST_CASE("SdiVpid::wireFormat: maps every sampling+bit-depth combo to SdiWireFormat") {
        auto wf = [](uint8_t samp, uint8_t depth) -> SdiWireFormat {
                SdiVpid v(SdiVpid::Byte1_SL_3GA_1080,
                          0,
                          static_cast<uint8_t>(samp & 0x0F),
                          static_cast<uint8_t>(depth & 0x03));
                return v.wireFormat();
        };
        CHECK(wf(SdiVpid::Sampling_YCbCr_422, SdiVpid::BitDepth_10) == SdiWireFormat::YCbCr_422_10);
        CHECK(wf(SdiVpid::Sampling_YCbCr_422, SdiVpid::BitDepth_12) == SdiWireFormat::YCbCr_422_12);
        CHECK(wf(SdiVpid::Sampling_YCbCr_444, SdiVpid::BitDepth_10) == SdiWireFormat::YCbCr_444_10);
        CHECK(wf(SdiVpid::Sampling_YCbCr_444, SdiVpid::BitDepth_12) == SdiWireFormat::YCbCr_444_12);
        CHECK(wf(SdiVpid::Sampling_RGB_444,   SdiVpid::BitDepth_10) == SdiWireFormat::RGB_444_10);
        CHECK(wf(SdiVpid::Sampling_RGB_444,   SdiVpid::BitDepth_12) == SdiWireFormat::RGB_444_12);
        CHECK(wf(SdiVpid::Sampling_RGBA_4444, SdiVpid::BitDepth_10) == SdiWireFormat::RGBA_444_10);
}

TEST_CASE("SdiVpid::wireFormat: 8-bit payloads are SDI-legal but not modelled as SdiWireFormat") {
        SdiVpid v(SdiVpid::Byte1_SL_SD, 0,
                  SdiVpid::Sampling_YCbCr_422, SdiVpid::BitDepth_8);
        CHECK(v.wireFormat() == SdiWireFormat::Auto);
        CHECK(v.bitDepth() == 8);
}

TEST_CASE("SdiVpid::wireFormat: 12-bit RGBA is not standardised — returns Auto") {
        SdiVpid v(SdiVpid::Byte1_SL_3GA_1080, 0,
                  SdiVpid::Sampling_RGBA_4444, SdiVpid::BitDepth_12);
        CHECK(v.wireFormat() == SdiWireFormat::Auto);
}

TEST_CASE("SdiVpid::wireFormat: 4:2:0 / YCbCrA samplings return Auto") {
        SdiVpid yuv420(SdiVpid::Byte1_SL_3GA_1080, 0,
                       SdiVpid::Sampling_YCbCr_420, SdiVpid::BitDepth_10);
        CHECK(yuv420.wireFormat() == SdiWireFormat::Auto);
}

// ============================================================================
// Picture rate (byte 2 bits 3-0, Table 2)
// ============================================================================

TEST_CASE("SdiVpid::pictureRate: maps every well-known Table 2 code") {
        auto rate = [](uint8_t code) -> FrameRate {
                return SdiVpid(SdiVpid::Byte1_SL_HD_1080,
                               static_cast<uint8_t>(code & 0x0F),
                               0, 0).pictureRate();
        };
        CHECK(rate(SdiVpid::Rate_23_98) == FrameRate(FrameRate::FPS_23_98));
        CHECK(rate(SdiVpid::Rate_24)    == FrameRate(FrameRate::FPS_24));
        CHECK(rate(SdiVpid::Rate_47_95) == FrameRate(FrameRate::FPS_47_95));
        CHECK(rate(SdiVpid::Rate_25)    == FrameRate(FrameRate::FPS_25));
        CHECK(rate(SdiVpid::Rate_29_97) == FrameRate(FrameRate::FPS_29_97));
        CHECK(rate(SdiVpid::Rate_30)    == FrameRate(FrameRate::FPS_30));
        CHECK(rate(SdiVpid::Rate_48)    == FrameRate(FrameRate::FPS_48));
        CHECK(rate(SdiVpid::Rate_50)    == FrameRate(FrameRate::FPS_50));
        CHECK(rate(SdiVpid::Rate_59_94) == FrameRate(FrameRate::FPS_59_94));
        CHECK(rate(SdiVpid::Rate_60)    == FrameRate(FrameRate::FPS_60));
        CHECK_FALSE(rate(SdiVpid::Rate_Unknown).isValid());
}

TEST_CASE("SdiVpid: Table 2 codes match the spec values exactly") {
        // Spot-check the codes whose pre-spec implementation got
        // shifted by one (25, 29.97, 30, 48 — see ST 352:2013 Table 2).
        CHECK(SdiVpid::Rate_25    == 0x5);
        CHECK(SdiVpid::Rate_29_97 == 0x6);
        CHECK(SdiVpid::Rate_30    == 0x7);
        CHECK(SdiVpid::Rate_48    == 0x8);
}

// ============================================================================
// Scan mode (byte 2 bits 7 and 6)
// ============================================================================

TEST_CASE("SdiVpid::videoScanMode: combines transport + picture bits correctly") {
        // 1080p59.94: PT=1, PS=1; rate code = 0xA (59.94)
        SdiVpid prog(SdiVpid::Byte1_SL_HD_1080, 0xC0 | SdiVpid::Rate_59_94, 0, 0);
        CHECK(prog.videoScanMode() == VideoScanMode::Progressive);
        CHECK(prog.isProgressiveTransport());
        CHECK(prog.isProgressivePicture());

        // 1080PsF29.97: PT=0, PS=1; rate code = 0x6 (29.97)
        SdiVpid psf(SdiVpid::Byte1_SL_HD_1080, 0x40 | SdiVpid::Rate_29_97, 0, 0);
        CHECK(psf.videoScanMode() == VideoScanMode::PsF);
        CHECK_FALSE(psf.isProgressiveTransport());
        CHECK(psf.isProgressivePicture());

        // 1080i59.94: PT=0, PS=0; rate code = 0x6 (29.97 frame rate)
        SdiVpid interl(SdiVpid::Byte1_SL_HD_1080, 0x00 | SdiVpid::Rate_29_97, 0, 0);
        CHECK(interl.videoScanMode() == VideoScanMode::Interlaced);
        CHECK_FALSE(interl.isProgressiveTransport());
        CHECK_FALSE(interl.isProgressivePicture());
}

// ============================================================================
// Aspect ratio (byte 3 bit 7)
// ============================================================================

TEST_CASE("SdiVpid::is16x9: reflects byte 3 bit 7") {
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0x80, 0).is16x9());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0x00, 0).is16x9());
}

// ============================================================================
// Bit depth (byte 4 bits 1-0)
// ============================================================================

TEST_CASE("SdiVpid::bitDepth: decodes 8 / 10 / 12 per ST 352 byte 4 [1:0]") {
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, SdiVpid::BitDepth_8 ).bitDepth() == 8);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, SdiVpid::BitDepth_10).bitDepth() == 10);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, SdiVpid::BitDepth_12).bitDepth() == 12);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x03).bitDepth() == 0); // reserved
}

TEST_CASE("SdiVpid: spec-mandated bit-depth code values") {
        CHECK(SdiVpid::BitDepth_8  == 0x0);
        CHECK(SdiVpid::BitDepth_10 == 0x1);
        CHECK(SdiVpid::BitDepth_12 == 0x2);
}

// ============================================================================
// Channel assignment (byte 4 bits 7-5)
// ============================================================================

TEST_CASE("SdiVpid::channelAssignment: extracts byte 4 bits 7-5") {
        // ch1 / single-link = 000b
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x00).channelAssignment() == 0);
        // ch4 of multi-channel = 011b → 0x60
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x60).channelAssignment() == 3);
        // ch8 = 111b → 0xE0
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0xE0).channelAssignment() == 7);
}

// ============================================================================
// encode() — high-level builder
// ============================================================================

TEST_CASE("SdiVpid::encode: 1080i59.94 over HD-SDI matches ST 292-1 / 1080-line / 29.97 / 4:2:2 10-bit") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080i59_94),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_HD);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_HD_1080);    // 85h, not 84h
        CHECK(v.pictureRateCode() == SdiVpid::Rate_29_97); // 6h
        CHECK(v.videoScanMode() == VideoScanMode::Interlaced);
        CHECK(v.wireFormat() == SdiWireFormat::YCbCr_422_10);
        CHECK(v.bitDepth() == 10);
        CHECK(v.is16x9());
        // byte 2 = interlaced (bits 7+6 = 0), reserved 0, rate = 6h
        CHECK(v.byte2() == 0x06);
        // byte 3 = 16:9 (bit 7 = 1), reserved 0, sampling = 0h (4:2:2)
        CHECK(v.byte3() == 0x80);
        // byte 4 = ch 0, reserved 0, depth = 1 (10-bit)
        CHECK(v.byte4() == 0x01);
}

TEST_CASE("SdiVpid::encode: 720p59.94 over HD-SDI picks the 720-line byte 1 code (84h)") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte720p59_94),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_HD);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_HD_720);  // 84h
        CHECK(v.pictureRateCode() == SdiVpid::Rate_59_94);
        CHECK(v.videoScanMode() == VideoScanMode::Progressive);
        CHECK(v.isProgressiveTransport());
        CHECK(v.isProgressivePicture());
        // byte 2 = PT=1, PS=1, reserved=0, rate=Ah → 0b1100_1010 = 0xCA
        CHECK(v.byte2() == 0xCA);
}

TEST_CASE("SdiVpid::encode: 3G Level A 720-line picks 88h, 1080-line picks 89h") {
        SdiVpid v720 = SdiVpid::encode(VideoFormat(VideoFormat::Smpte720p59_94),
                                       SdiWireFormat::YCbCr_422_10,
                                       SdiLinkStandard::SL_3GA);
        CHECK(v720.byte1() == SdiVpid::Byte1_SL_3GA_720);  // 88h

        SdiVpid v1080 = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080p59_94),
                                        SdiWireFormat::YCbCr_422_10,
                                        SdiLinkStandard::SL_3GA);
        CHECK(v1080.byte1() == SdiVpid::Byte1_SL_3GA_1080); // 89h
}

TEST_CASE("SdiVpid::encode: 1080p59.94 RGB 4:4:4 12-bit over 6G-SDI") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080p59_94),
                                    SdiWireFormat::RGB_444_12,
                                    SdiLinkStandard::SL_6G);
        // 1080p59.94 on 6G-SDI uses the 1080-line code (0xC1 per
        // ST 2081-10 Mode 2), not the 2160-line code.
        CHECK(v.byte1() == SdiVpid::Byte1_SL_6G_1080);
        CHECK(v.pictureRateCode() == SdiVpid::Rate_59_94);
        CHECK(v.videoScanMode() == VideoScanMode::Progressive);
        CHECK(v.samplingCode() == SdiVpid::Sampling_RGB_444);
        CHECK(v.bitDepth() == 12);
        CHECK(v.wireFormat() == SdiWireFormat::RGB_444_12);
}

TEST_CASE("SdiVpid::encode: 1080psf23.98 over HD-SDI flips picture-progressive but not transport") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080psf23_98),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_HD);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_HD_1080);
        CHECK(v.pictureRateCode() == SdiVpid::Rate_23_98);
        CHECK(v.videoScanMode() == VideoScanMode::PsF);
        CHECK_FALSE(v.isProgressiveTransport());
        CHECK(v.isProgressivePicture());
        // byte 2 = PT=0, PS=1, reserved=0, rate=2h → 0b0100_0010 = 0x42
        CHECK(v.byte2() == 0x42);
}

TEST_CASE("SdiVpid::encode: 12G-SDI 2160-line picks ST 2082-10 Mode 1 byte 1") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p60),
                                    SdiWireFormat::YCbCr_444_12,
                                    SdiLinkStandard::SL_12G);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_12G_2160);
        CHECK(v.linkStandard() == SdiLinkStandard::SL_12G);
        CHECK(v.wireFormat() == SdiWireFormat::YCbCr_444_12);
}

TEST_CASE("SdiVpid::encode: 12G-SDI 1080-line HFR picks ST 2082-10 Mode 2 byte 1") {
        // 1080p120 over 12G-SDI = ST 2082-10 Mode 2 → byte 1 = 0xCF.
        VideoFormat fmt(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_120),
                        VideoScanMode::Progressive);
        SdiVpid v = SdiVpid::encode(fmt, SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_12G);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_12G_1080);
}

TEST_CASE("SdiVpid::encode: 24G-SDI byte 1 not defined in any available spec → Unknown") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p60),
                                    SdiWireFormat::YCbCr_444_12,
                                    SdiLinkStandard::SL_24G);
        CHECK(v.byte1() == SdiVpid::Byte1_Unknown);
        CHECK_FALSE(v.isValid());
}

TEST_CASE("SdiVpid::encode: Auto standard yields an invalid byte 1") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080i59_94),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::Auto);
        CHECK_FALSE(v.isValid());
        CHECK(v.byte1() == 0);
}

TEST_CASE("SdiVpid::encode: quad-link 2SI promotes to SL_3GA_1080 on byte 1") {
        // Each sub-image link on a quad-link 2SI signal carries its
        // own VPID stamped as if it were a 3G Level A single link;
        // the encoder produces the 3G Level A 1080-line code so the
        // per-link VPID is correct.
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p59_94),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::QL_3G_2SI);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_3GA_1080); // 89h
}

// ============================================================================
// String round-trip
// ============================================================================

TEST_CASE("SdiVpid::toString emits colon-separated 2-digit lower-case hex") {
        SdiVpid v(0x85, 0x06, 0x80, 0x01);
        CHECK(v.toString() == String("85:06:80:01"));

        SdiVpid zero;
        CHECK(zero.toString() == String("00:00:00:00"));
}

TEST_CASE("SdiVpid::fromString round-trips every byte combination") {
        const SdiVpid cases[] = {
                SdiVpid(),
                SdiVpid(0x85, 0x06, 0x80, 0x01),
                SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080i59_94),
                                SdiWireFormat::YCbCr_422_10, SdiLinkStandard::SL_HD),
                SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p60),
                                SdiWireFormat::RGB_444_12, SdiLinkStandard::SL_12G),
        };
        for (const auto &original : cases) {
                Result<SdiVpid> r = SdiVpid::fromString(original.toString());
                REQUIRE(r.second().isOk());
                CHECK(r.first() == original);
        }
}

TEST_CASE("SdiVpid::fromString is case-insensitive on hex digits") {
        Result<SdiVpid> r = SdiVpid::fromString(String("AB:CD:EF:01"));
        REQUIRE(r.second().isOk());
        CHECK(r.first().byte1() == 0xAB);
        CHECK(r.first().byte2() == 0xCD);
        CHECK(r.first().byte3() == 0xEF);
        CHECK(r.first().byte4() == 0x01);
}

TEST_CASE("SdiVpid::fromString rejects malformed input") {
        CHECK(SdiVpid::fromString(String()).second() == Error::InvalidArgument);
        CHECK(SdiVpid::fromString(String("85:77")).second() == Error::InvalidArgument);             // too few tokens
        CHECK(SdiVpid::fromString(String("85:06:80:01:00")).second() == Error::InvalidArgument);   // too many tokens
        CHECK(SdiVpid::fromString(String("85:GG:80:01")).second() == Error::InvalidArgument);       // bad hex
        CHECK(SdiVpid::fromString(String("85::80:01")).second() == Error::InvalidArgument);         // empty token
}

// ============================================================================
// DataStream round-trip + Variant integration
// ============================================================================

TEST_CASE("SdiVpid: DataStream operators round-trip a populated VPID") {
        SdiVpid original(0x85, 0x06, 0x80, 0x01);
        Buffer         storage(64);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
                REQUIRE(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        SdiVpid round(0xFF, 0xFF, 0xFF, 0xFF);
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
}

TEST_CASE("SdiVpid: DataType registry assigns DataTypeSdiVpid = 0x69") {
        DataType dt = DataType::of<SdiVpid>();
        REQUIRE(dt.isValid());
        CHECK(dt.id() == DataTypeSdiVpid);
        CHECK(dt.version() == 1u);
        CHECK(String(dt.name()) == "SdiVpid");
}

TEST_CASE("SdiVpid: round-trips through Variant") {
        SdiVpid original(0x85, 0x06, 0x80, 0x01);
        Variant v;
        v.set(original);
        CHECK(v.type() == DataTypeSdiVpid);
        SdiVpid out = v.get<SdiVpid>();
        CHECK(out == original);
}

// ============================================================================
// isCurrentVersion (byte 1 bit 7)
// ============================================================================

TEST_CASE("SdiVpid::isCurrentVersion: tracks byte 1 bit 7") {
        // Every registered Byte1_* constant has bit 7 set.
        CHECK(SdiVpid(SdiVpid::Byte1_SL_SD,       0, 0, 0).isCurrentVersion());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080,  0, 0, 0).isCurrentVersion());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_3GA_1080, 0, 0, 0).isCurrentVersion());

        // A pre-2008 historical code (byte 1 bit 7 = 0) — per Annex C.
        CHECK_FALSE(SdiVpid(0x01, 0, 0, 0).isCurrentVersion());
        // All-zero VPID also reads as legacy (bit 7 = 0).
        CHECK_FALSE(SdiVpid().isCurrentVersion());
}

// ============================================================================
// validate (field-range checks)
// ============================================================================

TEST_CASE("SdiVpid::validate: passes a well-formed canonical VPID") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080i59_94),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_HD);
        CHECK(v.validate() == Error::Ok);
}

TEST_CASE("SdiVpid::validate: rejects an unknown byte 1") {
        CHECK(SdiVpid().validate() == Error::InvalidArgument);
        CHECK(SdiVpid(0x42, 0x06, 0x80, 0x01).validate() == Error::InvalidArgument);
}

TEST_CASE("SdiVpid::validate: accepts picture-rate codes that are defined in any schema") {
        // Codes 1h and Ch-Fh were Reserved in ST 352:2013 Table 2 but
        // are defined in ST 2081-10 / 2082-10 Table 4 as the HFR
        // codes (95.90, 96, 100, 119.88, 120 fps).  validate() is
        // permissive — it accepts any code that's defined somewhere
        // in the spec ecosystem.
        for (uint8_t code : {uint8_t(0x01), uint8_t(0x0C), uint8_t(0x0F)}) {
                SdiVpid v(SdiVpid::Byte1_SL_HD_1080, code, 0x80, 0x01);
                CAPTURE(code);
                CHECK(v.validate() == Error::Ok);
        }
}

TEST_CASE("SdiVpid::validate: rejects reserved sampling codes") {
        // Codes Bh, Ch, Dh, Fh are reserved per Table 3 (7h-Ah, Eh
        // are defined).
        for (uint8_t reserved : {0x0B, 0x0C, 0x0D, 0x0F}) {
                SdiVpid v(SdiVpid::Byte1_SL_HD_1080, 0x06, static_cast<uint8_t>(0x80 | reserved), 0x01);
                CAPTURE(reserved);
                CHECK(v.validate() == Error::InvalidArgument);
        }
}

TEST_CASE("SdiVpid::validate: accepts bit-depth code 3h (12-bit Full Range in 2081-10/2082-10)") {
        // Code 3h was Reserved in ST 352:2013 but is "12-bit Full
        // Range" in ST 2081-10 / 2082-10 1080-line modes.  validate()
        // accepts it across the board; callers needing strict
        // ST 352:2013 behaviour can inspect @ref bitDepthCode and
        // @ref byte1 directly.
        SdiVpid v(SdiVpid::Byte1_SL_HD_1080, 0x06, 0x80, 0x03);
        CHECK(v.validate() == Error::Ok);
}

TEST_CASE("SdiVpid::validate: accepts ST 2048-2 FS / YCbCr+D / XYZ samplings") {
        // 7h, 8h, 9h, Ah, Eh are defined (not reserved).
        for (uint8_t defined : {0x07, 0x08, 0x09, 0x0A, 0x0E}) {
                SdiVpid v(SdiVpid::Byte1_SL_HD_1080, 0x06, static_cast<uint8_t>(0x80 | defined), 0x01);
                CAPTURE(defined);
                CHECK(v.validate() == Error::Ok);
        }
}

// ============================================================================
// fromUint32BE / toUint32BE
// ============================================================================

TEST_CASE("SdiVpid::toUint32BE: packs byte 1 in MSB, byte 4 in LSB") {
        SdiVpid v(0x89, 0xCA, 0x80, 0x01);
        CHECK(v.toUint32BE() == 0x89CA8001u);

        CHECK(SdiVpid().toUint32BE() == 0x00000000u);
        CHECK(SdiVpid(0xFF, 0xFF, 0xFF, 0xFF).toUint32BE() == 0xFFFFFFFFu);
}

TEST_CASE("SdiVpid::fromUint32BE: inverse of toUint32BE") {
        SdiVpid v = SdiVpid::fromUint32BE(0x89CA8001u);
        CHECK(v.byte1() == 0x89);
        CHECK(v.byte2() == 0xCA);
        CHECK(v.byte3() == 0x80);
        CHECK(v.byte4() == 0x01);

        for (uint32_t pattern : {0x00000000u, 0x12345678u, 0x89CA8001u, 0xDEADBEEFu, 0xFFFFFFFFu}) {
                CAPTURE(pattern);
                CHECK(SdiVpid::fromUint32BE(pattern).toUint32BE() == pattern);
        }
}

// ============================================================================
// Channel assignment setter + encode overload
// ============================================================================

TEST_CASE("SdiVpid::setChannelAssignment: writes byte 4 [7:5], preserves bit depth") {
        SdiVpid v(SdiVpid::Byte1_SL_HD_1080, 0x06, 0x80, SdiVpid::BitDepth_10);
        v.setChannelAssignment(3);
        CHECK(v.channelAssignment() == 3);
        CHECK(v.bitDepthCode() == SdiVpid::BitDepth_10); // unchanged
        // bit pattern: 011<<5 | 01 = 0x61
        CHECK(v.byte4() == 0x61);

        v.setChannelAssignment(7);
        CHECK(v.channelAssignment() == 7);
        CHECK(v.bitDepthCode() == SdiVpid::BitDepth_10);

        // High bits beyond 3 should be masked off.
        v.setChannelAssignment(0xFF);
        CHECK(v.channelAssignment() == 7);
}

TEST_CASE("SdiVpid::encode(..., channelIndex): stamps the channel field") {
        // Quad-link 2SI of UHD p59.94 — each sub-image link carries
        // a 3G Level A VPID with channel = 0..3.
        for (int ch = 0; ch <= 3; ++ch) {
                SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p59_94),
                                            SdiWireFormat::YCbCr_422_10,
                                            SdiLinkStandard::QL_3G_2SI, ch);
                CAPTURE(ch);
                CHECK(v.byte1() == SdiVpid::Byte1_SL_3GA_1080);
                CHECK(static_cast<int>(v.channelAssignment()) == ch);
        }
}

// ============================================================================
// Recommended VANC line lookup (ST 352:2013 §6.2)
// ============================================================================

TEST_CASE("SdiVpid::recommendedAncLine: 525-line interlaced returns 13 / 276") {
        VideoFormat fmt(VideoFormat::Smpte486i59_94);
        CHECK(SdiVpid::recommendedAncLine(fmt, 1) == 13);
        CHECK(SdiVpid::recommendedAncLine(fmt, 2) == 276);
}

TEST_CASE("SdiVpid::recommendedAncLine: 625-line interlaced returns 9 / 322") {
        VideoFormat fmt(VideoFormat::Smpte576i50);
        CHECK(SdiVpid::recommendedAncLine(fmt, 1) == 9);
        CHECK(SdiVpid::recommendedAncLine(fmt, 2) == 322);
}

TEST_CASE("SdiVpid::recommendedAncLine: 750-line progressive returns 10") {
        VideoFormat fmt(VideoFormat::Smpte720p59_94);
        CHECK(SdiVpid::recommendedAncLine(fmt, 1) == 10);
        CHECK(SdiVpid::recommendedAncLine(fmt, 2) == 0); // no field 2
}

TEST_CASE("SdiVpid::recommendedAncLine: 1125-line interlaced returns 10 / 572") {
        VideoFormat fmt(VideoFormat::Smpte1080i59_94);
        CHECK(SdiVpid::recommendedAncLine(fmt, 1) == 10);
        CHECK(SdiVpid::recommendedAncLine(fmt, 2) == 572);
}

TEST_CASE("SdiVpid::recommendedAncLine: 1125-line PsF returns 10 / 572 (treated like interlaced)") {
        VideoFormat fmt(VideoFormat::Smpte1080psf23_98);
        CHECK(SdiVpid::recommendedAncLine(fmt, 1) == 10);
        CHECK(SdiVpid::recommendedAncLine(fmt, 2) == 572);
}

TEST_CASE("SdiVpid::recommendedAncLine: 1125-line progressive returns 10 (no field 2)") {
        VideoFormat fmt(VideoFormat::Smpte1080p59_94);
        CHECK(SdiVpid::recommendedAncLine(fmt, 1) == 10);
        CHECK(SdiVpid::recommendedAncLine(fmt, 2) == 0);
}

TEST_CASE("SdiVpid::recommendedAncLine: UHD / 8K / invalid return 0") {
        CHECK(SdiVpid::recommendedAncLine(VideoFormat(VideoFormat::Smpte2160p59_94), 1) == 0);
        CHECK(SdiVpid::recommendedAncLine(VideoFormat(VideoFormat::Smpte4320p60), 1) == 0);
        CHECK(SdiVpid::recommendedAncLine(VideoFormat(), 1) == 0);
}

TEST_CASE("SdiVpid::recommendedAncLine: invalid field index returns 0") {
        VideoFormat fmt(VideoFormat::Smpte1080i59_94);
        CHECK(SdiVpid::recommendedAncLine(fmt, 0) == 0);
        CHECK(SdiVpid::recommendedAncLine(fmt, 3) == 0);
}

// ============================================================================
// ANC packet round-trip (St291Packet)
// ============================================================================

TEST_CASE("SdiVpid::toSt291Packet: wraps the 4 bytes in a DID=0x41 / SDID=0x01 / DC=4 packet") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080i59_94),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_HD);
        St291Packet p = v.toSt291Packet(10, /*fieldB=*/ false);

        CHECK(p.did() == 0x41);
        CHECK(p.sdid() == 0x01);
        CHECK(p.dataCount() == 0x04);

        const List<uint16_t> udw = p.udw();
        REQUIRE(udw.size() == 4);
        // Mask off parity bits (upper 2 of each 10-bit word).
        CHECK(static_cast<uint8_t>(udw.at(0) & 0xFF) == v.byte1());
        CHECK(static_cast<uint8_t>(udw.at(1) & 0xFF) == v.byte2());
        CHECK(static_cast<uint8_t>(udw.at(2) & 0xFF) == v.byte3());
        CHECK(static_cast<uint8_t>(udw.at(3) & 0xFF) == v.byte4());

        CHECK(p.line() == 10);
        CHECK_FALSE(p.fieldB());

        // The format dispatch should resolve to AncFormat::Vpid.
        AncFormat fmt = p.packet().format();
        CHECK(fmt == AncFormat(AncFormat::Vpid));
}

TEST_CASE("SdiVpid::toSt291Packet: fieldB flag and line round-trip") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte1080i59_94),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_HD);
        St291Packet p = v.toSt291Packet(572, /*fieldB=*/ true);
        CHECK(p.line() == 572);
        CHECK(p.fieldB());
}

TEST_CASE("SdiVpid::fromSt291Packet: round-trips through toSt291Packet") {
        const SdiVpid cases[] = {
                SdiVpid(0x85, 0x06, 0x80, 0x01),
                SdiVpid::encode(VideoFormat(VideoFormat::Smpte720p59_94),
                                SdiWireFormat::YCbCr_422_10, SdiLinkStandard::SL_HD),
                SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p60),
                                SdiWireFormat::RGB_444_12, SdiLinkStandard::SL_12G),
                SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p59_94),
                                SdiWireFormat::YCbCr_422_10, SdiLinkStandard::QL_3G_2SI, 2),
        };
        for (const auto &original : cases) {
                St291Packet     p   = original.toSt291Packet(10);
                Result<SdiVpid> dec = SdiVpid::fromSt291Packet(p);
                REQUIRE(dec.second().isOk());
                CHECK(dec.first() == original);
        }
}

TEST_CASE("SdiVpid::fromSt291Packet: rejects wrong DID/SDID") {
        // CEA-708 CDP packet — DID/SDID = 0x61/0x01.
        List<uint16_t> udw;
        for (int i = 0; i < 4; ++i) udw.pushToBack(0xAA);
        St291Packet other = St291Packet::buildRaw(0x61, 0x01, udw, 9);
        CHECK(SdiVpid::fromSt291Packet(other).second() == Error::InvalidArgument);

        // Right DID, wrong SDID.
        St291Packet wrongSdid = St291Packet::buildRaw(0x41, 0x05, udw, 9);
        CHECK(SdiVpid::fromSt291Packet(wrongSdid).second() == Error::InvalidArgument);
}

TEST_CASE("SdiVpid::fromSt291Packet: rejects wrong data count") {
        List<uint16_t> udw;
        // 5-byte payload — wrong shape.
        for (int i = 0; i < 5; ++i) udw.pushToBack(0xAA);
        St291Packet pkt = St291Packet::buildRaw(0x41, 0x01, udw, 10);
        CHECK(SdiVpid::fromSt291Packet(pkt).second() == Error::InvalidArgument);
}

// ============================================================================
// AncFormat::Vpid registration
// ============================================================================

TEST_CASE("AncFormat::Vpid is registered with DID=0x41, SDID=0x01") {
        AncFormat fmt(AncFormat::Vpid);
        CHECK(fmt.id() == AncFormat::Vpid);
        CHECK(fmt.st291Did() == 0x41);
        CHECK(fmt.st291Sdid() == 0x01);
        CHECK(fmt.category() == AncCategory::PayloadId);
        CHECK(fmt.name() == String("Vpid"));
}

TEST_CASE("AncFormat::fromSt291DidSdid(0x41, 0x01) returns Vpid") {
        AncFormat fmt = AncFormat::fromSt291DidSdid(0x41, 0x01);
        CHECK(fmt.id() == AncFormat::Vpid);
}

// ============================================================================
// Extended (ST 2081-10 / ST 2082-10) schema accessors
// ============================================================================

TEST_CASE("SdiVpid::isExtendedSchema: true for 6G/12G byte 1 codes only") {
        CHECK_FALSE(SdiVpid().isExtendedSchema());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0).isExtendedSchema());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_3GA_1080, 0, 0, 0).isExtendedSchema());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_3GB, 0, 0, 0).isExtendedSchema());

        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160,  0, 0, 0).isExtendedSchema());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_1080,  0, 0, 0).isExtendedSchema());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_12G_2160, 0, 0, 0).isExtendedSchema());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_12G_1080, 0, 0, 0).isExtendedSchema());
}

TEST_CASE("SdiVpid::transferCharacteristic: decodes byte 2 [5:4] for 6G/12G") {
        // SDR on 6G-SDI 2160-line
        SdiVpid sdr(SdiVpid::Byte1_SL_6G_2160,
                    0xC0 | (SdiVpid::Transfer_SDR << 4) | SdiVpid::Rate_60,
                    0, 0);
        CHECK(sdr.transferCharacteristic() == TransferCharacteristics::BT709);

        // HLG
        SdiVpid hlg(SdiVpid::Byte1_SL_6G_2160,
                    0xC0 | (SdiVpid::Transfer_HLG << 4) | SdiVpid::Rate_60,
                    0, 0);
        CHECK(hlg.transferCharacteristic() == TransferCharacteristics::ARIB_STD_B67);

        // PQ
        SdiVpid pq(SdiVpid::Byte1_SL_6G_2160,
                   0xC0 | (SdiVpid::Transfer_PQ << 4) | SdiVpid::Rate_60,
                   0, 0);
        CHECK(pq.transferCharacteristic() == TransferCharacteristics::SMPTE2084);

        // Unspecified
        SdiVpid uns(SdiVpid::Byte1_SL_6G_2160,
                    0xC0 | (SdiVpid::Transfer_Unspecified << 4) | SdiVpid::Rate_60,
                    0, 0);
        CHECK(uns.transferCharacteristic() == TransferCharacteristics::Unspecified);

        // For ST 352:2013 (HD/3G) byte 1 codes, the transfer field
        // doesn't exist in the spec — accessor returns Unspecified.
        SdiVpid hd(SdiVpid::Byte1_SL_HD_1080,
                   0xC0 | (SdiVpid::Transfer_PQ << 4) | SdiVpid::Rate_29_97,
                   0, 0);
        CHECK(hd.transferCharacteristic() == TransferCharacteristics::Unspecified);
}

TEST_CASE("SdiVpid::colorimetry: decodes byte 3 [5:4] for 6G/12G") {
        SdiVpid rec709(SdiVpid::Byte1_SL_6G_2160, 0,
                       static_cast<uint8_t>(0x80 | (SdiVpid::Colorimetry_Rec709 << 4)), 0);
        CHECK(rec709.colorimetry() == ColorPrimaries::BT709);

        SdiVpid uhdtv(SdiVpid::Byte1_SL_6G_2160, 0,
                      static_cast<uint8_t>(0x80 | (SdiVpid::Colorimetry_UHDTV << 4)), 0);
        CHECK(uhdtv.colorimetry() == ColorPrimaries::BT2020);

        SdiVpid vanc(SdiVpid::Byte1_SL_6G_2160, 0,
                     static_cast<uint8_t>(0x80 | (SdiVpid::Colorimetry_VANC << 4)), 0);
        CHECK(vanc.colorimetry() == ColorPrimaries::Unspecified);

        SdiVpid unk(SdiVpid::Byte1_SL_6G_2160, 0,
                    static_cast<uint8_t>(0x80 | (SdiVpid::Colorimetry_Unknown << 4)), 0);
        CHECK(unk.colorimetry() == ColorPrimaries::Unspecified);

        // Non-extended schemas return Unspecified.
        SdiVpid hd(SdiVpid::Byte1_SL_HD_1080, 0, 0x80, 0);
        CHECK(hd.colorimetry() == ColorPrimaries::Unspecified);
}

TEST_CASE("SdiVpid::isIctcp: decodes byte 4 bit 4") {
        SdiVpid ycbcr(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x01); // bit 4 = 0
        CHECK_FALSE(ycbcr.isIctcp());

        SdiVpid ictcp(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x11); // bit 4 = 1
        CHECK(ictcp.isIctcp());
}

TEST_CASE("SdiVpid: HDR setters update the right bits") {
        SdiVpid v(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0);

        v.setTransferCode(SdiVpid::Transfer_PQ);
        CHECK(v.transferCode() == SdiVpid::Transfer_PQ);
        CHECK(v.transferCharacteristic() == TransferCharacteristics::SMPTE2084);

        v.setColorimetryCode(SdiVpid::Colorimetry_UHDTV);
        CHECK(v.colorimetryCode() == SdiVpid::Colorimetry_UHDTV);
        CHECK(v.colorimetry() == ColorPrimaries::BT2020);

        v.setIctcp(true);
        CHECK(v.isIctcp());
        v.setIctcp(false);
        CHECK_FALSE(v.isIctcp());

        v.setBitDepthCode(SdiVpid::BitDepth_12_Full);
        CHECK(v.bitDepthCode() == 0x3);
        CHECK(v.bitDepth() == 12);
        CHECK(v.isFullRange());
}

TEST_CASE("SdiVpid::bitDepth: schema-aware decoding") {
        // ST 352:2013 schema (HD): 0=8, 1=10, 2=12, 3=invalid
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x0).bitDepth() == 8);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x1).bitDepth() == 10);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x2).bitDepth() == 12);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x3).bitDepth() == 0);

        // Extended schema (6G/12G): 0=10-Full, 1=10, 2=12, 3=12-Full
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x0).bitDepth() == 10);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x1).bitDepth() == 10);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x2).bitDepth() == 12);
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x3).bitDepth() == 12);
}

TEST_CASE("SdiVpid::isFullRange: only meaningful in extended schema") {
        // ST 352:2013: range is implicit (always narrow).
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x0).isFullRange());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0x1).isFullRange());

        // Extended schema: codes 0 (10-bit Full) and 3 (12-bit Full)
        // are full-range; 1 (10-bit) and 2 (12-bit) are narrow.
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x0).isFullRange());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x1).isFullRange());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x2).isFullRange());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0x3).isFullRange());
}

// ============================================================================
// HFR picture rate codes (ST 2081-10 / 2082-10 extensions)
// ============================================================================

TEST_CASE("SdiVpid::pictureRate: decodes HFR codes (ST 2081-10 / 2082-10)") {
        auto rate = [](uint8_t code) -> FrameRate {
                return SdiVpid(SdiVpid::Byte1_SL_6G_1080,
                               static_cast<uint8_t>(0xC0 | code),  // PT=1, PS=1
                               0, 0).pictureRate();
        };
        CHECK(rate(SdiVpid::Rate_95_90)  == FrameRate(FrameRate::FPS_95_90));
        CHECK(rate(SdiVpid::Rate_96)     == FrameRate(FrameRate::FPS_96));
        CHECK(rate(SdiVpid::Rate_100)    == FrameRate(FrameRate::FPS_100));
        CHECK(rate(SdiVpid::Rate_119_88) == FrameRate(FrameRate::FPS_119_88));
        CHECK(rate(SdiVpid::Rate_120)    == FrameRate(FrameRate::FPS_120));
}

TEST_CASE("SdiVpid::encodePictureRateCode: encodes HFR rates") {
        CHECK(SdiVpid::encodePictureRateCode(FrameRate(FrameRate::FPS_95_90))  == SdiVpid::Rate_95_90);
        CHECK(SdiVpid::encodePictureRateCode(FrameRate(FrameRate::FPS_96))     == SdiVpid::Rate_96);
        CHECK(SdiVpid::encodePictureRateCode(FrameRate(FrameRate::FPS_100))    == SdiVpid::Rate_100);
        CHECK(SdiVpid::encodePictureRateCode(FrameRate(FrameRate::FPS_119_88)) == SdiVpid::Rate_119_88);
        CHECK(SdiVpid::encodePictureRateCode(FrameRate(FrameRate::FPS_120))    == SdiVpid::Rate_120);
}

// ============================================================================
// 6G-SDI mode split (2160-line vs 1080-line)
// ============================================================================

TEST_CASE("SdiVpid::encode: 6G-SDI 2160-line picks ST 2081-10 Mode 1 byte 1 (C0h)") {
        SdiVpid v = SdiVpid::encode(VideoFormat(VideoFormat::Smpte2160p30),
                                    SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_6G);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_6G_2160);
        CHECK(v.byte1() == 0xC0);
}

TEST_CASE("SdiVpid::encode: 6G-SDI 1080-line picks ST 2081-10 Mode 2 byte 1 (C1h)") {
        VideoFormat fmt(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_120),
                        VideoScanMode::Progressive);
        SdiVpid v = SdiVpid::encode(fmt, SdiWireFormat::YCbCr_422_10,
                                    SdiLinkStandard::SL_6G);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_6G_1080);
        CHECK(v.byte1() == 0xC1);
        CHECK(v.pictureRateCode() == SdiVpid::Rate_120);
}

// ============================================================================
// Annex B.1 SD schema (byte 1 = 0x81)
// ============================================================================

TEST_CASE("SdiVpid::isSdSchema: true for byte 1 = 0x81 only") {
        CHECK(SdiVpid(SdiVpid::Byte1_SL_SD, 0, 0, 0).isSdSchema());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_HD_1080, 0, 0, 0).isSdSchema());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_6G_2160, 0, 0, 0).isSdSchema());
        CHECK_FALSE(SdiVpid().isSdSchema());
}

TEST_CASE("SdiVpid::videoScanMode: SD schema uses bit 6 alone (Annex B.1)") {
        // SD interlaced: byte 2 bit 6 = 0
        SdiVpid sdi(SdiVpid::Byte1_SL_SD, SdiVpid::Rate_29_97, 0x80, 0);
        CHECK(sdi.videoScanMode() == VideoScanMode::Interlaced);

        // SD progressive: byte 2 bit 6 = 1
        SdiVpid sdp(SdiVpid::Byte1_SL_SD, 0x40 | SdiVpid::Rate_29_97, 0x80, 0);
        CHECK(sdp.videoScanMode() == VideoScanMode::Progressive);

        // For SD, byte 2 bit 7 being set doesn't change the
        // interpretation — Annex B.1 marks it Reserved.
        SdiVpid weird(SdiVpid::Byte1_SL_SD, 0x80 | SdiVpid::Rate_29_97, 0x80, 0);
        CHECK(weird.videoScanMode() == VideoScanMode::Interlaced);
}

TEST_CASE("SdiVpid::sdHas960Samples: SD horizontal sample count flag") {
        // 720 samples: byte 3 bit 6 = 0
        SdiVpid s720(SdiVpid::Byte1_SL_SD, SdiVpid::Rate_29_97, 0x80, 0);
        CHECK_FALSE(s720.sdHas960Samples());

        // 960 samples: byte 3 bit 6 = 1
        SdiVpid s960(SdiVpid::Byte1_SL_SD, SdiVpid::Rate_29_97, 0xC0, 0);
        CHECK(s960.sdHas960Samples());
}

TEST_CASE("SdiVpid::has2048Samples: extended-schema horizontal sample count flag") {
        SdiVpid p1920(SdiVpid::Byte1_SL_6G_1080, 0, 0x80, 0);
        CHECK_FALSE(p1920.has2048Samples());

        SdiVpid p2048(SdiVpid::Byte1_SL_6G_1080, 0, 0xC0, 0);
        CHECK(p2048.has2048Samples());
}

// ============================================================================
// Annex C legacy (pre-2008) codes
// ============================================================================

TEST_CASE("SdiVpid::isAnnexC: true for byte 1 0x01-0x06 only") {
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_BT601,  0, 0, 0).isAnnexC());
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_BT1358, 0, 0, 0).isAnnexC());
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST347,  0, 0, 0).isAnnexC());
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST274,  0, 0, 0).isAnnexC());
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST296,  0, 0, 0).isAnnexC());
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST349,  0, 0, 0).isAnnexC());

        // Modern codes are not Annex C.
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_SD,        0, 0, 0).isAnnexC());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_HD_1080,   0, 0, 0).isAnnexC());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_SL_6G_2160,   0, 0, 0).isAnnexC());

        // Default-constructed and unknown codes aren't Annex C either.
        CHECK_FALSE(SdiVpid().isAnnexC());
        CHECK_FALSE(SdiVpid(0x42, 0, 0, 0).isAnnexC());
}

TEST_CASE("SdiVpid::isValid: Annex C codes are recognised as valid (with caveat)") {
        // isValid() accepts the legacy codes so they're not dismissed
        // as garbage on the wire — but the field decoders are not
        // Annex C-aware, see class docs.
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST274, 0, 0, 0).isValid());
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST296, 0, 0, 0).isValid());
}

TEST_CASE("SdiVpid::isCurrentVersion: false for Annex C, true for modern") {
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_AnnexC_ST274, 0, 0, 0).isCurrentVersion());
        CHECK_FALSE(SdiVpid(SdiVpid::Byte1_AnnexC_BT601, 0, 0, 0).isCurrentVersion());

        CHECK(SdiVpid(SdiVpid::Byte1_SL_HD_1080,   0, 0, 0).isCurrentVersion());
        CHECK(SdiVpid(SdiVpid::Byte1_SL_6G_2160,   0, 0, 0).isCurrentVersion());
}

TEST_CASE("SdiVpid::linkStandard: Annex C codes promote to modern SdiLinkStandard") {
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_BT601,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_SD);
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_BT1358, 0, 0, 0).linkStandard() == SdiLinkStandard::SL_SD);
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST347,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_SD);
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST274,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_HD);
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST296,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_HD);
        CHECK(SdiVpid(SdiVpid::Byte1_AnnexC_ST349,  0, 0, 0).linkStandard() == SdiLinkStandard::SL_HD);
}

TEST_CASE("SdiVpid::encode: SD signal omits the byte 2 progressive-transport bit") {
        // 525I59.94 SD interlaced video — byte 2 bit 7 must stay 0
        // (Annex B.1 Reserved), bit 6 carries the I/P flag (0 here).
        VideoFormat fmt(VideoFormat::Smpte486i59_94);
        SdiVpid v = SdiVpid::encode(fmt, SdiWireFormat::Auto,
                                    SdiLinkStandard::SL_SD);
        CHECK(v.byte1() == SdiVpid::Byte1_SL_SD);
        // bit 7 (would be I/P transport in Table 1b) stays 0 for SD.
        CHECK((v.byte2() & 0x80) == 0);
        // bit 6 (I/P picture) is 0 for interlaced.
        CHECK((v.byte2() & 0x40) == 0);
        CHECK(v.pictureRateCode() == SdiVpid::Rate_29_97);
        CHECK(v.videoScanMode() == VideoScanMode::Interlaced);
}
