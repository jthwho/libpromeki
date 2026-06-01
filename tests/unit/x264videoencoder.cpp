/**
 * @file      tests/x264videoencoder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Host-runnable (no GPU) coverage for the libx264 software H.264 encoder
 * backend: input-format table, end-to-end encode through the typed
 * VideoCodec::H264 "x264" backend, out-of-band parameter sets, and
 * bitstream sanity via the SPS resolution / chroma / depth parsed back
 * out of the emitted Annex-B.
 */

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include "codectesthelpers.h"

#include <promeki/x264videoencoder.h>
#include <promeki/videocodec.h>
#include <promeki/videoencoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/h264bitstream.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/set.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/ciepoint.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/ancdesc.h>
#include <promeki/anctranslator.h>
#include <promeki/cea708cdp.h>
#include <promeki/variant.h>
#include <promeki/videoencodersei.h>
#include <promeki/pixelaspect.h>
#include <promeki/enums_video.h>
#include <promeki/timecode.h>

using namespace promeki;

// Fill a planar YUV payload with a deterministic pattern.  Works for
// 8-bit (uint8_t) and 10-bit LE (uint16_t) samples.
static UncompressedVideoPayload::Ptr makePlanarYUV(int width, int height, PixelFormat::ID pd, int bitDepth,
                                                   int chromaIDC) {
        auto img = UncompressedVideoPayload::allocate(ImageDesc(width, height, pd));
        if (!img.isValid()) return img;
        const int    chromaMid = (bitDepth == 8) ? 128 : 512;
        const int    lumaMax = (bitDepth == 8) ? 219 : 876;
        const size_t pixSize = (bitDepth > 8) ? 2 : 1;
        auto         store = [&](uint8_t *row, int x, int val) {
                if (pixSize == 1) {
                        row[x] = static_cast<uint8_t>(val);
                } else {
                        row[x * 2 + 0] = static_cast<uint8_t>(val & 0xFF);
                        row[x * 2 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                }
        };
        const auto &ml = img->desc().pixelFormat().memLayout();

        // Luma plane — horizontal ramp.
        {
                uint8_t     *plane = img.modify()->data()[0].data();
                const size_t stride = ml.lineStride(0, width);
                for (int y = 0; y < height; y++) {
                        uint8_t *row = plane + y * stride;
                        for (int x = 0; x < width; x++) {
                                store(row, x, ((x * lumaMax) / width) + 16);
                        }
                }
        }
        // Chroma planes — flat midpoint.
        const int chromaW = (chromaIDC == 3) ? width : width / 2;
        const int chromaH = (chromaIDC == 1) ? height / 2 : height;
        for (size_t p = 1; p < img->desc().pixelFormat().planeCount(); p++) {
                uint8_t     *plane = img.modify()->data()[p].data();
                const size_t stride = ml.lineStride(p, width);
                for (int y = 0; y < chromaH; y++) {
                        uint8_t *row = plane + y * stride;
                        for (int x = 0; x < chromaW; x++) {
                                store(row, x, chromaMid);
                        }
                }
        }
        return img;
}

// Push one Frame through an encoder, drain it, flush, and return every
// emitted compressed packet (EOS sentinel excluded).
static List<CompressedVideoPayload::Ptr> encodeFrame(VideoEncoder &enc, const Frame &frame) {
        List<CompressedVideoPayload::Ptr> out;
        auto                              drainReady = [&]() {
                for (;;) {
                        Frame f = enc.receiveFrame();
                        if (!f.isValid()) break;
                        auto pkt = tests::firstCompressedVideo(f);
                        if (pkt.isValid() && !pkt->isEndOfStream()) out.pushToBack(pkt);
                }
        };
        enc.submitFrame(frame);
        drainReady();
        enc.flush();
        for (;;) {
                Frame f = enc.receiveFrame();
                if (!f.isValid()) break;
                auto pkt = tests::firstCompressedVideo(f);
                if (!pkt.isValid()) continue;
                if (pkt->isEndOfStream()) break;
                out.pushToBack(pkt);
        }
        return out;
}

static List<CompressedVideoPayload::Ptr> encodeOne(VideoEncoder &enc, const UncompressedVideoPayload::Ptr &src) {
        return encodeFrame(enc, tests::frameWith(src));
}

// Reverse H.264 RBSP emulation-prevention: drop the 0x03 in any
// 00 00 03 sequence so the SEI message header bytes parse cleanly.
static List<uint8_t> stripEmulation(const uint8_t *d, size_t n) {
        List<uint8_t> out;
        out.reserve(n);
        int zeros = 0;
        for (size_t i = 0; i < n; ++i) {
                if (zeros >= 2 && d[i] == 0x03) {
                        zeros = 0;
                        continue;
                }
                out.pushToBack(d[i]);
                zeros = (d[i] == 0x00) ? zeros + 1 : 0;
        }
        return out;
}

// Collect every SEI payloadType present across the SEI NALs of an
// emitted Annex-B access unit.
static Set<int> seiPayloadTypes(const CompressedVideoPayload::Ptr &pkt) {
        Set<int> types;
        auto     pv = pkt->plane(0);
        Buffer   buf(pv.size());
        std::memcpy(buf.data(), pv.data(), pv.size());
        buf.setSize(pv.size());
        BufferView view(buf, 0, pv.size());
        H264Bitstream::forEachAnnexBNal(view, [&](const H264Bitstream::NalUnit &nal) -> Error {
                if ((nal.header0 & 0x1F) != 6) return Error::Ok; // not SEI
                const List<uint8_t> rbsp = stripEmulation(nal.view.data(), nal.view.size());
                const uint8_t      *d = rbsp.data();
                const size_t        n = rbsp.size();
                size_t              i = 1; // skip the 1-byte NAL header
                while (i < n && d[i] != 0x80) {
                        int type = 0;
                        while (i < n && d[i] == 0xFF) type += 255, ++i;
                        if (i >= n) break;
                        type += d[i++];
                        int size = 0;
                        while (i < n && d[i] == 0xFF) size += 255, ++i;
                        if (i >= n) break;
                        size += d[i++];
                        types.insert(type);
                        i += static_cast<size_t>(size);
                }
                return Error::Ok;
        });
        return types;
}

// Locate the SPS NAL inside an Annex-B blob and parse its resolution /
// chroma / depth.
static bool parseSpsFrom(const String &annexB, H264Bitstream::SpsInfo &out) {
        if (annexB.isEmpty()) return false;
        const size_t len = annexB.length();
        Buffer       buf(len);
        std::memcpy(buf.data(), annexB.cstr(), len);
        buf.setSize(len);
        BufferView view(buf, 0, len);
        bool       found = false;
        H264Bitstream::forEachAnnexBNal(view, [&](const H264Bitstream::NalUnit &nal) -> Error {
                if ((nal.header0 & 0x1F) == 7) { // NAL_SPS
                        if (H264Bitstream::parseSpsResolution(nal.view, out).isOk()) found = true;
                }
                return Error::Ok;
        });
        return found;
}

// ---------------------------------------------------------------------------
// Input format table
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_SupportedInputList") {
        const List<int> in = X264VideoEncoder::supportedInputList();
        auto            has = [&](PixelFormat::ID id) { return in.contains(static_cast<int>(id)); };
        CHECK(has(PixelFormat::YUV8_420_Planar_Rec709));
        CHECK(has(PixelFormat::YUV8_422_Planar_Rec709));
        CHECK(has(PixelFormat::YUV8_444_Planar_Rec709));
        CHECK(has(PixelFormat::YUV10_420_Planar_LE_Rec709));
        CHECK(has(PixelFormat::YUV10_422_Planar_LE_Rec709));
        CHECK(has(PixelFormat::YUV10_444_Planar_LE_Rec709));
        CHECK(in.size() == 6);
        // Every advertised input is planar YUV the encoder feeds zero-copy.
        for (int id : in) {
                CHECK(PixelFormat(static_cast<PixelFormat::ID>(id)).planeCount() == 3);
        }
}

