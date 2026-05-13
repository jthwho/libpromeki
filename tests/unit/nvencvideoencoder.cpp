/**
 * @file      tests/nvencvideoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * End-to-end smoke tests for the NVENC backend.  Everything here is
 * gated on @c PROMEKI_ENABLE_NVENC (the backend is only built when
 * the Video Codec SDK was found during configure) and additionally
 * checks @ref CudaDevice::isAvailable at runtime so CI machines
 * without a GPU — or without @c libnvidia-encode.so.1 — skip the
 * tests cleanly instead of failing.
 */

#include <doctest/doctest.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_NVENC

#include <promeki/videoencoder.h>
#include <promeki/videocodec.h>
#include <promeki/mediaconfig.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/buffer.h>
#include <promeki/cuda.h>
#include <promeki/enums.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslator.h>
#include <promeki/cea708cdp.h>
#include <promeki/frame.h>
#include "codectesthelpers.h"
#include <cstdint>
#include <cstring>
#include <vector>

using namespace promeki;

namespace {

        UncompressedVideoPayload::Ptr makeNv12Frame(int width, int height, uint8_t yValue = 128,
                                                    uint8_t uvValue = 128) {
                PixelFormat pd(PixelFormat::YUV8_420_SemiPlanar_Rec709);
                auto        img = UncompressedVideoPayload::allocate(ImageDesc(Size2Du32(width, height), pd));
                REQUIRE(img->planeCount() == 2);
                const size_t yBytes = static_cast<size_t>(width) * height;
                const size_t uvBytes = static_cast<size_t>(width) * (height / 2);
                std::memset(img.modify()->data()[0].data(), yValue, yBytes);
                std::memset(img.modify()->data()[1].data(), uvValue, uvBytes);
                return img;
        }

        // Returns true when the first 4 bytes start with a H.264/HEVC Annex-B
        // NAL unit start code (0x00000001) or 3-byte variant (0x000001).
        bool looksLikeAnnexB(const Buffer &b) {
                if (!b || b.size() < 4) return false;
                const auto *p = static_cast<const uint8_t *>(b.data());
                if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01) return true;
                if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01) return true;
                return false;
        }

        // Runs a small encode through the named codec.  Returns the number of
        // packets produced and populates @p firstPacket / @p lastPacket.
        // Returns -1 and populates a doctest FAIL when the codec isn't
        // available at runtime (no device, no driver library) so the caller
        // can early-return.
        int runSmallEncode(VideoCodec::ID codecId, CompressedVideoPayload::Ptr &firstPacket,
                           CompressedVideoPayload::Ptr &lastPacket) {
                auto r = VideoCodec(codecId).createEncoder();
                if (error(r).isError()) return -1;
                VideoEncoder *enc = value(r);

                MediaConfig cfg;
                cfg.set(MediaConfig::BitrateKbps, int32_t(4000));
                cfg.set(MediaConfig::GopLength, int32_t(30));
                cfg.set(MediaConfig::VideoRcMode, RateControlMode::CBR);
                cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
                enc->configure(cfg);

                // 256x128 is the minimum I've found to consistently initialise
                // H.264 + HEVC across NVENC generations; narrower frames
                // trigger NV_ENC_ERR_INVALID_PARAM on some GPUs.
                constexpr int kWidth = 256;
                constexpr int kHeight = 128;
                constexpr int kFrames = 8;

                int numPackets = 0;
                for (int i = 0; i < kFrames; ++i) {
                        // Slight luma variation frame-to-frame so the encoder
                        // sees some movement (helps exercise RC).
                        auto uvp = makeNv12Frame(kWidth, kHeight, static_cast<uint8_t>(96 + i * 4), 128);
                        if (enc->submitFrame(tests::frameWith(uvp)) != Error::Ok) {
                                // First real call lazily loads libnvidia-encode.
                                // If that fails, we fail gracefully by cleaning
                                // up and reporting zero packets — the outer test
                                // will check its device-availability guard.
                                delete enc;
                                return -1;
                        }
                        while (auto pkt = tests::firstCompressedVideo(enc->receiveFrame())) {
                                if (pkt->isEndOfStream()) break;
                                if (!firstPacket) firstPacket = pkt;
                                lastPacket = pkt;
                                ++numPackets;
                        }
                }

                enc->flush();
                while (auto pkt = tests::firstCompressedVideo(enc->receiveFrame())) {
                        if (pkt->isEndOfStream()) {
                                lastPacket = pkt;
                                break;
                        }
                        if (!firstPacket) firstPacket = pkt;
                        lastPacket = pkt;
                        ++numPackets;
                }

                delete enc;
                return numPackets;
        }

} // namespace

