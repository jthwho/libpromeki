/**
 * @file      ancdesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancdesc.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

// ============================================================================
// Construction / validity
// ============================================================================

TEST_CASE("AncDesc: default-constructed is invalid") {
        AncDesc d;
        CHECK_FALSE(d.isValid());
}

TEST_CASE("AncDesc: bound raster + scan mode + rate makes it valid") {
        AncDesc d(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        CHECK(d.isValid());
        CHECK(d.sourceRaster() == Size2Du32(1920, 1080));
        CHECK(d.scanMode() == VideoScanMode::Progressive);
        CHECK(d.frameRate() == FrameRate::FPS_60);
}

TEST_CASE("AncDesc: unbound raster but with allowedFormats is valid") {
        AncDesc d;
        AncFormat::IDList ids;
        ids.pushToBack(AncFormat::Cea708);
        d.setAllowedFormats(std::move(ids));
        CHECK(d.isValid());
}

TEST_CASE("AncDesc: unbound raster but with allowedCategories is valid") {
        AncDesc                    d;
        ::promeki::List<AncCategory> cats;
        cats.pushToBack(AncCategory::Captions);
        d.setAllowedCategories(std::move(cats));
        CHECK(d.isValid());
}

// ============================================================================
// Paired stream indices
// ============================================================================

TEST_CASE("AncDesc: paired stream indices default to -1 (unbound)") {
        AncDesc d;
        CHECK(d.pairedVideoStreamIndex() == -1);
        CHECK(d.pairedAudioStreamIndex() == -1);

        AncDesc bound(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        CHECK(bound.pairedVideoStreamIndex() == -1);
        CHECK(bound.pairedAudioStreamIndex() == -1);
}

TEST_CASE("AncDesc: paired stream indices round-trip through setters") {
        AncDesc d;
        d.setPairedVideoStreamIndex(2);
        d.setPairedAudioStreamIndex(1);
        CHECK(d.pairedVideoStreamIndex() == 2);
        CHECK(d.pairedAudioStreamIndex() == 1);
        d.setPairedVideoStreamIndex(-1);
        CHECK(d.pairedVideoStreamIndex() == -1);
        CHECK(d.pairedAudioStreamIndex() == 1);
}

TEST_CASE("AncDesc: paired stream indices participate in formatEquals / operator==") {
        AncDesc a(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        AncDesc b = a;
        CHECK(a == b);
        CHECK(a.formatEquals(b));

        b.setPairedVideoStreamIndex(0);
        CHECK_FALSE(a.formatEquals(b));
        CHECK(a != b);

        a.setPairedVideoStreamIndex(0);
        CHECK(a.formatEquals(b));
        CHECK(a == b);

        b.setPairedAudioStreamIndex(1);
        CHECK_FALSE(a.formatEquals(b));
        a.setPairedAudioStreamIndex(1);
        CHECK(a.formatEquals(b));
}

// ============================================================================
// CoW detach — mutators on a shared handle don't bleed into the source
// ============================================================================

TEST_CASE("AncDesc: copying a handle shares storage, mutation detaches") {
        AncDesc original(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        original.setPairedVideoStreamIndex(0);
        AncDesc copy = original;

        // Both observe the same fields prior to any mutation.
        CHECK(copy == original);
        CHECK(copy.pairedVideoStreamIndex() == 0);

        // Mutate the copy; original must not change.
        copy.setPairedVideoStreamIndex(1);
        CHECK(copy.pairedVideoStreamIndex() == 1);
        CHECK(original.pairedVideoStreamIndex() == 0);

        // Mutate via metadata() &; original metadata must be untouched.
        copy.metadata().set(Metadata::Title, String("Copy"));
        CHECK(copy.metadata().get(Metadata::Title).get<String>() == "Copy");
        CHECK_FALSE(original.metadata().contains(Metadata::Title));

        // setAllowedFormats also detaches.
        AncFormat::IDList fmts;
        fmts.pushToBack(AncFormat::Cea708);
        copy.setAllowedFormats(fmts);
        CHECK(copy.allowedFormats().size() == 1);
        CHECK(original.allowedFormats().isEmpty());
}

// ============================================================================
// acceptsFormat — filter logic
// ============================================================================

TEST_CASE("AncDesc: empty filters accept every format") {
        AncDesc d(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        CHECK(d.acceptsFormat(AncFormat(AncFormat::Cea708)));
        CHECK(d.acceptsFormat(AncFormat(AncFormat::AtcLtc)));
        CHECK(d.acceptsFormat(AncFormat(AncFormat::HdrStatic2086)));
}

TEST_CASE("AncDesc: allowedFormats whitelist restricts admittance") {
        AncDesc           d(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        AncFormat::IDList ids;
        ids.pushToBack(AncFormat::Cea708);
        ids.pushToBack(AncFormat::AtcLtc);
        d.setAllowedFormats(std::move(ids));

        CHECK(d.acceptsFormat(AncFormat(AncFormat::Cea708)));
        CHECK(d.acceptsFormat(AncFormat(AncFormat::AtcLtc)));
        CHECK_FALSE(d.acceptsFormat(AncFormat(AncFormat::Afd)));
        CHECK_FALSE(d.acceptsFormat(AncFormat(AncFormat::HdrStatic2086)));
}

TEST_CASE("AncDesc: allowedCategories whitelist restricts admittance") {
        AncDesc                    d(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        ::promeki::List<AncCategory> cats;
        cats.pushToBack(AncCategory::Captions);
        cats.pushToBack(AncCategory::Timecode);
        d.setAllowedCategories(std::move(cats));

        CHECK(d.acceptsFormat(AncFormat(AncFormat::Cea708)));
        CHECK(d.acceptsFormat(AncFormat(AncFormat::AtcLtc)));
        CHECK_FALSE(d.acceptsFormat(AncFormat(AncFormat::Afd))); // Aspect, not in filter
}

TEST_CASE("AncDesc: both filters must admit the format") {
        AncDesc d(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);

        AncFormat::IDList ids;
        ids.pushToBack(AncFormat::Cea708);
        d.setAllowedFormats(std::move(ids));

        ::promeki::List<AncCategory> cats;
        cats.pushToBack(AncCategory::Captions);
        d.setAllowedCategories(std::move(cats));

        CHECK(d.acceptsFormat(AncFormat(AncFormat::Cea708)));
        // Cea608 is Captions but not in the ID whitelist.
        CHECK_FALSE(d.acceptsFormat(AncFormat(AncFormat::Cea608)));
}

// ============================================================================
// formatEquals / operator==
// ============================================================================

TEST_CASE("AncDesc: formatEquals ignores metadata") {
        AncDesc a(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_30);
        AncDesc b(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_30);

        a.metadata().set(Metadata::Title, String("With Title"));
        CHECK(a.formatEquals(b));
        CHECK(a != b);
}

TEST_CASE("AncDesc: operator== compares everything including metadata") {
        AncDesc a(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_30);
        AncDesc b = a;
        CHECK(a == b);

        b.metadata().set(Metadata::Title, String("Different"));
        CHECK(a != b);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("AncDesc: DataStream round-trip preserves every field") {
        AncDesc original(Size2Du32(1920, 1080), VideoScanMode::InterlacedOddFirst, FrameRate::FPS_30);
        AncFormat::IDList ids;
        ids.pushToBack(AncFormat::Cea708);
        ids.pushToBack(AncFormat::AtcLtc);
        original.setAllowedFormats(std::move(ids));

        ::promeki::List<AncCategory> cats;
        cats.pushToBack(AncCategory::Captions);
        cats.pushToBack(AncCategory::Timecode);
        original.setAllowedCategories(std::move(cats));

        original.setPairedVideoStreamIndex(0);
        original.setPairedAudioStreamIndex(2);
        original.metadata().set(Metadata::Title, String("Test ANC stream"));

        Buffer         storage(8192);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);

        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << original;
                REQUIRE(writer.status() == DataStream::Ok);
        }
        dev.seek(0);
        AncDesc round;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> round;
                REQUIRE(reader.status() == DataStream::Ok);
        }
        CHECK(round == original);
        CHECK(round.allowedFormats().size() == 2);
        CHECK(round.allowedCategories().size() == 2);
        CHECK(round.pairedVideoStreamIndex() == 0);
        CHECK(round.pairedAudioStreamIndex() == 2);
        CHECK(round.metadata().get(Metadata::Title).get<String>() == "Test ANC stream");
}

TEST_CASE("AncDesc: DataStream round-trip of an unbound descriptor with categories only") {
        AncDesc original;
        ::promeki::List<AncCategory> cats;
        cats.pushToBack(AncCategory::Splice);
        original.setAllowedCategories(std::move(cats));

        Buffer         storage(4096);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);

        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << original;
        }
        dev.seek(0);
        AncDesc round;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> round;
        }
        CHECK(round.isValid());
        CHECK(round.allowedCategories().size() == 1);
        CHECK(round.allowedCategories().at(0) == AncCategory::Splice);
}

// ============================================================================
// SDP round-trip (RFC 8331 §6.2)
// ============================================================================

#include <promeki/sdpsession.h>

TEST_CASE("AncDesc: toSdp emits smpte291 rtpmap + DID_SDID fmtp list") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        AncFormat::IDList allowed;
        allowed.pushToBack(AncFormat::Cea708);
        allowed.pushToBack(AncFormat::AtcLtc);
        desc.setAllowedFormats(std::move(allowed));

        SdpMediaDescription md = desc.toSdp(100);
        CHECK(md.mediaType() == "application");
        CHECK(md.protocol() == "RTP/AVP");
        REQUIRE(md.payloadTypes().size() == 1);
        CHECK(md.payloadTypes().at(0) == 100);

        SdpMediaDescription::RtpMap rm = md.rtpMap();
        CHECK(rm.valid);
        CHECK(rm.encoding == "smpte291");
        CHECK(rm.clockRate == 90000u);
        CHECK(rm.payloadType == 100);

        const String fmtp = md.attribute(String("fmtp"));
        REQUIRE_FALSE(fmtp.isEmpty());
        // Cea708 is DID 0x61, SDID 0x01; AtcLtc is DID 0x60, SDID 0x60.
        CHECK(fmtp.contains(String("DID_SDID={0x61,0x01}")));
        CHECK(fmtp.contains(String("DID_SDID={0x60,0x60}")));
}

TEST_CASE("AncDesc: empty allowedFormats emits the full St291 registry") {
        AncDesc desc;
        AncFormat::IDList cats;  // intentionally empty
        SdpMediaDescription md = desc.toSdp(100);
        const String fmtp = md.attribute(String("fmtp"));
        REQUIRE_FALSE(fmtp.isEmpty());
        // Spot-check a handful of well-known St291 formats.
        CHECK(fmtp.contains(String("DID_SDID={0x61,0x01}")));   // Cea708
        CHECK(fmtp.contains(String("DID_SDID={0x41,0x05}")));   // Afd
        CHECK(fmtp.contains(String("DID_SDID={0x60,0x60}")));   // AtcLtc
}

TEST_CASE("AncDesc: fromSdp / toSdp round-trip preserves DID/SDID list") {
        AncDesc original;
        AncFormat::IDList allowed;
        allowed.pushToBack(AncFormat::Cea708);
        allowed.pushToBack(AncFormat::Afd);
        allowed.pushToBack(AncFormat::AtcVitc1);
        original.setAllowedFormats(allowed);

        SdpMediaDescription md = original.toSdp(101);
        AncDesc parsed = AncDesc::fromSdp(md);

        REQUIRE(parsed.allowedFormats().size() == allowed.size());
        for (size_t i = 0; i < allowed.size(); ++i) {
                CHECK(parsed.allowedFormats().at(i) == allowed.at(i));
        }
}

TEST_CASE("AncDesc: fromSdp rejects wrong media type / rtpmap") {
        SdpMediaDescription md;
        md.setMediaType("video");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(96);
        md.setAttribute("rtpmap", "96 raw/90000");
        AncDesc bad = AncDesc::fromSdp(md);
        CHECK(bad.allowedFormats().isEmpty());
}

TEST_CASE("AncDesc: fromSdp tolerates decimal DID/SDID literals") {
        SdpMediaDescription md;
        md.setMediaType("application");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(100);
        md.setAttribute("rtpmap", "100 smpte291/90000");
        // Mix decimal and hex in the same fmtp line.
        md.setAttribute("fmtp", "100 DID_SDID={97,1};DID_SDID={0x41,5}");
        AncDesc parsed = AncDesc::fromSdp(md);
        REQUIRE(parsed.allowedFormats().size() == 2u);
        CHECK(parsed.allowedFormats().at(0) == AncFormat::Cea708);
        CHECK(parsed.allowedFormats().at(1) == AncFormat::Afd);
}

TEST_CASE("AncDesc: fromSdp keeps unknown DID/SDID pairs as Invalid") {
        SdpMediaDescription md;
        md.setMediaType("application");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(100);
        md.setAttribute("rtpmap", "100 smpte291/90000");
        md.setAttribute("fmtp", "100 DID_SDID={0xAB,0xCD}");
        AncDesc parsed = AncDesc::fromSdp(md);
        REQUIRE(parsed.allowedFormats().size() == 1u);
        CHECK(parsed.allowedFormats().at(0) == AncFormat::Invalid);
}

TEST_CASE("AncDesc: fromSdp ignores malformed DID_SDID entries") {
        SdpMediaDescription md;
        md.setMediaType("application");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(100);
        md.setAttribute("rtpmap", "100 smpte291/90000");
        // Missing braces and the comma respectively — both should
        // be skipped, leaving the valid trailing entry intact.
        md.setAttribute("fmtp", "100 DID_SDID=0x61,0x01;DID_SDID={};DID_SDID={0x61,0x01}");
        AncDesc parsed = AncDesc::fromSdp(md);
        REQUIRE(parsed.allowedFormats().size() == 1u);
        CHECK(parsed.allowedFormats().at(0) == AncFormat::Cea708);
}