// ---------------------------------------------------------------------------
// Registry — the typed VideoCodec::H264 path resolves the "x264" backend
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_RegisteredBackend") {
        VideoCodec vc(VideoCodec::H264);
        REQUIRE(vc.isValid());
        CHECK(vc.canEncode());

        // Pin the software backend explicitly (NVENC, when present,
        // outranks it by weight on an unpinned create).
        MediaConfig cfg;
        cfg.set(MediaConfig::CodecBackend, String("x264"));
        auto encResult = vc.createEncoder(&cfg);
        REQUIRE_FALSE(error(encResult).isError());
        VideoEncoder *enc = value(encResult);
        REQUIRE(enc != nullptr);
        // The resolved codec round-trips to the canonical Name:Backend form.
        CHECK(enc->codec().toString() == String("H264:x264"));

        auto src = makePlanarYUV(320, 240, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        auto pkts = encodeOne(*enc, src);
        CHECK(pkts.size() == 1);
        delete enc;
}

// ---------------------------------------------------------------------------
// Invalid input
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_InvalidInput") {
        X264VideoEncoder enc;
        CHECK(enc.submitFrame(Frame()).isError());
}

TEST_CASE("X264VideoEncoder_RejectsUnsupportedPixelFormat") {
        X264VideoEncoder enc;
        // Interleaved RGB is not a planar-YUV input the encoder accepts.
        auto uvp = UncompressedVideoPayload::allocate(ImageDesc(64, 64, PixelFormat(PixelFormat::RGB8_sRGB)));
        CHECK(enc.submitFrame(tests::frameWith(uvp)).isError());
}