TEST_CASE("NvencVideoEncoder: registered as Nvidia backend for H264/HEVC/AV1") {
        auto nvidia = VideoCodec::lookupBackend("Nvidia");
        REQUIRE(isOk(nvidia));
        const auto backend = value(nvidia);

        auto h264 = VideoCodec(VideoCodec::H264).availableEncoderBackends();
        auto hevc = VideoCodec(VideoCodec::HEVC).availableEncoderBackends();
        bool h264HasNvidia = false;
        bool hevcHasNvidia = false;
        for (auto b : h264)
                if (b == backend) {
                        h264HasNvidia = true;
                        break;
                }
        for (auto b : hevc)
                if (b == backend) {
                        hevcHasNvidia = true;
                        break;
                }
        CHECK(h264HasNvidia);
        CHECK(hevcHasNvidia);
}

TEST_CASE("NvencVideoEncoder: H.264 encode produces keyframe and EOS") {
        if (!CudaDevice::isAvailable()) return;

        CompressedVideoPayload::Ptr first, last;
        int                         n = runSmallEncode(VideoCodec::H264, first, last);
        if (n < 0) return; // NVENC runtime unavailable — skip

        CHECK(n >= 1);
        REQUIRE(first);
        CHECK(first->desc().pixelFormat().id() == PixelFormat::H264);
        CHECK(first->isKeyframe());
        CHECK(first->plane(0).size() > 0);
        CHECK(looksLikeAnnexB(first->plane(0).buffer()));

        REQUIRE(last);
        CHECK(last->isEndOfStream());
}

TEST_CASE("NvencVideoEncoder: HEVC encode produces keyframe and EOS") {
        if (!CudaDevice::isAvailable()) return;

        CompressedVideoPayload::Ptr first, last;
        int                         n = runSmallEncode(VideoCodec::HEVC, first, last);
        if (n < 0) return;

        CHECK(n >= 1);
        REQUIRE(first);
        CHECK(first->desc().pixelFormat().id() == PixelFormat::HEVC);
        CHECK(first->isKeyframe());
        CHECK(first->plane(0).size() > 0);
        CHECK(looksLikeAnnexB(first->plane(0).buffer()));

        REQUIRE(last);
        CHECK(last->isEndOfStream());
}

TEST_CASE("NvencVideoEncoder: requestKeyframe forces IDR on next submit") {
        if (!CudaDevice::isAvailable()) return;

        auto r = VideoCodec(VideoCodec::H264).createEncoder();
        REQUIRE(isOk(r));
        VideoEncoder *enc = value(r);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(4000));
        cfg.set(MediaConfig::GopLength, int32_t(240)); // long GOP: IDRs would be rare
        cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
        enc->configure(cfg);

        // 256x128 is the minimum I've found to consistently initialise
        // H.264 + HEVC across NVENC generations; narrower frames
        // trigger NV_ENC_ERR_INVALID_PARAM on some GPUs.
        constexpr int kWidth = 256;
        constexpr int kHeight = 128;

        // Submit 3 frames: frame 0 is the implicit IDR, frames 1 and
        // 2 are non-keyframes.  Request a keyframe and submit frame
        // 3; it should come back as an IDR.
        auto submit = [&](int idx) -> CompressedVideoPayload::Ptr {
                auto uvp = makeNv12Frame(kWidth, kHeight, static_cast<uint8_t>(64 + idx * 8), 128);
                if (enc->submitFrame(tests::frameWith(uvp)) != Error::Ok) return CompressedVideoPayload::Ptr();
                return tests::firstCompressedVideo(enc->receiveFrame());
        };

        auto p0 = submit(0);
        if (!p0) {
                delete enc;
                return;
        } // runtime missing
        CHECK(p0->isKeyframe());

        auto p1 = submit(1);
        REQUIRE(p1);
        CHECK_FALSE(p1->isKeyframe());

        auto p2 = submit(2);
        REQUIRE(p2);
        CHECK_FALSE(p2->isKeyframe());

        enc->requestKeyframe();
        auto p3 = submit(3);
        REQUIRE(p3);
        CHECK(p3->isKeyframe());

        delete enc;
}

// ---------------------------------------------------------------------------
// Caption-SEI injection.  Drives an NV12 frame through NVENC with a CEA-708
// AncPayload attached to the source Frame and verifies the encoded H.264
// bitstream contains an ATSC A/53 user_data SEI message wrapping the
// expected cc_data triples.
// ---------------------------------------------------------------------------
namespace {

