/**
 * @file      anccodec_hdrdynamic2094_40.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/hdmiinfoframe.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/result.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        HdrDynamic2094_40 sampleHdr10Plus() {
                HdrDynamic2094_40 md;
                md.setApplicationVersion(1);
                md.setNumWindows(1);
                md.setTargetedSystemDisplayMaximumLuminance(40'000'000u); // 4000 cd/m^2 * 10000
                auto &wp = md.windowProcessing()[0];
                wp.maxScl[0] = 100000;
                wp.maxScl[1] = 95000;
                wp.maxScl[2] = 90000;
                wp.averageMaxRgb = 50000;
                wp.distribution = {
                        HdrDynamic2094_40::DistributionMaxRgb{ 1u, 12000u },
                        HdrDynamic2094_40::DistributionMaxRgb{50u, 45000u },
                        HdrDynamic2094_40::DistributionMaxRgb{99u, 95000u },
                };
                wp.fractionBrightPixels = 256;
                wp.hasToneMapping = true;
                wp.toneMapping.kneePointX = 1024;
                wp.toneMapping.kneePointY = 2048;
                wp.toneMapping.bezierCurveAnchors = {64, 128, 192, 256};
                wp.hasColorSaturationMapping = false;
                return md;
        }

} // namespace

TEST_CASE("HdrDynamic2094_40<->HdmiInfoFrame: parser + builder registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::HdmiInfoFrame));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::HdmiInfoFrame));
}

TEST_CASE("HdrDynamic2094_40<->HdmiInfoFrame: build emits Vendor-Specific InfoFrame on type 0x81 / version 1") {
        AncTranslator     t;
        HdrDynamic2094_40 md = sampleHdr10Plus();
        Result<List<AncPacket>> built =
                t.build(Variant(md), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::HdmiInfoFrame);
        REQUIRE(built.second().isOk());
        CHECK(built.first().front().format().id() == AncFormat::HdrDynamic2094_40);
        CHECK(built.first().front().transport() == AncTransport::HdmiInfoFrame);

        Result<HdmiInfoFrame> rf = HdmiInfoFrame::from(built.first().front());
        REQUIRE(rf.second().isOk());
        const HdmiInfoFrame &frame = rf.first();
        CHECK(frame.type() == HdrDynamic2094_40::InfoFrameType);   // 0x81
        CHECK(frame.version() == HdrDynamic2094_40::InfoFrameVersion); // 1
        CHECK(frame.checksumValid());

        // Body must start with the HDR10+ OUI in LSB-first wire order.
        Buffer body = frame.body();
        REQUIRE(body.size() >= 3u);
        const uint8_t *bp = static_cast<const uint8_t *>(body.data());
        CHECK(bp[0] == 0x8Bu);
        CHECK(bp[1] == 0x84u);
        CHECK(bp[2] == 0x90u);
}

TEST_CASE("HdrDynamic2094_40<->HdmiInfoFrame: round-trip via AncTranslator parse + build") {
        AncTranslator     t;
        HdrDynamic2094_40 src = sampleHdr10Plus();
        Result<List<AncPacket>> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::HdmiInfoFrame);
        REQUIRE(built.second().isOk());

        Result<Variant> parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        REQUIRE(parsed.first().type() == Variant::TypeHdrDynamic2094_40);

        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out == src);
}

TEST_CASE("HdrDynamic2094_40<->HdmiInfoFrame: AncTranslateConfig::HdmiInfoFrameOui overrides default OUI") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::HdmiInfoFrameOui, uint32_t(0xAABBCCu));
        AncTranslator     t(cfg);
        HdrDynamic2094_40 md = sampleHdr10Plus();
        Result<List<AncPacket>> built =
                t.build(Variant(md), AncFormat(AncFormat::HdrDynamic2094_40), AncTransport::HdmiInfoFrame);
        REQUIRE(built.second().isOk());

        Result<HdmiInfoFrame> rf = HdmiInfoFrame::from(built.first().front());
        REQUIRE(rf.second().isOk());
        Buffer body = rf.first().body();
        REQUIRE(body.size() >= 3u);
        const uint8_t *bp = static_cast<const uint8_t *>(body.data());
        CHECK(bp[0] == 0xCCu);
        CHECK(bp[1] == 0xBBu);
        CHECK(bp[2] == 0xAAu);

        // Parser still round-trips even when the OUI is not the HDR10+ default —
        // it parses the bitstream after the three OUI bytes regardless.
        Result<Variant> parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        HdrDynamic2094_40 out = parsed.first().get<HdrDynamic2094_40>();
        CHECK(out == md);
}

TEST_CASE("HdrDynamic2094_40<->HdmiInfoFrame: parser rejects body shorter than 3-byte OUI") {
        // Construct a malformed Vendor-Specific InfoFrame with empty body.
        HdmiInfoFrame frame = HdmiInfoFrame::buildRaw(HdrDynamic2094_40::InfoFrameType,
                                                      HdrDynamic2094_40::InfoFrameVersion, Buffer());

        AncTranslator   t;
        Result<Variant> parsed = t.parse(frame.packet());
        CHECK(parsed.second().isError());
}