// ---------------------------------------------------------------------------
// End-to-end encode — 8-bit 4:2:0 MVP
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_Encode420_8bit") {
        X264VideoEncoder enc;
        MediaConfig      cfg;
        cfg.set(MediaConfig::VideoRcMode, RateControlMode::CQP);
        cfg.set(MediaConfig::VideoQp, 23);
        cfg.set(MediaConfig::GopLength, 30);
        cfg.set(MediaConfig::BFrames, 0);
        enc.configure(cfg);

        auto src = makePlanarYUV(320, 240, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        auto pkts = encodeOne(enc, src);

        REQUIRE(pkts.size() == 1);
        CompressedVideoPayload::Ptr pkt = pkts[0];
        CHECK(pkt->isKeyframe());
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::H264);
        CHECK(pkt->plane(0).size() > 0);

        // Out-of-band parameter sets are published on the packet.
        String params = pkt->metadata().get(Metadata::CodecParameterSets).get<String>();
        REQUIRE_FALSE(params.isEmpty());

        // The emitted SPS describes a 320x240 4:2:0 8-bit stream.
        H264Bitstream::SpsInfo sps;
        REQUIRE(parseSpsFrom(params, sps));
        CHECK(sps.width == 320);
        CHECK(sps.height == 240);
        CHECK(sps.chromaFormatIdc == 1);
        CHECK(sps.bitDepthLumaMinus8 == 0);
}

// ---------------------------------------------------------------------------
// Geometry / profile-auto wiring — 8/10-bit × 4:2:0 / 4:2:2 / 4:4:4 all
// produce a correctly-signalled SPS.  (10-bit / 4:2:2 / 4:4:4 are the
// Milestone-4 pro formats; this proves the shared code path now.)
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_ChromaDepthSignalling") {
        struct Case {
                        PixelFormat::ID id;
                        int             bitDepth;
                        int             chromaIDC;
        };
        const Case cases[] = {
                {PixelFormat::YUV8_420_Planar_Rec709, 8, 1},
                {PixelFormat::YUV8_422_Planar_Rec709, 8, 2},
                {PixelFormat::YUV8_444_Planar_Rec709, 8, 3},
                {PixelFormat::YUV10_420_Planar_LE_Rec709, 10, 1},
                {PixelFormat::YUV10_422_Planar_LE_Rec709, 10, 2},
                {PixelFormat::YUV10_444_Planar_LE_Rec709, 10, 3},
        };
        for (const auto &c : cases) {
                X264VideoEncoder enc;
                auto             src = makePlanarYUV(160, 128, c.id, c.bitDepth, c.chromaIDC);
                REQUIRE_MESSAGE(src.isValid(), "alloc failed for ", (int)c.id);
                auto pkts = encodeOne(enc, src);
                REQUIRE_MESSAGE(pkts.size() == 1, "encode failed for ", (int)c.id);
                CHECK(pkts[0]->isKeyframe());

                String params = pkts[0]->metadata().get(Metadata::CodecParameterSets).get<String>();
                H264Bitstream::SpsInfo sps;
                REQUIRE_MESSAGE(parseSpsFrom(params, sps), "no SPS for ", (int)c.id);
                CHECK(sps.width == 160);
                CHECK(sps.height == 128);
                CHECK(sps.chromaFormatIdc == c.chromaIDC);
                CHECK(sps.bitDepthLumaMinus8 == (c.bitDepth - 8));
        }
}