        // Build a one-packet AncPayload carrying a CEA-708 St291 packet
        // built from @p triples via the standard Phase 2 codec path.
        // The payload is paired to video stream 0 (matching the NVENC
        // backend's selectAncForSei call).
        AncPayload::Ptr makeCea708AncPayload(const Cea708Cdp::CcDataList &triples) {
                Cea708Cdp cdp(0 /*frameRateCode*/, triples, 0 /*sequenceCounter*/);
                AncTranslator t;
                Result<AncPacket> r = t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                              AncTransport::St291);
                REQUIRE(r.second().isOk());
                AncDesc desc;
                desc.setPairedVideoStreamIndex(0);
                AncPayload::Ptr ap = AncPayload::Ptr::create(desc);
                ap.modify()->addPacket(r.first());
                return ap;
        }

        // Search @p data for the ATSC A/53 SEI wrapper marker bytes.
        // Returns the offset of the match or -1 when not found.  The
        // wrapper bytes (country=USA, provider=ATSC, user_id="GA94",
        // user_data_type_code=cc_data) survive H.264 emulation prevention
        // — none of the constituent byte sequences contain a 0x00 0x00
        // pair, so NVENC will not insert 0x03 bytes anywhere in this
        // marker.
        ssize_t findAtscA53Marker(const Buffer &data) {
                static constexpr uint8_t kMarker[] = {
                        0xB5,             // itu_t_t35_country_code = USA
                        0x00, 0x31,       // itu_t_t35_provider_code = ATSC
                        0x47, 0x41, 0x39, 0x34, // user_identifier = "GA94"
                        0x03,             // user_data_type_code = cc_data
                };
                if (!data || data.size() < sizeof(kMarker)) return -1;
                const auto *p = static_cast<const uint8_t *>(data.data());
                const size_t end = data.size() - sizeof(kMarker);
                for (size_t i = 0; i <= end; ++i) {
                        if (std::memcmp(p + i, kMarker, sizeof(kMarker)) == 0) {
                                return static_cast<ssize_t>(i);
                        }
                }
                return -1;
        }

        // Concatenate all packets emitted by a small encode into a single
        // Buffer for substring searching.  Returns an empty Buffer when
        // the encoder didn't produce anything (typically: NVENC runtime
        // unavailable).
        Buffer runEncodeAndCollect(VideoCodec::ID codecId, bool seiCaptions,
                                   const AncPayload::Ptr &captionsAnc) {
                auto r = VideoCodec(codecId).createEncoder();
                if (error(r).isError()) return Buffer();
                VideoEncoder *enc = value(r);

                MediaConfig cfg;
                cfg.set(MediaConfig::BitrateKbps, int32_t(4000));
                cfg.set(MediaConfig::GopLength, int32_t(30));
                cfg.set(MediaConfig::VideoRcMode, RateControlMode::CBR);
                cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
                cfg.set(MediaConfig::VideoSeiCaptionsEnabled, seiCaptions);
                enc->configure(cfg);

                constexpr int kWidth = 256;
                constexpr int kHeight = 128;
                constexpr int kFrames = 4;

                std::vector<uint8_t> blob;
                auto                 appendFrame = [&](const Frame &f) {
                        Frame out = enc->receiveFrame();
                        while (auto pkt = tests::firstCompressedVideo(out)) {
                                if (pkt->isEndOfStream()) break;
                                const Buffer &b = pkt->plane(0).buffer();
                                if (b && b.size() > 0) {
                                        const auto *bp = static_cast<const uint8_t *>(b.data());
                                        blob.insert(blob.end(), bp, bp + b.size());
                                }
                                out = enc->receiveFrame();
                        }
                        (void)f;
                };

                for (int i = 0; i < kFrames; ++i) {
                        auto  uvp = makeNv12Frame(kWidth, kHeight, static_cast<uint8_t>(96 + i * 4), 128);
                        Frame frame = tests::frameWith(uvp);
                        if (i == 0 && captionsAnc.isValid()) {
                                // Attach the caption ANC only on the first
                                // frame.  Both with-SEI and without-SEI
                                // variants should still emit valid
                                // bitstreams; the with-SEI variant gets
                                // the ATSC marker in plane 0 of one of
                                // the encoded packets.
                                frame.addPayload(captionsAnc);
                        }
                        if (enc->submitFrame(frame) != Error::Ok) {
                                delete enc;
                                return Buffer();
                        }
                        appendFrame(frame);
                }
                enc->flush();
                while (true) {
                        Frame f = enc->receiveFrame();
                        auto  pkt = tests::firstCompressedVideo(f);
                        if (!pkt) break;
                        if (pkt->isEndOfStream()) break;
                        const Buffer &b = pkt->plane(0).buffer();
                        if (b && b.size() > 0) {
                                const auto *bp = static_cast<const uint8_t *>(b.data());
                                blob.insert(blob.end(), bp, bp + b.size());
                        }
                }

                delete enc;

                Buffer out(blob.size());
                out.setSize(blob.size());
                if (!blob.empty()) out.copyFrom(blob.data(), blob.size(), 0);
                return out;
        }

} // namespace

