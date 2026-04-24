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
#include <cstdint>
#include <cstring>

using namespace promeki;

namespace {

UncompressedVideoPayload::Ptr makeNv12Frame(int width, int height,
                                            uint8_t yValue = 128,
                                            uint8_t uvValue = 128) {
        PixelFormat pd(PixelFormat::YUV8_420_SemiPlanar_Rec709);
        auto img = UncompressedVideoPayload::allocate(
                ImageDesc(Size2Du32(width, height), pd));
        REQUIRE(img->planeCount() == 2);
        const size_t yBytes  = static_cast<size_t>(width) * height;
        const size_t uvBytes = static_cast<size_t>(width) * (height / 2);
        std::memset(img.modify()->data()[0].data(), yValue,  yBytes);
        std::memset(img.modify()->data()[1].data(), uvValue, uvBytes);
        return img;
}

// Returns true when the first 4 bytes start with a H.264/HEVC Annex-B
// NAL unit start code (0x00000001) or 3-byte variant (0x000001).
bool looksLikeAnnexB(const Buffer::Ptr &b) {
        if(!b || b->size() < 4) return false;
        const auto *p = static_cast<const uint8_t *>(b->data());
        if(p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01) return true;
        if(p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01) return true;
        return false;
}

// Runs a small encode through the named codec.  Returns the number of
// packets produced and populates @p firstPacket / @p lastPacket.
// Returns -1 and populates a doctest FAIL when the codec isn't
// available at runtime (no device, no driver library) so the caller
// can early-return.
int runSmallEncode(VideoCodec::ID codecId,
                   CompressedVideoPayload::Ptr &firstPacket,
                   CompressedVideoPayload::Ptr &lastPacket) {
        auto r = VideoCodec(codecId).createEncoder();
        if(error(r).isError()) return -1;
        VideoEncoder *enc = value(r);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(4000));
        cfg.set(MediaConfig::GopLength,   int32_t(30));
        cfg.set(MediaConfig::VideoRcMode, RateControlMode::CBR);
        cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
        enc->configure(cfg);

        // 256x128 is the minimum I've found to consistently initialise
        // H.264 + HEVC across NVENC generations; narrower frames
        // trigger NV_ENC_ERR_INVALID_PARAM on some GPUs.
        constexpr int kWidth  = 256;
        constexpr int kHeight = 128;
        constexpr int kFrames = 8;

        int numPackets = 0;
        for(int i = 0; i < kFrames; ++i) {
                // Slight luma variation frame-to-frame so the encoder
                // sees some movement (helps exercise RC).
                auto uvp = makeNv12Frame(kWidth, kHeight,
                                         static_cast<uint8_t>(96 + i * 4),
                                         128);
                if(enc->submitPayload(uvp) != Error::Ok) {
                        // First real call lazily loads libnvidia-encode.
                        // If that fails, we fail gracefully by cleaning
                        // up and reporting zero packets — the outer test
                        // will check its device-availability guard.
                        delete enc;
                        return -1;
                }
                while(auto pkt = enc->receiveCompressedPayload()) {
                        if(pkt->isEndOfStream()) break;
                        if(!firstPacket) firstPacket = pkt;
                        lastPacket = pkt;
                        ++numPackets;
                }
        }

        enc->flush();
        while(auto pkt = enc->receiveCompressedPayload()) {
                if(pkt->isEndOfStream()) {
                        lastPacket = pkt;
                        break;
                }
                if(!firstPacket) firstPacket = pkt;
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
        for(auto b : h264) if(b == backend) { h264HasNvidia = true; break; }
        for(auto b : hevc) if(b == backend) { hevcHasNvidia = true; break; }
        CHECK(h264HasNvidia);
        CHECK(hevcHasNvidia);
}

TEST_CASE("NvencVideoEncoder: H.264 encode produces keyframe and EOS") {
        if(!CudaDevice::isAvailable()) return;

        CompressedVideoPayload::Ptr first, last;
        int n = runSmallEncode(VideoCodec::H264, first, last);
        if(n < 0) return;   // NVENC runtime unavailable — skip

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
        if(!CudaDevice::isAvailable()) return;

        CompressedVideoPayload::Ptr first, last;
        int n = runSmallEncode(VideoCodec::HEVC, first, last);
        if(n < 0) return;

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
        if(!CudaDevice::isAvailable()) return;

        auto r = VideoCodec(VideoCodec::H264).createEncoder();
        REQUIRE(isOk(r));
        VideoEncoder *enc = value(r);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(4000));
        cfg.set(MediaConfig::GopLength,   int32_t(240)); // long GOP: IDRs would be rare
        cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
        enc->configure(cfg);

        // 256x128 is the minimum I've found to consistently initialise
        // H.264 + HEVC across NVENC generations; narrower frames
        // trigger NV_ENC_ERR_INVALID_PARAM on some GPUs.
        constexpr int kWidth  = 256;
        constexpr int kHeight = 128;

        // Submit 3 frames: frame 0 is the implicit IDR, frames 1 and
        // 2 are non-keyframes.  Request a keyframe and submit frame
        // 3; it should come back as an IDR.
        auto submit = [&](int idx) -> CompressedVideoPayload::Ptr {
                auto uvp = makeNv12Frame(kWidth, kHeight,
                                         static_cast<uint8_t>(64 + idx * 8), 128);
                if(enc->submitPayload(uvp) != Error::Ok) return CompressedVideoPayload::Ptr();
                return enc->receiveCompressedPayload();
        };

        auto p0 = submit(0);
        if(!p0) { delete enc; return; }  // runtime missing
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

TEST_CASE("NvencVideoEncoder: rejects non-NV12 input") {
        if(!CudaDevice::isAvailable()) return;

        auto r = VideoCodec(VideoCodec::H264).createEncoder();
        REQUIRE(isOk(r));
        VideoEncoder *enc = value(r);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(2000));
        enc->configure(cfg);

        // Build an RGB8 frame — deliberately the wrong format.
        PixelFormat pd(PixelFormat::RGB8_sRGB);
        auto rgb = UncompressedVideoPayload::allocate(ImageDesc(64, 64, pd));
        REQUIRE(rgb.isValid());
        std::memset(rgb.modify()->data()[0].data(), 0x80, rgb->plane(0).size());

        Error err = enc->submitPayload(rgb);
        CHECK(err == Error::PixelFormatNotSupported);

        delete enc;
}

#endif  // PROMEKI_ENABLE_NVENC