// ---------------------------------------------------------------------------
// Reset returns the session to a fresh state
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_ResetReusable") {
        X264VideoEncoder enc;
        auto             src = makePlanarYUV(128, 96, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        CHECK(encodeOne(enc, src).size() == 1);
        CHECK_FALSE(enc.reset().isError());
        // A second pass after reset works (and could use a new geometry).
        auto src2 = makePlanarYUV(256, 160, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        CHECK(encodeOne(enc, src2).size() == 1);
}

// ---------------------------------------------------------------------------
// HDR static-metadata SEI — mastering display (137) + content light (144)
// driven from MediaConfig, parsed back out of the emitted bitstream.
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_HdrSeiInjected") {
        X264VideoEncoder enc;
        MediaConfig      cfg;
        cfg.set(MediaConfig::HdrMasteringDisplay,
                MasteringDisplay(CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797), CIEPoint(0.131, 0.046),
                                 CIEPoint(0.3127, 0.3290), 0.005, 1000.0));
        cfg.set(MediaConfig::HdrContentLightLevel, ContentLightLevel(1000, 400));
        enc.configure(cfg);

        auto src = makePlanarYUV(160, 128, PixelFormat::YUV10_420_Planar_LE_Rec709, 10, 1);
        auto pkts = encodeOne(enc, src);
        REQUIRE(pkts.size() == 1);

        const Set<int> types = seiPayloadTypes(pkts[0]);
        CHECK(types.contains(VideoEncoderSei::TypeMasteringDisplay));
        CHECK(types.contains(VideoEncoderSei::TypeContentLightLevel));
}

TEST_CASE("X264VideoEncoder_NoHdrConfig_NoHdrSei") {
        X264VideoEncoder enc;
        auto             src = makePlanarYUV(160, 128, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        auto             pkts = encodeOne(enc, src);
        REQUIRE(pkts.size() == 1);
        const Set<int> types = seiPayloadTypes(pkts[0]);
        CHECK_FALSE(types.contains(VideoEncoderSei::TypeMasteringDisplay));
        CHECK_FALSE(types.contains(VideoEncoderSei::TypeContentLightLevel));
}

// ---------------------------------------------------------------------------
// Caption SEI — CEA-708 ANC on the source Frame → user_data_registered (4)
// ---------------------------------------------------------------------------

// Build an AncPayload carrying one CEA-708 caption packet ("hi!").
static AncPayload::Ptr makeCaptionAnc() {
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x94, 0x20});
        triples.pushToBack({true, 0, 'h' | 0x80, 'i' | 0x80});
        triples.pushToBack({true, 0, '!' | 0x80, 0x80});
        Cea708Cdp     cdp(4, triples, 7);
        AncTranslator t;
        AncTranslator::PacketsResult built =
                t.build(Variant(cdp), AncFormat(AncFormat::Cea708), AncTransport::St291);
        if (!built.second().isOk()) return AncPayload::Ptr();
        return AncPayload::Ptr::create(AncDesc(), built.first());
}

