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
        // RFC 8331 §3.1 / ST 2110-40 §7: m=video, not m=application.
        CHECK(md.mediaType() == "video");
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
        // m=audio is not RFC 8331 ANC.
        md.setMediaType("audio");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(96);
        md.setAttribute("rtpmap", "96 raw/90000");
        AncDesc bad = AncDesc::fromSdp(md);
        CHECK(bad.allowedFormats().isEmpty());

        // m=video with the wrong rtpmap encoding (raw/90000 is video
        // raw, not smpte291) is also rejected.
        SdpMediaDescription md2;
        md2.setMediaType("video");
        md2.setProtocol("RTP/AVP");
        md2.addPayloadType(96);
        md2.setAttribute("rtpmap", "96 raw/90000");
        CHECK(AncDesc::fromSdp(md2).allowedFormats().isEmpty());
}

TEST_CASE("AncDesc: fromSdp tolerates decimal DID/SDID literals") {
        SdpMediaDescription md;
        md.setMediaType("video");
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
        md.setMediaType("video");
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
        md.setMediaType("video");
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

// ============================================================================
// F3 — SDP C8 / A7 / C9 (ST 2110-40 §7 emission)
// ============================================================================

TEST_CASE("AncDesc F3 — toSdp emits ST 2110-40 §7 mandatory fmtp parameters") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_59_94);
        AncFormat::IDList allowed;
        allowed.pushToBack(AncFormat::Cea708);
        desc.setAllowedFormats(std::move(allowed));

        SdpMediaDescription md = desc.toSdp(100);
        // C8: m=video, not m=application.
        CHECK(md.mediaType() == "video");

        const String fmtp = md.attribute(String("fmtp"));
        REQUIRE_FALSE(fmtp.isEmpty());
        // C9: SSN and TM defaults must always be emitted.
        CHECK(fmtp.contains(String("SSN=ST2110-40:2018")));
        CHECK(fmtp.contains(String("TM=CTM")));
        // C9: exactframerate emitted from FrameRate.
        // FPS_59_94 = 60000/1001 → "60000/1001"
        CHECK(fmtp.contains(String("exactframerate=60000/1001")));
        // DID_SDID still present after the new params.
        CHECK(fmtp.contains(String("DID_SDID={0x61,0x01}")));
}

TEST_CASE("AncDesc F3 — integer frame rates emit bare integers") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        SdpMediaDescription md = desc.toSdp(100);
        const String fmtp = md.attribute(String("fmtp"));
        CHECK(fmtp.contains(String("exactframerate=60")));
        CHECK_FALSE(fmtp.contains(String("exactframerate=60/1")));
}

TEST_CASE("AncDesc F3 — A7 wildcard-SDID expands across the registered range") {
        // Smpte2020Audio registers under DID 0x45 with wildcard SDID,
        // backed by the concrete SDID range 0x01..0x09.
        AncDesc desc;
        AncFormat::IDList allowed;
        allowed.pushToBack(AncFormat::Smpte2020Audio);
        desc.setAllowedFormats(std::move(allowed));

        SdpMediaDescription md = desc.toSdp(100);
        const String fmtp = md.attribute(String("fmtp"));
        REQUIRE_FALSE(fmtp.isEmpty());
        // Wildcard SDID=0x00 must NOT appear (collides with Type-1
        // sentinel per RFC 8331 §3.1).
        CHECK_FALSE(fmtp.contains(String("DID_SDID={0x45,0x00}")));
        // Every concrete SDID under DID 0x45 must appear.
        for (uint8_t sdid = 0x01; sdid <= 0x09; ++sdid) {
                char  hex[3] = {'0', static_cast<char>('1' + (sdid - 1)), '\0'};
                const String expected = String("DID_SDID={0x45,0x0") +
                                        String::number(static_cast<int>(sdid)) +
                                        String("}");
                CHECK(fmtp.contains(expected));
                (void)hex;
        }
}

