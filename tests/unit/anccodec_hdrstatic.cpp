/**
 * @file      anccodec_hdrstatic.cpp
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
#include <promeki/contentlightlevel.h>
#include <promeki/hdmiinfoframe.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/masteringdisplay.h>
#include <promeki/result.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        HdrStaticMetadata hdr10Sample() {
                MasteringDisplay md(CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797),
                                    CIEPoint(0.131, 0.046), CIEPoint(0.3127, 0.3290),
                                    0.005, 1000.0);
                return HdrStaticMetadata(TransferCharacteristics::SMPTE2084, std::move(md),
                                         ContentLightLevel(1000, 400));
        }

} // namespace

TEST_CASE("HdrStatic<->HdmiInfoFrame: parser + builder are registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::HdrStatic2086), AncTransport::HdmiInfoFrame));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::HdrStatic2086), AncTransport::HdmiInfoFrame));
}

TEST_CASE("HdrStatic<->HdmiInfoFrame: build emits an InfoFrame on type 0x87 / version 1") {
        AncTranslator     t;
        HdrStaticMetadata md = hdr10Sample();
        Result<AncPacket> built =
                t.build(Variant(md), AncFormat(AncFormat::HdrStatic2086), AncTransport::HdmiInfoFrame);
        REQUIRE(built.second().isOk());
        CHECK(built.first().format().id() == AncFormat::HdrStatic2086);
        CHECK(built.first().transport() == AncTransport::HdmiInfoFrame);

        Result<HdmiInfoFrame> rf = HdmiInfoFrame::from(built.first());
        REQUIRE(rf.second().isOk());
        const HdmiInfoFrame &frame = rf.first();
        CHECK(frame.type() == HdrStaticMetadata::InfoFrameType);   // 0x87
        CHECK(frame.version() == HdrStaticMetadata::InfoFrameVersion); // 1
        CHECK(frame.length() == HdrStaticMetadata::Type1BodySize); // 26
        CHECK(frame.checksumValid());
}

TEST_CASE("HdrStatic<->HdmiInfoFrame: round-trip via AncTranslator parse + build") {
        AncTranslator     t;
        HdrStaticMetadata src = hdr10Sample();
        Result<AncPacket> built =
                t.build(Variant(src), AncFormat(AncFormat::HdrStatic2086), AncTransport::HdmiInfoFrame);
        REQUIRE(built.second().isOk());

        Result<Variant> parsed = t.parse(built.first());
        REQUIRE(parsed.second().isOk());
        REQUIRE(parsed.first().type() == Variant::TypeHdrStaticMetadata);

        HdrStaticMetadata out = parsed.first().get<HdrStaticMetadata>();
        // Compare via wire bytes — the in-memory CIEPoint doubles round
        // through CTA-861.3's 50000-step quantisation, so the bit-exact
        // identity is on the encoded form.
        CHECK(out.toBuffer() == src.toBuffer());
        // (Buffer::operator== now does value-equality with identity
        // short-circuit; two independently-built CDP wire forms with
        // identical bytes compare equal here.)
        CHECK(out.eotf() == TransferCharacteristics::SMPTE2084);
        CHECK(out.contentLightLevel().maxCLL() == 1000u);
        CHECK(out.contentLightLevel().maxFALL() == 400u);
}