TEST_CASE("X264VideoEncoder_CaptionSeiInjected") {
        AncPayload::Ptr anc = makeCaptionAnc();
        REQUIRE(anc.isValid());

        auto  src = makePlanarYUV(160, 128, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        Frame f = tests::frameWith(src);
        f.addPayload(anc);

        X264VideoEncoder enc; // captions enabled by default
        auto             pkts = encodeFrame(enc, f);
        REQUIRE(pkts.size() == 1);
        CHECK(seiPayloadTypes(pkts[0]).contains(VideoEncoderSei::TypeUserDataRegistered));
}

TEST_CASE("X264VideoEncoder_CaptionSeiDisabled") {
        AncPayload::Ptr anc = makeCaptionAnc();
        REQUIRE(anc.isValid());

        auto  src = makePlanarYUV(160, 128, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        Frame f = tests::frameWith(src);
        f.addPayload(anc);

        X264VideoEncoder enc;
        MediaConfig      cfg;
        cfg.set(MediaConfig::VideoSeiCaptionsEnabled, false);
        enc.configure(cfg);
        auto pkts = encodeFrame(enc, f);
        REQUIRE(pkts.size() == 1);
        CHECK_FALSE(seiPayloadTypes(pkts[0]).contains(VideoEncoderSei::TypeUserDataRegistered));
}

// Helper: encode one frame and parse the SPS out of its parameter sets.
static bool encodeAndParseSps(X264VideoEncoder &enc, const UncompressedVideoPayload::Ptr &src,
                              H264Bitstream::SpsInfo &sps) {
        auto pkts = encodeOne(enc, src);
        if (pkts.size() != 1) return false;
        const String params = pkts[0]->metadata().get(Metadata::CodecParameterSets).get<String>();
        return parseSpsFrom(params, sps);
}

// ---------------------------------------------------------------------------
// Profile auto-selection — geometry → profile_idc in the emitted SPS
// (High=100, High10=110, High422=122, High444=244).
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_ProfileAuto_Idc") {
        struct Case {
                        PixelFormat::ID id;
                        int             bitDepth;
                        int             chromaIDC;
                        int             profileIdc;
        };
        const Case cases[] = {
                {PixelFormat::YUV8_420_Planar_Rec709, 8, 1, 100},     // High
                {PixelFormat::YUV8_422_Planar_Rec709, 8, 2, 122},     // High 4:2:2
                {PixelFormat::YUV8_444_Planar_Rec709, 8, 3, 244},     // High 4:4:4
                {PixelFormat::YUV10_420_Planar_LE_Rec709, 10, 1, 110}, // High 10
                {PixelFormat::YUV10_422_Planar_LE_Rec709, 10, 2, 122}, // High 4:2:2
                {PixelFormat::YUV10_444_Planar_LE_Rec709, 10, 3, 244}, // High 4:4:4
        };
        for (const auto &c : cases) {
                X264VideoEncoder       enc;
                auto                   src = makePlanarYUV(160, 128, c.id, c.bitDepth, c.chromaIDC);
                H264Bitstream::SpsInfo sps;
                REQUIRE_MESSAGE(encodeAndParseSps(enc, src, sps), "no SPS for ", (int)c.id);
                CHECK(sps.profileIdc == c.profileIdc);
        }
}

// ---------------------------------------------------------------------------
// Explicit profile / level override (VideoProfile / VideoLevel wire tokens)
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_ProfileLevelOverride") {
        SUBCASE("baseline + level 3.1") {
                X264VideoEncoder enc;
                MediaConfig      cfg;
                cfg.set(MediaConfig::VideoProfile, String("baseline"));
                cfg.set(MediaConfig::VideoLevel, String("3.1"));
                enc.configure(cfg);
                auto                   src = makePlanarYUV(320, 240, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
                H264Bitstream::SpsInfo sps;
                REQUIRE(encodeAndParseSps(enc, src, sps));
                CHECK(sps.profileIdc == 66); // Constrained Baseline
                CHECK(sps.levelIdc == 31);
        }
        SUBCASE("main profile") {
                X264VideoEncoder enc;
                MediaConfig      cfg;
                cfg.set(MediaConfig::VideoProfile, String("main"));
                enc.configure(cfg);
                auto                   src = makePlanarYUV(320, 240, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
                H264Bitstream::SpsInfo sps;
                REQUIRE(encodeAndParseSps(enc, src, sps));
                CHECK(sps.profileIdc == 77); // Main
        }
}

// ---------------------------------------------------------------------------
// Lossless at any chroma — x264 has no 4:4:4-only input restriction (unlike
// NVENC, which rejects lossless on non-4:4:4 input).  H.264 transform-bypass
// lossless is only legal in High 4:4:4 Predictive, so x264 signals
// profile_idc 244 for every chroma while still encoding the native 4:2:0 /
// 4:2:2 / 4:4:4 content.  (Pixel-exact round-trip is a decoder-dependent
// functest; here we confirm the lossless path encodes a valid keyframe and
// preserves the input chroma at every subsampling.)
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_LosslessAnyChroma") {
        struct Case {
                        PixelFormat::ID id;
                        int             chromaIDC;
        };
        const Case cases[] = {
                {PixelFormat::YUV8_420_Planar_Rec709, 1},
                {PixelFormat::YUV8_422_Planar_Rec709, 2},
                {PixelFormat::YUV8_444_Planar_Rec709, 3},
        };
        for (const auto &c : cases) {
                X264VideoEncoder enc;
                MediaConfig      cfg;
                cfg.set(MediaConfig::VideoRcMode, RateControlMode::CQP);
                cfg.set(MediaConfig::VideoQp, 0); // lossless
                enc.configure(cfg);
                auto src = makePlanarYUV(160, 128, c.id, 8, c.chromaIDC);
                auto pkts = encodeOne(enc, src);
                REQUIRE_MESSAGE(pkts.size() == 1, "lossless encode failed for ", (int)c.id);
                CHECK(pkts[0]->isKeyframe());
                CHECK(pkts[0]->plane(0).size() > 0);
                H264Bitstream::SpsInfo sps;
                const String           params = pkts[0]->metadata().get(Metadata::CodecParameterSets).get<String>();
                REQUIRE(parseSpsFrom(params, sps));
                CHECK(sps.chromaFormatIdc == c.chromaIDC);
                // Transform-bypass lossless ⇒ High 4:4:4 Predictive (244)
                // regardless of the (preserved) chroma subsampling.
                CHECK(sps.profileIdc == 244);
        }
}

// ---------------------------------------------------------------------------
// Sample aspect ratio (VUI / SAR)
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_SarSignalled") {
        X264VideoEncoder enc;
        MediaConfig      cfg;
        cfg.set(MediaConfig::VideoPixelAspect, PixelAspect(16, 11)); // anamorphic SD
        enc.configure(cfg);
        auto                   src = makePlanarYUV(320, 240, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        H264Bitstream::SpsInfo sps;
        REQUIRE(encodeAndParseSps(enc, src, sps));
        CHECK(sps.sarWidth == 16);
        CHECK(sps.sarHeight == 11);
}

TEST_CASE("X264VideoEncoder_SquareSar_NotSignalled") {
        X264VideoEncoder       enc; // no VideoPixelAspect → square, no VUI SAR
        auto                   src = makePlanarYUV(320, 240, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        H264Bitstream::SpsInfo sps;
        REQUIRE(encodeAndParseSps(enc, src, sps));
        CHECK(sps.sarWidth == 0);
        CHECK(sps.sarHeight == 0);
}

// ---------------------------------------------------------------------------
// Interlaced — VideoScanMode drives x264's native MBAFF (b_interlaced), so
// the SPS clears frame_mbs_only_flag.
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_Interlaced_SpsSignalsFieldCoding") {
        SUBCASE("interlaced (TFF) → frame_mbs_only_flag = 0") {
                X264VideoEncoder enc;
                MediaConfig      cfg;
                cfg.set(MediaConfig::VideoScanMode, VideoScanMode::InterlacedEvenFirst);
                enc.configure(cfg);
                auto                   src = makePlanarYUV(320, 256, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
                H264Bitstream::SpsInfo sps;
                REQUIRE(encodeAndParseSps(enc, src, sps));
                CHECK_FALSE(sps.frameMbsOnly);
        }
        SUBCASE("progressive → frame_mbs_only_flag = 1") {
                X264VideoEncoder       enc;
                auto                   src = makePlanarYUV(320, 256, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
                H264Bitstream::SpsInfo sps;
                REQUIRE(encodeAndParseSps(enc, src, sps));
                CHECK(sps.frameMbsOnly);
        }
}

// ---------------------------------------------------------------------------
// Timecode rides as payload metadata — x264 owns the pic_timing SEI and
// hardcodes clock_timestamp_flag=0, so SMPTE timecode can't be injected
// in-band; it is preserved as Metadata::Timecode across the encode instead.
// ---------------------------------------------------------------------------

TEST_CASE("X264VideoEncoder_TimecodeRidesAsMetadata") {
        X264VideoEncoder enc;
        auto             src = makePlanarYUV(160, 128, PixelFormat::YUV8_420_Planar_Rec709, 8, 1);
        src.modify()->desc().metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 2, 15, 30, 12));
        auto pkts = encodeOne(enc, src);
        REQUIRE(pkts.size() == 1);
        const Timecode tc = pkts[0]->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.hour() == 2);
        CHECK(tc.min() == 15);
        CHECK(tc.frame() == 12);
}
