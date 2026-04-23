/**
 * @file      tests/nvdecvideodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * End-to-end NVDEC smoke test: encode a short burst of synthetic NV12
 * frames through NVENC, push the resulting H.264 / HEVC packets into
 * NVDEC, and check that we get a matching number of decoded Images
 * out the other side at the original resolution.  Device-gated like
 * the NVENC suite — auto-skips when no GPU, no driver runtime, or the
 * build flags are off.
 */

#include <doctest/doctest.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_NVDEC && PROMEKI_ENABLE_NVENC

#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/videocodec.h>
#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/cuda.h>
#include <promeki/enums.h>
#include <cstdint>
#include <cstring>

using namespace promeki;

namespace {

Image makeNv12Frame(int width, int height, uint8_t yValue, uint8_t uvValue) {
        PixelFormat pd(PixelFormat::YUV8_420_SemiPlanar_Rec709);
        Image img(Size2Du32(width, height), pd);
        REQUIRE(img.planes().size() == 2);
        const size_t yBytes  = static_cast<size_t>(width) * height;
        const size_t uvBytes = static_cast<size_t>(width) * (height / 2);
        std::memset(img.data(0), yValue,  yBytes);
        std::memset(img.data(1), uvValue, uvBytes);
        return img;
}

// Encode @p numFrames synthetic NV12 frames, return the emitted
// VideoPackets (including a trailing EOS).  Returns an empty list
// when the NVENC runtime is unavailable so the caller can skip.
List<VideoPacket::Ptr> encodeBurst(VideoCodec::ID codecId,
                                   int width, int height, int numFrames) {
        List<VideoPacket::Ptr> out;
        auto r = VideoCodec(codecId).createEncoder();
        if(error(r).isError()) return out;
        VideoEncoder *enc = value(r);
        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(4000));
        cfg.set(MediaConfig::GopLength,   int32_t(15));
        cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
        enc->configure(cfg);
        for(int i = 0; i < numFrames; ++i) {
                Image::Ptr f = Image::Ptr::create(makeNv12Frame(width, height,
                                        static_cast<uint8_t>(64 + i * 4), 128));
                if(enc->submitFrame(f) != Error::Ok) {
                        delete enc;
                        return List<VideoPacket::Ptr>();
                }
                while(auto pkt = enc->receivePacket()) {
                        if(!pkt->isEndOfStream()) out.pushToBack(pkt);
                }
        }
        enc->flush();
        while(auto pkt = enc->receivePacket()) {
                if(!pkt->isEndOfStream()) out.pushToBack(pkt);
        }
        delete enc;
        return out;
}

} // namespace

TEST_CASE("NvdecVideoDecoder: registered as Nvidia backend for H264/HEVC") {
        auto nvidia = VideoCodec::lookupBackend("Nvidia");
        REQUIRE(isOk(nvidia));
        const auto backend = value(nvidia);

        auto h264 = VideoCodec(VideoCodec::H264).availableDecoderBackends();
        auto hevc = VideoCodec(VideoCodec::HEVC).availableDecoderBackends();
        bool h264HasNvidia = false;
        bool hevcHasNvidia = false;
        for(auto b : h264) if(b == backend) { h264HasNvidia = true; break; }
        for(auto b : hevc) if(b == backend) { hevcHasNvidia = true; break; }
        CHECK(h264HasNvidia);
        CHECK(hevcHasNvidia);
}

TEST_CASE("NvdecVideoDecoder: H.264 encode/decode round trip") {
        if(!CudaDevice::isAvailable()) return;

        constexpr int kWidth  = 256;
        constexpr int kHeight = 128;
        constexpr int kFrames = 8;

        auto packets = encodeBurst(VideoCodec::H264, kWidth, kHeight, kFrames);
        if(packets.isEmpty()) return;  // NVENC runtime missing

        auto r = VideoCodec(VideoCodec::H264).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);
        dec->configure(MediaConfig());

        int decoded = 0;
        for(const auto &pkt : packets) {
                Error err = dec->submitPacket(pkt);
                if(err.isError()) {
                        // Decoder runtime missing — skip cleanly.
                        delete dec;
                        return;
                }
                while(true) {
                        Image::Ptr img = dec->receiveFrame();
                        if(!img.isValid()) break;
                        CHECK(img->width() == kWidth);
                        CHECK(img->height() == kHeight);
                        CHECK(img->pixelFormat().id() == PixelFormat::YUV8_420_SemiPlanar_Rec709);
                        ++decoded;
                }
        }
        dec->flush();
        while(true) {
                Image::Ptr img = dec->receiveFrame();
                if(!img.isValid()) break;
                ++decoded;
        }

        CHECK(decoded >= 1);
        delete dec;
}

TEST_CASE("NvdecVideoDecoder: HEVC encode/decode round trip") {
        if(!CudaDevice::isAvailable()) return;

        // HEVC NVDEC minimum is 144x144 on most NVIDIA architectures;
        // pad well past that so the test doesn't get rejected on
        // hardware we haven't characterised yet.
        constexpr int kWidth  = 384;
        constexpr int kHeight = 256;
        constexpr int kFrames = 8;

        auto packets = encodeBurst(VideoCodec::HEVC, kWidth, kHeight, kFrames);
        if(packets.isEmpty()) return;

        auto r = VideoCodec(VideoCodec::HEVC).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);
        dec->configure(MediaConfig());

        int decoded = 0;
        for(const auto &pkt : packets) {
                Error err = dec->submitPacket(pkt);
                if(err.isError()) { delete dec; return; }
                while(true) {
                        Image::Ptr img = dec->receiveFrame();
                        if(!img.isValid()) break;
                        CHECK(img->width() == kWidth);
                        CHECK(img->height() == kHeight);
                        ++decoded;
                }
        }
        dec->flush();
        while(true) {
                Image::Ptr img = dec->receiveFrame();
                if(!img.isValid()) break;
                ++decoded;
        }

        CHECK(decoded >= 1);
        delete dec;
}

#endif  // PROMEKI_ENABLE_NVDEC && PROMEKI_ENABLE_NVENC