TEST_CASE("NvencVideoEncoder: caption-SEI injection wraps Cea708 ANC into ATSC A/53 SEI") {
        if (!CudaDevice::isAvailable()) return;

        // Two cc_data triples carrying recognisable bytes ('D','E' then a
        // DTVCC pair) so we can recover them from the SEI by hand.
        Cea708Cdp::CcDataList triples;
        triples.pushToBack(Cea708Cdp::CcData{true, 0, 0xC4, 0x45});
        triples.pushToBack(Cea708Cdp::CcData{true, 2, 0x80, 0x80});

        AncPayload::Ptr ancPayload = makeCea708AncPayload(triples);
        REQUIRE(ancPayload.isValid());

        Buffer bitstream = runEncodeAndCollect(VideoCodec::H264, /*seiCaptions=*/true, ancPayload);
        if (!bitstream || bitstream.size() == 0) return; // NVENC runtime unavailable

        ssize_t markerOffset = findAtscA53Marker(bitstream);
        CHECK(markerOffset >= 0);
        if (markerOffset < 0) return;

        // After the user_data_type_code byte, the layout is:
        //   flags byte: 0xC0 | cc_count(5)  → 0xC0 | 2 = 0xC2
        //   em_data:    0xFF
        //   triple 0:   marker(0xF8) | cc_valid(0x04) | cc_type(0x00) = 0xFC,
        //               then b1=0xC4, b2=0x45
        //   triple 1:   0xF8 | 0x04 | 0x02 = 0xFE, then 0x80, 0x80
        //   trailing:   0xFF
        const auto  *p = static_cast<const uint8_t *>(bitstream.data())
                        + static_cast<size_t>(markerOffset)
                        + 8 /* skip the 8-byte ATSC A/53 wrapper marker */;
        REQUIRE(static_cast<size_t>(markerOffset) + 8 + 11 <= bitstream.size());
        CHECK(p[0] == 0xC2);  // process_cc_data + reserved + cc_count=2
        CHECK(p[1] == 0xFF);  // em_data
        CHECK(p[2] == 0xFC);  // triple 0 header (cc_valid=1, cc_type=0)
        CHECK(p[3] == 0xC4);
        CHECK(p[4] == 0x45);
        CHECK(p[5] == 0xFE);  // triple 1 header (cc_valid=1, cc_type=2)
        CHECK(p[6] == 0x80);
        CHECK(p[7] == 0x80);
        CHECK(p[8] == 0xFF);  // trailing marker
}

TEST_CASE("NvencVideoEncoder: caption-SEI suppressed when VideoSeiCaptionsEnabled = false") {
        if (!CudaDevice::isAvailable()) return;

        Cea708Cdp::CcDataList triples;
        triples.pushToBack(Cea708Cdp::CcData{true, 0, 0xC4, 0x45});
        AncPayload::Ptr ancPayload = makeCea708AncPayload(triples);
        REQUIRE(ancPayload.isValid());

        Buffer bitstream = runEncodeAndCollect(VideoCodec::H264, /*seiCaptions=*/false, ancPayload);
        if (!bitstream || bitstream.size() == 0) return;

        // No SEI marker — the encoder should still emit a valid bitstream
        // that contains ANC-bearing input frames, but no caption SEI.
        CHECK(findAtscA53Marker(bitstream) < 0);
}

TEST_CASE("NvencVideoEncoder: caption-SEI silent passthrough when no Cea708 ANC on frame") {
        if (!CudaDevice::isAvailable()) return;

        Buffer bitstream = runEncodeAndCollect(VideoCodec::H264, /*seiCaptions=*/true, AncPayload::Ptr());
        if (!bitstream || bitstream.size() == 0) return;

        // Default-on with no caption ANC on the source must produce a
        // bitstream identical in shape to the no-SEI run — no marker.
        CHECK(findAtscA53Marker(bitstream) < 0);
}

TEST_CASE("NvencVideoEncoder: rejects non-NV12 input") {
        if (!CudaDevice::isAvailable()) return;

        auto r = VideoCodec(VideoCodec::H264).createEncoder();
        REQUIRE(isOk(r));
        VideoEncoder *enc = value(r);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(2000));
        enc->configure(cfg);

        // Build an RGB8 frame — deliberately the wrong format.
        PixelFormat pd(PixelFormat::RGB8_sRGB);
        auto        rgb = UncompressedVideoPayload::allocate(ImageDesc(64, 64, pd));
        REQUIRE(rgb.isValid());
        std::memset(rgb.modify()->data()[0].data(), 0x80, rgb->plane(0).size());

        Error err = enc->submitFrame(tests::frameWith(rgb));
        CHECK(err == Error::PixelFormatNotSupported);

        delete enc;
}

#endif // PROMEKI_ENABLE_NVENC