TEST_CASE("AncDesc F3 — fromSdp dedupes wildcard SDIDs into one family ID") {
        SdpMediaDescription md;
        md.setMediaType("video");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(100);
        md.setAttribute("rtpmap", "100 smpte291/90000");
        // Three concrete SMPTE 2020 SDIDs all collapse onto the same
        // Smpte2020Audio family ID via wildcard lookup.
        md.setAttribute("fmtp",
                        "100 DID_SDID={0x45,0x01};DID_SDID={0x45,0x03};DID_SDID={0x45,0x05}");
        AncDesc parsed = AncDesc::fromSdp(md);
        REQUIRE(parsed.allowedFormats().size() == 1u);
        CHECK(parsed.allowedFormats().at(0) == AncFormat::Smpte2020Audio);
}

TEST_CASE("AncDesc F3 — TROFF emitted only when non-zero") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        SdpMediaDescription md1 = desc.toSdp(100);
        // Default 0 → no TROFF parameter at all.
        CHECK_FALSE(md1.attribute(String("fmtp")).contains(String("TROFF=")));

        desc.setTroff(750);
        SdpMediaDescription md2 = desc.toSdp(100);
        CHECK(md2.attribute(String("fmtp")).contains(String("TROFF=750")));
}

TEST_CASE("AncDesc F3 — VPID_Code emitted only when non-zero") {
        AncDesc desc(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        SdpMediaDescription md1 = desc.toSdp(100);
        CHECK_FALSE(md1.attribute(String("fmtp")).contains(String("VPID_Code=")));

        desc.setVpidCode(0x89);
        SdpMediaDescription md2 = desc.toSdp(100);
        CHECK(md2.attribute(String("fmtp")).contains(String("VPID_Code=137")));
}

TEST_CASE("AncDesc F3 — fromSdp parses TROFF and VPID_Code") {
        SdpMediaDescription md;
        md.setMediaType("video");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(100);
        md.setAttribute("rtpmap", "100 smpte291/90000");
        md.setAttribute(
                "fmtp",
                "100 SSN=ST2110-40:2018;TM=CTM;exactframerate=60;"
                "TROFF=1234;VPID_Code=137;DID_SDID={0x61,0x01}");
        AncDesc parsed = AncDesc::fromSdp(md);
        CHECK(parsed.troff() == 1234u);
        CHECK(parsed.vpidCode() == 0x89);
        REQUIRE(parsed.allowedFormats().size() == 1u);
        CHECK(parsed.allowedFormats().at(0) == AncFormat::Cea708);
}

TEST_CASE("AncDesc F3 — TROFF / VPID_Code survive fromSdp/toSdp round-trip") {
        AncDesc original(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        original.setTroff(0xCAFEu);
        original.setVpidCode(0x84);
        AncFormat::IDList allowed;
        allowed.pushToBack(AncFormat::Cea708);
        original.setAllowedFormats(std::move(allowed));

        SdpMediaDescription md = original.toSdp(100);
        AncDesc parsed = AncDesc::fromSdp(md);
        CHECK(parsed.troff() == 0xCAFEu);
        CHECK(parsed.vpidCode() == 0x84);
}

TEST_CASE("AncDesc F3 — DataStream v2 preserves TROFF + VPID_Code") {
        AncDesc original(Size2Du32(1920, 1080), VideoScanMode::Progressive, FrameRate::FPS_60);
        original.setTroff(42u);
        original.setVpidCode(0x77);

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        AncDesc restored;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        CHECK(restored.troff() == 42u);
        CHECK(restored.vpidCode() == 0x77);
}

TEST_CASE("AncFormat F3 — st291ConcreteSdids resolves wildcard families") {
        // Smpte2020Audio: wildcard, concrete range 0x01..0x09.
        const auto sdids = AncFormat(AncFormat::Smpte2020Audio).st291ConcreteSdids();
        REQUIRE(sdids.size() == 9u);
        for (size_t i = 0; i < 9; ++i) {
                CHECK(sdids.at(i) == static_cast<uint8_t>(0x01 + i));
        }
        // Non-wildcard format returns a one-element list.
        const auto cea = AncFormat(AncFormat::Cea708).st291ConcreteSdids();
        REQUIRE(cea.size() == 1u);
        CHECK(cea.at(0) == 0x01);
        // Format with no St291 carriage returns empty.
        const auto scte35 = AncFormat(AncFormat::Scte35).st291ConcreteSdids();
        CHECK(scte35.isEmpty());
}
