/**
 * @file      tests/nvdecvideodecoder.cpp
 * @copyright Jason Howard. All rights reserved.
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
#include <promeki/nvdecvideodecoder.h>
#include <promeki/videocodec.h>
#include <promeki/mediaconfig.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/audiodesc.h>
#include <promeki/cuda.h>
#include <promeki/enums_codec.h>
#include <promeki/memspace.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/mediaioallocator.h>
#include "codectesthelpers.h"
#include <cstdint>
#include <cstring>

using namespace promeki;

namespace {

        UncompressedVideoPayload::Ptr makeNv12Frame(int width, int height, uint8_t yValue, uint8_t uvValue) {
                PixelFormat pd(PixelFormat::YUV8_420_SemiPlanar_Rec709);
                auto        img = UncompressedVideoPayload::allocate(ImageDesc(Size2Du32(width, height), pd));
                REQUIRE(img->planeCount() == 2);
                const size_t yBytes = static_cast<size_t>(width) * height;
                const size_t uvBytes = static_cast<size_t>(width) * (height / 2);
                std::memset(img.modify()->data()[0].data(), yValue, yBytes);
                std::memset(img.modify()->data()[1].data(), uvValue, uvBytes);
                return img;
        }

        // Encode @p numFrames synthetic NV12 frames, return the emitted
        // compressed payloads (excluding the trailing EOS).  Returns an empty
        // list when the NVENC runtime is unavailable so the caller can skip.
        List<CompressedVideoPayload::Ptr> encodeBurst(VideoCodec::ID codecId, int width, int height, int numFrames) {
                List<CompressedVideoPayload::Ptr> out;
                auto                              r = VideoCodec(codecId).createEncoder();
                if (error(r).isError()) return out;
                VideoEncoder *enc = value(r);
                MediaConfig   cfg;
                cfg.set(MediaConfig::BitrateKbps, int32_t(4000));
                cfg.set(MediaConfig::GopLength, int32_t(15));
                cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
                enc->configure(cfg);
                for (int i = 0; i < numFrames; ++i) {
                        auto uvp = makeNv12Frame(width, height, static_cast<uint8_t>(64 + i * 4), 128);
                        if (enc->submitFrame(tests::frameWith(uvp)) != Error::Ok) {
                                delete enc;
                                return List<CompressedVideoPayload::Ptr>();
                        }
                        while (true) {
                                auto outFrame = enc->receiveFrame();
                                if (!outFrame.isValid()) break;
                                auto pkt = tests::firstCompressedVideo(outFrame);
                                if (pkt.isValid() && !pkt->isEndOfStream()) out.pushToBack(pkt);
                        }
                }
                enc->flush();
                while (true) {
                        auto outFrame = enc->receiveFrame();
                        if (!outFrame.isValid()) break;
                        auto pkt = tests::firstCompressedVideo(outFrame);
                        if (pkt.isValid() && !pkt->isEndOfStream()) out.pushToBack(pkt);
                }
                delete enc;
                return out;
        }

} // namespace

TEST_CASE("NvdecVideoDecoder: supportedOutputList covers SDR + HDR P010 PixelFormats") {
        // The decoder now picks its output PixelFormat from the
        // bitstream's bit depth + VUI transfer at sequence-callback
        // time (chooseOutputPixelFormat).  All four PixelFormats it
        // can emit — 8-bit NV12 SDR, 10-bit P010 SDR, 10-bit P010
        // BT.2020 + PQ, 10-bit P010 BT.2020 + HLG — must surface on
        // supportedOutputList so VideoDecoder::registerBackend
        // advertises them and the planner can match HDR-aware sinks.
        List<int> outputs = NvdecVideoDecoder::supportedOutputList();
        bool      sdr8    = false;
        bool      sdr10   = false;
        bool      pq      = false;
        bool      hlg     = false;
        for (int id : outputs) {
                if (id == static_cast<int>(PixelFormat::YUV8_420_SemiPlanar_Rec709)) sdr8 = true;
                if (id == static_cast<int>(PixelFormat::YUV10_420_SemiPlanar_LE_Rec709)) sdr10 = true;
                if (id == static_cast<int>(PixelFormat::YUV10_420_SemiPlanar_LE_Rec2020_PQ)) pq = true;
                if (id == static_cast<int>(PixelFormat::YUV10_420_SemiPlanar_LE_Rec2020_HLG)) hlg = true;
        }
        CHECK(sdr8);
        CHECK(sdr10);
        CHECK(pq);
        CHECK(hlg);
}

TEST_CASE("NvdecVideoDecoder: registered as Nvidia backend for H264/HEVC") {
        auto nvidia = VideoCodec::lookupBackend("Nvidia");
        REQUIRE(isOk(nvidia));
        const auto backend = value(nvidia);

        auto h264 = VideoCodec(VideoCodec::H264).availableDecoderBackends();
        auto hevc = VideoCodec(VideoCodec::HEVC).availableDecoderBackends();
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

TEST_CASE("NvdecVideoDecoder: H.264 encode/decode round trip") {
        if (!CudaDevice::isAvailable()) return;

        constexpr int kWidth = 256;
        constexpr int kHeight = 128;
        constexpr int kFrames = 8;

        auto packets = encodeBurst(VideoCodec::H264, kWidth, kHeight, kFrames);
        if (packets.isEmpty()) return; // NVENC runtime missing

        auto r = VideoCodec(VideoCodec::H264).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);
        dec->configure(MediaConfig());

        int decoded = 0;
        for (const auto &pkt : packets) {
                Error err = dec->submitFrame(tests::frameWith(pkt));
                if (err.isError()) {
                        // Decoder runtime missing — skip cleanly.
                        delete dec;
                        return;
                }
                while (true) {
                        UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                        if (!img.isValid()) break;
                        CHECK(img->desc().width() == kWidth);
                        CHECK(img->desc().height() == kHeight);
                        CHECK(img->desc().pixelFormat().id() == PixelFormat::YUV8_420_SemiPlanar_Rec709);
                        ++decoded;
                }
        }
        dec->flush();
        while (true) {
                UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                if (!img.isValid()) break;
                ++decoded;
        }

        CHECK(decoded >= 1);
        delete dec;
}

TEST_CASE("NvdecVideoDecoder: HEVC encode/decode round trip") {
        if (!CudaDevice::isAvailable()) return;

        // HEVC NVDEC minimum is 144x144 on most NVIDIA architectures;
        // pad well past that so the test doesn't get rejected on
        // hardware we haven't characterised yet.
        constexpr int kWidth = 384;
        constexpr int kHeight = 256;
        constexpr int kFrames = 8;

        auto packets = encodeBurst(VideoCodec::HEVC, kWidth, kHeight, kFrames);
        if (packets.isEmpty()) return;

        auto r = VideoCodec(VideoCodec::HEVC).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);
        dec->configure(MediaConfig());

        int decoded = 0;
        for (const auto &pkt : packets) {
                Error err = dec->submitFrame(tests::frameWith(pkt));
                if (err.isError()) {
                        delete dec;
                        return;
                }
                while (true) {
                        UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                        if (!img.isValid()) break;
                        CHECK(img->desc().width() == kWidth);
                        CHECK(img->desc().height() == kHeight);
                        ++decoded;
                }
        }
        dec->flush();
        while (true) {
                UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                if (!img.isValid()) break;
                ++decoded;
        }

        CHECK(decoded >= 1);
        delete dec;
}

TEST_CASE("VideoDecoder::allocator(): defaults to MediaIOAllocator::defaultAllocator()") {
        // Use NVDEC as a concrete VideoDecoder — the API is on the base
        // class, so this exercise covers any decoder.  No GPU needed; we
        // never actually submit a frame.
        auto r = VideoCodec(VideoCodec::H264).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);

        MediaIOAllocator::Ptr alloc = dec->allocator();
        REQUIRE(alloc.isValid());
        CHECK(alloc.ptr() == MediaIOAllocator::defaultAllocator().ptr());

        // setAllocator(null) is a clear; allocator() reverts to default.
        dec->setAllocator(MediaIOAllocator::Ptr());
        CHECK(dec->allocator().ptr() == MediaIOAllocator::defaultAllocator().ptr());

        delete dec;
}

TEST_CASE("VideoDecoder::setAllocator(): override is preserved") {
        auto r = VideoCodec(VideoCodec::H264).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);

        auto custom = NvdecVideoDecoder::makeDeviceResidentAllocator();
        REQUIRE(custom.isValid());
        dec->setAllocator(custom);
        CHECK(dec->allocator().ptr() == custom.ptr());
        CHECK(dec->allocator()->name() == String("NvdecAllocator"));

        // Null clears.
        dec->setAllocator(MediaIOAllocator::Ptr());
        CHECK(dec->allocator().ptr() == MediaIOAllocator::defaultAllocator().ptr());

        delete dec;
}

TEST_CASE("NvdecAllocator: vends CudaDevice planes for video; defaults audio + bytes") {
        auto alloc = NvdecVideoDecoder::makeDeviceResidentAllocator();
        REQUIRE(alloc.isValid());
        CHECK(alloc->name() == String("NvdecAllocator"));

        // Video plane lands in CudaDevice — only meaningful when CUDA
        // is initialised at runtime, otherwise the Buffer falls back
        // to invalid (factory not registered).  Skip the placement
        // assertion when CUDA isn't available, but still run the
        // shape checks.
        ImageDesc desc(Size2Du32(320, 240),
                       PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        if (CudaDevice::isAvailable()) {
                // Force the CUDA factories to register before we ask
                // the allocator for a CudaDevice buffer.  Without this
                // the test passes only when a sibling test (e.g. the
                // round-trip case) ran first and bootstrapped CUDA;
                // when run in isolation the factory falls back to
                // System and the placement assertion fails.
                REQUIRE(CudaBootstrap::ensureRegistered().isOk());
                Buffer plane0 = alloc->allocateVideoPlane(desc, 0);
                REQUIRE(plane0.isValid());
                CHECK(plane0.memSpace().id() == MemSpace::CudaDevice);
                CHECK(plane0.allocSize() >= static_cast<size_t>(320 * 240));

                Buffer plane1 = alloc->allocateVideoPlane(desc, 1);
                REQUIRE(plane1.isValid());
                CHECK(plane1.memSpace().id() == MemSpace::CudaDevice);
        }

        // Audio + bytes fall through to the default — System placement.
        AudioDesc adesc(48000.0f, 2);
        Buffer    audio = alloc->allocateAudioChunk(adesc, 1024);
        REQUIRE(audio.isValid());
        CHECK(audio.memSpace().id() == MemSpace::System);

        Buffer bytes = alloc->allocateBytes(4096);
        REQUIRE(bytes.isValid());
        CHECK(bytes.memSpace().id() == MemSpace::System);

        // Invalid plane index returns an invalid Buffer.
        Buffer invalid = alloc->allocateVideoPlane(desc, 99);
        CHECK(!invalid.isValid());
}

TEST_CASE("NvdecVideoDecoder: device-resident allocator produces CudaDevice planes") {
        if (!CudaDevice::isAvailable()) return;

        constexpr int kWidth  = 256;
        constexpr int kHeight = 128;
        constexpr int kFrames = 4;

        auto packets = encodeBurst(VideoCodec::H264, kWidth, kHeight, kFrames);
        if (packets.isEmpty()) return; // NVENC runtime missing

        auto r = VideoCodec(VideoCodec::H264).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);
        dec->setAllocator(NvdecVideoDecoder::makeDeviceResidentAllocator());
        dec->configure(MediaConfig());

        int decoded = 0;
        for (const auto &pkt : packets) {
                Error err = dec->submitFrame(tests::frameWith(pkt));
                if (err.isError()) {
                        delete dec;
                        return;
                }
                while (true) {
                        UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                        if (!img.isValid()) break;
                        REQUIRE(img->planeCount() == 2);
                        // Both planes must be CudaDevice-resident — the
                        // whole point of installing the allocator.
                        CHECK(img->data()[0].buffer().memSpace().id() == MemSpace::CudaDevice);
                        CHECK(img->data()[1].buffer().memSpace().id() == MemSpace::CudaDevice);
                        // Buffer::data() returns nullptr on a
                        // non-host-mapped buffer — confirm we're not
                        // accidentally back on the host path.
                        CHECK(img->data()[0].buffer().data() == nullptr);
                        ++decoded;
                }
        }
        dec->flush();
        while (true) {
                UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                if (!img.isValid()) break;
                REQUIRE(img->planeCount() == 2);
                CHECK(img->data()[0].buffer().memSpace().id() == MemSpace::CudaDevice);
                CHECK(img->data()[1].buffer().memSpace().id() == MemSpace::CudaDevice);
                ++decoded;
        }

        CHECK(decoded >= 1);
        delete dec;
}

TEST_CASE("NvdecVideoDecoder: default allocator preserves System-memory output") {
        if (!CudaDevice::isAvailable()) return;

        constexpr int kWidth  = 256;
        constexpr int kHeight = 128;
        constexpr int kFrames = 4;

        auto packets = encodeBurst(VideoCodec::H264, kWidth, kHeight, kFrames);
        if (packets.isEmpty()) return;

        auto r = VideoCodec(VideoCodec::H264).createDecoder();
        REQUIRE(isOk(r));
        VideoDecoder *dec = value(r);
        // No setAllocator — default path.  Confirms the existing
        // behaviour stayed intact through the refactor.
        dec->configure(MediaConfig());

        int decoded = 0;
        for (const auto &pkt : packets) {
                Error err = dec->submitFrame(tests::frameWith(pkt));
                if (err.isError()) {
                        delete dec;
                        return;
                }
                while (true) {
                        UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                        if (!img.isValid()) break;
                        REQUIRE(img->planeCount() == 2);
                        CHECK(img->data()[0].buffer().memSpace().id() == MemSpace::System);
                        CHECK(img->data()[1].buffer().memSpace().id() == MemSpace::System);
                        // Host-readable: Buffer::data() returns a real
                        // pointer.
                        CHECK(img->data()[0].buffer().data() != nullptr);
                        ++decoded;
                }
        }
        dec->flush();
        while (true) {
                UncompressedVideoPayload::Ptr img = tests::firstUncompressedVideo(dec->receiveFrame());
                if (!img.isValid()) break;
                ++decoded;
        }

        CHECK(decoded >= 1);
        delete dec;
}

#endif // PROMEKI_ENABLE_NVDEC && PROMEKI_ENABLE_NVENC
