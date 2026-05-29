/**
 * @file      st2110trafficcalc.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK && PROMEKI_ENABLE_PROAV
#include <promeki/enums_rtp.h>
#include <promeki/enums_st2110.h>
#include <promeki/st2110trafficcalc.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

St2110TrafficCalc::Result calc1080p(const FrameRate &rate, const RtpSenderType &type) {
        VideoFormat fmt(VideoFormat::Raster_HD, rate, VideoScanMode::Progressive);
        return St2110TrafficCalc::compute(fmt, St2110Sampling::YCbCr422, St2110Depth::Bits10, type);
}

} // namespace

TEST_CASE("St2110TrafficCalc: 1080p 4:2:2/10 reproduces the standard's packet example") {
        // ST 2110-21 §7.1.1 Note 1: "8 packets is just over 2 lines of
        // 4:2:2/10 samples in a 1920 sample width format" — i.e. 4
        // packets per line at the Standard UDP Size Limit, so 1080
        // lines → 4320 packets/frame.
        const St2110TrafficCalc::Result r =
                calc1080p(FrameRate(FrameRate::FPS_60), RtpSenderType::TypeN);
        REQUIRE(r.isValid());
        CHECK(r.width == 1920);
        CHECK(r.height == 1080);
        CHECK(r.pgroup.octets == 5);
        CHECK(r.pgroup.pixels == 2);
        CHECK(r.packetsPerLine == 4);
        CHECK(r.packetsPerFrame == 4320);
        CHECK(r.progressive == true);
}

TEST_CASE("St2110TrafficCalc: R_ACTIVE is 1080/1125 for progressive formats") {
        // §6.3.2 fixes R_ACTIVE at 1080/1125 = 0.96 for every
        // progressive raster, including 720p and 2160p.
        const St2110TrafficCalc::Result hd =
                calc1080p(FrameRate(FrameRate::FPS_50), RtpSenderType::TypeNL);
        REQUIRE(hd.isValid());
        CHECK(hd.activeLines == 1080);
        CHECK(hd.totalLines == 1125);
        CHECK(hd.activeRatio == doctest::Approx(0.96).epsilon(1e-9));

        VideoFormat uhd(VideoFormat::Raster_UHD, FrameRate(FrameRate::FPS_60),
                        VideoScanMode::Progressive);
        const St2110TrafficCalc::Result r =
                St2110TrafficCalc::compute(uhd, St2110Sampling::YCbCr422, St2110Depth::Bits10,
                                           RtpSenderType::TypeN);
        REQUIRE(r.isValid());
        CHECK(r.activeRatio == doctest::Approx(0.96).epsilon(1e-9));
        // 3840-wide line = 1920 pgroups × 5 = 9600 octets → 7 packets
        // per line at the 1450-octet usable payload, × 2160 lines.
        CHECK(r.packetsPerLine == 7);
        CHECK(r.packetsPerFrame == 7 * 2160);
}

TEST_CASE("St2110TrafficCalc: interlaced picks the §6.3.3 Table 1 ratio") {
        VideoFormat sd(VideoFormat::Raster_SD525, FrameRate(FrameRate::FPS_29_97),
                       VideoScanMode::Interlaced);
        const St2110TrafficCalc::Result r =
                St2110TrafficCalc::compute(sd, St2110Sampling::YCbCr422, St2110Depth::Bits10,
                                           RtpSenderType::TypeN);
        REQUIRE(r.isValid());
        CHECK(r.progressive == false);
        CHECK(r.activeLines == 487);
        CHECK(r.totalLines == 525);

        VideoFormat hd625(VideoFormat::Raster_SD625, FrameRate(FrameRate::FPS_25),
                          VideoScanMode::Interlaced);
        const St2110TrafficCalc::Result r625 =
                St2110TrafficCalc::compute(hd625, St2110Sampling::YCbCr422, St2110Depth::Bits10,
                                           RtpSenderType::TypeN);
        REQUIRE(r625.isValid());
        CHECK(r625.activeLines == 576);
        CHECK(r625.totalLines == 625);
}

TEST_CASE("St2110TrafficCalc: narrow VRX_FULL has the 8-packet floor at MTU 1500") {
        // §7.1.1: VRX_FULL = max(INT(1500*8/MAXIP), ...).  With
        // MAXIP = 1500 the first term is 8 packets.
        const St2110TrafficCalc::Result r =
                calc1080p(FrameRate(FrameRate::FPS_24), RtpSenderType::TypeN);
        REQUIRE(r.isValid());
        CHECK(r.vrxFull >= 8);
        CHECK(r.beta == doctest::Approx(1.10));

        // Derived timing tolerances (§6.6): VRX window = VRX_FULL ×
        // T_RS, C_MAX burst window = C_MAX × T_DRAIN.
        REQUIRE(r.vrxTimingWindow.isValid());
        REQUIRE(r.cmaxBurstWindow.isValid());
        CHECK(r.vrxTimingWindow.nanoseconds() == r.vrxFull * r.trs.nanoseconds());
        CHECK(r.cmaxBurstWindow.nanoseconds() ==
              static_cast<int64_t>(r.cmax) * r.tDrain.nanoseconds());
}

TEST_CASE("St2110TrafficCalc: narrow vs wide differ in VRX_FULL / CMAX / TP") {
        const St2110TrafficCalc::Result narrow =
                calc1080p(FrameRate(FrameRate::FPS_60), RtpSenderType::TypeN);
        const St2110TrafficCalc::Result wide =
                calc1080p(FrameRate(FrameRate::FPS_60), RtpSenderType::TypeW);
        REQUIRE(narrow.isValid());
        REQUIRE(wide.isValid());

        // Same packetization → same N_PACKETS regardless of class.
        CHECK(narrow.packetsPerFrame == wide.packetsPerFrame);
        // Wide buffers far more and has a 16-packet CMAX floor.
        CHECK(wide.vrxFull > narrow.vrxFull);
        CHECK(narrow.cmax >= 4);
        CHECK(wide.cmax >= 16);
        CHECK(narrow.tpParam == String("2110TPN"));
        CHECK(wide.tpParam == String("2110TPW"));

        // Type N is gapped (T_RS scaled by R_ACTIVE); NL/W are linear.
        CHECK(narrow.trs.nanoseconds() == narrow.trsGapped.nanoseconds());
        CHECK(wide.trs.nanoseconds() == wide.trsLinear.nanoseconds());
        CHECK(narrow.trsGapped.nanoseconds() < narrow.trsLinear.nanoseconds());
}

TEST_CASE("St2110TrafficCalc: BPM yields fixed-size packets and a different count") {
        St2110TrafficCalc::PacketModel bpm;
        bpm.packingMode = St2110PackingMode::Bpm;
        VideoFormat fmt(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_60),
                        VideoScanMode::Progressive);
        const St2110TrafficCalc::Result r = St2110TrafficCalc::compute(
                fmt, St2110Sampling::YCbCr422, St2110Depth::Bits10, RtpSenderType::TypeNL, bpm);
        REQUIRE(r.isValid());
        // BPM payload is a multiple of 180 octets (§6.3.3); the
        // usable pgroup data is that minus ESN + SRD header, floored
        // to whole 5-octet pgroups.
        CHECK(r.payloadDataPerPacket > 0);
        CHECK(r.payloadDataPerPacket % 5 == 0);
        CHECK(r.packetsPerFrame > 0);
}

TEST_CASE("St2110TrafficCalc: custom MTU lowers the VRX_FULL packet floor") {
        St2110TrafficCalc::PacketModel jumbo;
        jumbo.mtu = 9000;
        VideoFormat fmt(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_60),
                        VideoScanMode::Progressive);
        const St2110TrafficCalc::Result r = St2110TrafficCalc::compute(
                fmt, St2110Sampling::YCbCr422, St2110Depth::Bits10, RtpSenderType::TypeN, jumbo);
        REQUIRE(r.isValid());
        // Larger packets → fewer packets/frame than the 1500 case.
        const St2110TrafficCalc::Result std1500 =
                calc1080p(FrameRate(FrameRate::FPS_60), RtpSenderType::TypeN);
        CHECK(r.packetsPerFrame < std1500.packetsPerFrame);
}

TEST_CASE("St2110TrafficCalc: invalid inputs report errors, not crashes") {
        // Invalid sender class for the §7.1 model.
        const St2110TrafficCalc::Result auto1 =
                calc1080p(FrameRate(FrameRate::FPS_60), RtpSenderType::Auto);
        CHECK_FALSE(auto1.isValid());

        // Invalid video format.
        const St2110TrafficCalc::Result badFmt = St2110TrafficCalc::compute(
                VideoFormat(), St2110Sampling::YCbCr422, St2110Depth::Bits10, RtpSenderType::TypeN);
        CHECK_FALSE(badFmt.isValid());

        // Unsupported (sampling, depth) — XYZ at 8-bit is not in the
        // ST 2110-20 pgroup table.
        VideoFormat fmt(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_60),
                        VideoScanMode::Progressive);
        const St2110TrafficCalc::Result badCombo = St2110TrafficCalc::compute(
                fmt, St2110Sampling::Xyz, St2110Depth::Bits8, RtpSenderType::TypeN);
        CHECK_FALSE(badCombo.isValid());
}

#endif // PROMEKI_ENABLE_NETWORK && PROMEKI_ENABLE_PROAV
