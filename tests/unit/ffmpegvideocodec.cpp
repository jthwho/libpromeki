/**
 * @file      tests/ffmpegvideocodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Tests for the generic FFmpeg VideoEncoder / VideoDecoder backend.
 *
 *   - Registration: the @c "FFmpeg" backend is wired as a decoder for
 *     H.264 / HEVC / AV1 / ProRes and as an encoder for the six ProRes
 *     variants + Motion-JPEG, all at the fallback weight.
 *   - ProRes: a smooth-gradient frame is encoded and decoded entirely
 *     within FFmpeg and compared luma-plane against the original (ProRes
 *     is high-quality lossy, so a generous tolerance absorbs the codec).
 *   - MJPEG: the encoder emits a well-formed JFIF (SOI marker) packet.
 *   - H.264 decode fallback (when x264 is built): a stream encoded by our
 *     x264 backend decodes through FFmpeg's software H.264 decoder — the
 *     "fall back to FFmpeg" path on a box without NVDEC.
 */

#include <doctest/doctest.h>

#include <promeki/config.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <promeki/buffer.h>
#include <promeki/ffmpegsupport.h>
#include <promeki/logger.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/pixelformat.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/videocodec.h>
#include <promeki/videodecoder.h>
#include <promeki/videoencoder.h>
#include "codectesthelpers.h"

using namespace promeki;

namespace {

        // Bytes per luma sample for a planar YUV format (8-bit → 1, 10/12-bit → 2).
        size_t bytesPerSample(const PixelFormat &pf, size_t width) {
                const size_t stride = pf.memLayout().lineStride(0, width);
                return (width > 0 && stride / width >= 2) ? 2 : 1;
        }

        // Fills every plane of @p p with a smooth diagonal gradient (luma) /
        // mid-level ramp (chroma).  Smooth content round-trips cleanly through
        // lossy intra codecs, keeping the comparison tolerance meaningful.
        void fillGradient(UncompressedVideoPayload *p) {
                const PixelFormat    &pf = p->desc().pixelFormat();
                const PixelMemLayout &ml = pf.memLayout();
                const size_t          w = p->desc().size().width();
                const size_t          planes = pf.planeCount();
                const size_t          bps = bytesPerSample(pf, w);
                const int             maxV = (bps == 2) ? 1023 : 255; // 10-bit in 16-bit storage / 8-bit
                for (size_t c = 0; c < planes; ++c) {
                        const size_t stride = ml.lineStride(c, w);
                        if (stride == 0) continue;
                        const size_t planeBytes = pf.planeSize(c, p->desc());
                        const size_t rows = planeBytes / stride;
                        const size_t cols = stride / bps;
                        uint8_t     *base = p->data()[c].data();
                        for (size_t r = 0; r < rows; ++r) {
                                for (size_t x = 0; x < cols; ++x) {
                                        int v;
                                        if (c == 0) {
                                                v = static_cast<int>((x + r) * maxV / (cols + rows));
                                        } else {
                                                // Chroma: gentle ramp around mid-grey.
                                                v = maxV / 2 + static_cast<int>(x * (maxV / 8) / (cols + 1));
                                        }
                                        v = std::clamp(v, 0, maxV);
                                        if (bps == 2) {
                                                uint16_t s = static_cast<uint16_t>(v);
                                                std::memcpy(base + r * stride + x * 2, &s, 2);
                                        } else {
                                                base[r * stride + x] = static_cast<uint8_t>(v);
                                        }
                                }
                        }
                }
        }

        UncompressedVideoPayload::Ptr makeGradient(PixelFormat::ID fmt, uint32_t w, uint32_t h) {
                ImageDesc desc(Size2Du32(w, h), PixelFormat(fmt));
                auto      p = UncompressedVideoPayload::allocate(desc);
                if (p.isValid()) fillGradient(p.modify());
                return p;
        }

        // Mean absolute difference over the luma plane, expressed in 10-bit
        // sample units regardless of either payload's native bit depth.
        //
        // Each side is normalised by its own full-scale value and rescaled to
        // the 10-bit range, so an 8-bit, 10-bit, or 12-bit payload can be
        // compared against any other — e.g. a 10-bit gradient encoded as
        // ProRes 4444 decodes back as 12-bit (the 4444 / XQ profiles carry a
        // 12-bit surface), and the round-trip difference is still measured on a
        // common scale.  Reads each payload at its OWN per-plane stride
        // (a.desc() / b.desc()) and iterates only the active width × height —
        // so it stays correct whether a payload is tightly packed or carries
        // per-plane line padding (e.g. the zero-copy FFmpeg decode path, whose
        // linesize may exceed the tight stride).
        double lumaMad(const UncompressedVideoPayload &a, const UncompressedVideoPayload &b) {
                const PixelFormat &pfa = a.desc().pixelFormat();
                const PixelFormat &pfb = b.desc().pixelFormat();
                const size_t       w = a.desc().size().width();
                const size_t       h = a.desc().size().height();
                const size_t       strideA = pfa.lineStride(0, a.desc());
                const size_t       strideB = pfb.lineStride(0, b.desc());
                const size_t       bpsA = bytesPerSample(pfa, w);
                const size_t       bpsB = bytesPerSample(pfb, w);
                const double       maxA = static_cast<double>((1 << pfa.memLayout().compDesc(0).bits) - 1);
                const double       maxB = static_cast<double>((1 << pfb.memLayout().compDesc(0).bits) - 1);
                const uint8_t     *pa = a.plane(0).data();
                const uint8_t     *pb = b.plane(0).data();
                auto               readAt = [](const uint8_t *base, size_t off, size_t bps) -> int {
                        if (bps == 2) {
                                uint16_t s;
                                std::memcpy(&s, base + off, 2);
                                return s;
                        }
                        return base[off];
                };
                double sum = 0.0;
                size_t n = 0;
                for (size_t r = 0; r < h; ++r) {
                        for (size_t x = 0; x < w; ++x) {
                                const double va = readAt(pa, r * strideA + x * bpsA, bpsA) / maxA;
                                const double vb = readAt(pb, r * strideB + x * bpsB, bpsB) / maxB;
                                sum += std::abs(va - vb) * 1023.0;
                                ++n;
                        }
                }
                return n ? sum / static_cast<double>(n) : 0.0;
        }

        // Encode one frame through @p enc and return the first compressed
        // payload it produces (driving submit + flush + drain).
        CompressedVideoPayload::Ptr encodeOne(VideoEncoder *enc, const UncompressedVideoPayload::Ptr &in) {
                if (enc->submitFrame(tests::frameWith(in)) != Error::Ok) return CompressedVideoPayload::Ptr();
                CompressedVideoPayload::Ptr out;
                auto drain = [&]() {
                        while (true) {
                                Frame f = enc->receiveFrame();
                                if (!f.isValid()) break;
                                auto pkt = tests::firstCompressedVideo(f);
                                if (pkt.isValid() && pkt->isValid() && !pkt->isEndOfStream() && !out.isValid())
                                        out = pkt;
                        }
                };
                drain();
                enc->flush();
                drain();
                return out;
        }

} // namespace

TEST_CASE("FFmpegVideo: backend registered for decode + encode") {
        auto bk = VideoCodec::lookupBackend("FFmpeg");
        REQUIRE(!error(bk).isError());
        const VideoCodec::Backend ffmpeg = value(bk);

        SUBCASE("decoders") {
                const VideoCodec::ID ids[] = {VideoCodec::H264, VideoCodec::HEVC, VideoCodec::AV1,
                                              VideoCodec::ProRes_422};
                for (VideoCodec::ID id : ids) {
                        VideoCodec codec(id);
                        CHECK(codec.availableDecoderBackends().contains(ffmpeg));
                }
                // VP9 is intentionally NOT registered: the library has no
                // PixelFormat::VP9, so no container backend can surface a VP9
                // access unit to feed such a decoder.
                CHECK_FALSE(VideoCodec(VideoCodec::VP9).availableDecoderBackends().contains(ffmpeg));
        }
        SUBCASE("encoders") {
                const VideoCodec::ID ids[] = {VideoCodec::ProRes_422_Proxy, VideoCodec::ProRes_422,
                                              VideoCodec::ProRes_4444, VideoCodec::ProRes_4444_XQ,
                                              VideoCodec::JPEG};
                for (VideoCodec::ID id : ids) {
                        VideoCodec codec(id);
                        CHECK(codec.availableEncoderBackends().contains(ffmpeg));
                }
        }
        SUBCASE("no H.264 / HEVC encoder (LGPL build)") {
                // FFmpeg must not claim H.264/HEVC encode — those need GPL libs.
                CHECK_FALSE(VideoCodec(VideoCodec::H264).availableEncoderBackends().contains(ffmpeg));
                CHECK_FALSE(VideoCodec(VideoCodec::HEVC).availableEncoderBackends().contains(ffmpeg));
        }
}

TEST_CASE("FFmpegVideo: FFmpeg ranks below x264 for H.264 (fallback weight)") {
        // With no pin, H.264 must not resolve to FFmpeg when a higher-weight
        // backend (x264 / NVENC) is present.  FFmpeg has no H.264 encoder at
        // all here, so this also just confirms FFmpeg isn't the auto pick.
        auto enc = VideoCodec(VideoCodec::H264).createEncoder();
        if (isOk(enc)) {
                VideoEncoder *e = value(enc);
                CHECK(e->codec().backend().name() != String("FFmpeg"));
                delete e;
        }
}

TEST_CASE("FFmpegVideo: ProRes 422 encode/decode round-trips a gradient") {
        VideoCodec codec(VideoCodec::ProRes_422);
        REQUIRE(codec.canEncode());
        REQUIRE(codec.canDecode());

        auto in = makeGradient(PixelFormat::YUV10_422_Planar_LE_Rec709, 64, 64);
        REQUIRE(in.isValid());

        auto encRes = codec.createEncoder();
        REQUIRE(isOk(encRes));
        VideoEncoder *enc = value(encRes);
        auto          pkt = encodeOne(enc, in);
        delete enc;
        REQUIRE(pkt.isValid());
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::ProRes_422);
        CHECK(pkt->isKeyframe());

        auto decRes = codec.createDecoder();
        REQUIRE(isOk(decRes));
        VideoDecoder *dec = value(decRes);
        REQUIRE(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
        dec->flush();
        Frame out = dec->receiveFrame();
        auto  img = tests::firstUncompressedVideo(out);
        delete dec;

        REQUIRE(img.isValid());
        CHECK(img->desc().size().width() == 64);
        CHECK(img->desc().size().height() == 64);
        // The 422 family carries a 10-bit surface; codec_tag 'apcn' decodes it
        // back at its native 10-bit depth.
        CHECK(img->desc().pixelFormat().id() == PixelFormat::YUV10_422_Planar_LE_Rec709);
        // ProRes 422 is visually lossless on smooth content.
        CHECK(lumaMad(*in, *img) < 24.0);
}

TEST_CASE("FFmpegVideo: CodecThreads cap is honored (round-trips with threads=1)") {
        // Pin the per-session thread count to 1 (single-threaded) via the
        // config knob and confirm the encode→decode round-trip still holds —
        // exercises the MediaConfig::CodecThreads → thread_count plumbing.
        VideoCodec  codec(VideoCodec::ProRes_422);
        MediaConfig cfg;
        cfg.set(MediaConfig::CodecThreads, int32_t(1));

        auto in = makeGradient(PixelFormat::YUV10_422_Planar_LE_Rec709, 64, 64);
        REQUIRE(in.isValid());

        auto encRes = codec.createEncoder(&cfg);
        REQUIRE(isOk(encRes));
        VideoEncoder *enc = value(encRes);
        auto          pkt = encodeOne(enc, in);
        delete enc;
        REQUIRE(pkt.isValid());

        auto decRes = codec.createDecoder(&cfg);
        REQUIRE(isOk(decRes));
        VideoDecoder *dec = value(decRes);
        REQUIRE(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
        dec->flush();
        auto img = tests::firstUncompressedVideo(dec->receiveFrame());
        delete dec;
        REQUIRE(img.isValid());
        CHECK(img->desc().pixelFormat().id() == PixelFormat::YUV10_422_Planar_LE_Rec709);
        CHECK(lumaMad(*in, *img) < 24.0);
}

TEST_CASE("FFmpegVideo: ProRes 4444 encode/decode round-trips a gradient") {
        VideoCodec codec(VideoCodec::ProRes_4444);
        REQUIRE(codec.canEncode());
        REQUIRE(codec.canDecode());

        auto in = makeGradient(PixelFormat::YUV10_444_Planar_LE_Rec709, 64, 64);
        REQUIRE(in.isValid());

        auto encRes = codec.createEncoder();
        REQUIRE(isOk(encRes));
        VideoEncoder *enc = value(encRes);
        auto          pkt = encodeOne(enc, in);
        delete enc;
        REQUIRE(pkt.isValid());
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::ProRes_4444);

        auto decRes = codec.createDecoder();
        REQUIRE(isOk(decRes));
        VideoDecoder *dec = value(decRes);
        REQUIRE(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
        dec->flush();
        auto img = tests::firstUncompressedVideo(dec->receiveFrame());
        delete dec;

        REQUIRE(img.isValid());
        // ProRes 4444 carries a 12-bit 4:4:4 surface — codec_tag 'ap4h' makes
        // the decoder emit the bitstream's native depth (the 10-bit input is
        // widened to 12 bits), so the round-trip lands on the 12-bit planar
        // format.  lumaMad normalises both depths to a 10-bit scale.
        CHECK(img->desc().pixelFormat().id() == PixelFormat::YUV12_444_Planar_LE_Rec709);
        CHECK(lumaMad(*in, *img) < 16.0); // 4444 is near-lossless
}

TEST_CASE("FFmpegVideo: ProRes 4444 XQ encode/decode round-trips a gradient") {
        VideoCodec codec(VideoCodec::ProRes_4444_XQ);
        REQUIRE(codec.canEncode());
        REQUIRE(codec.canDecode());

        auto in = makeGradient(PixelFormat::YUV10_444_Planar_LE_Rec709, 64, 64);
        REQUIRE(in.isValid());

        auto encRes = codec.createEncoder();
        REQUIRE(isOk(encRes));
        VideoEncoder *enc = value(encRes);
        auto          pkt = encodeOne(enc, in);
        delete enc;
        REQUIRE(pkt.isValid());
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::ProRes_4444_XQ);

        auto decRes = codec.createDecoder();
        REQUIRE(isOk(decRes));
        VideoDecoder *dec = value(decRes);
        REQUIRE(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
        dec->flush();
        auto img = tests::firstUncompressedVideo(dec->receiveFrame());
        delete dec;

        REQUIRE(img.isValid());
        // 4444 XQ is intrinsically 12-bit; codec_tag 'ap4x' makes the decoder
        // emit its native 12-bit 4:4:4 surface (this is exactly the path a
        // genuine 12-bit asset from another NLE or a hardware ProRes codec
        // lands on, which the codec_tag-unset path used to clamp to 10-bit).
        CHECK(img->desc().pixelFormat().id() == PixelFormat::YUV12_444_Planar_LE_Rec709);
        CHECK(lumaMad(*in, *img) < 16.0);
}

TEST_CASE("FFmpegVideo: MJPEG encoder emits a well-formed JFIF") {
        VideoCodec codec(VideoCodec::JPEG);
        // JPEG has a higher-weight Turbo backend; pin FFmpeg explicitly.
        auto pinned = VideoCodec::fromString("JPEG:FFmpeg");
        REQUIRE(isOk(pinned));
        auto encRes = value(pinned).createEncoder();
        REQUIRE(isOk(encRes));
        VideoEncoder *enc = value(encRes);

        auto in = makeGradient(PixelFormat::YUV8_420_Planar_Rec709, 64, 48);
        REQUIRE(in.isValid());
        auto pkt = encodeOne(enc, in);
        delete enc;

        REQUIRE(pkt.isValid());
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_YUV8_420_Rec709);
        auto         view = pkt->plane(0);
        const uint8_t *b = view.data();
        REQUIRE(view.size() > 4);
        // JFIF Start-Of-Image marker.
        CHECK(b[0] == 0xFF);
        CHECK(b[1] == 0xD8);
}

TEST_CASE("FFmpegVideo: ffmpegErrorString decodes an AVERROR code") {
        // Any negative code yields a non-empty message (FFmpeg falls back to
        // "Error number N occurred" for codes it doesn't recognise).
        CHECK_FALSE(ffmpegErrorString(-1).isEmpty());
        // Idempotent install — safe to call again.
        ffmpegInstallLogBridge();
}

TEST_CASE("FFmpegVideo: libav log routes through the promeki Logger") {
        // The ProRes decoder emits a WARNING ("Unknown prores profile 0",
        // because we leave codec_tag unset) — it must arrive via the av_log →
        // Logger bridge, tagged with the "ffmpeg" source.
        std::atomic<bool> sawFfmpeg{false};
        Logger::ListenerHandle h = Logger::defaultLogger().installListener(
                [&](const Logger::LogEntry &e, const String &) {
                        if (e.file != nullptr && std::strcmp(e.file, "ffmpeg") == 0) sawFfmpeg.store(true);
                });

        VideoCodec codec(VideoCodec::ProRes_422);
        auto       in = makeGradient(PixelFormat::YUV10_422_Planar_LE_Rec709, 64, 64);
        REQUIRE(in.isValid());
        VideoEncoder *enc = value(codec.createEncoder());
        auto          pkt = encodeOne(enc, in);
        delete enc;
        REQUIRE(pkt.isValid());

        VideoDecoder *dec = value(codec.createDecoder());
        dec->submitFrame(tests::frameWith(pkt));
        dec->flush();
        (void)dec->receiveFrame();
        delete dec;

        Logger::defaultLogger().sync();
        Logger::defaultLogger().removeListener(h);
        CHECK(sawFfmpeg.load());
}

#if PROMEKI_ENABLE_X264
TEST_CASE("FFmpegVideo: H.264 decode fallback (x264 -> FFmpeg)") {
        // Encode with our x264 software encoder, decode through FFmpeg's
        // software H.264 decoder — the no-GPU "fall back to FFmpeg" path.
        auto encPin = VideoCodec::fromString("H264:x264");
        REQUIRE(isOk(encPin));
        auto encRes = value(encPin).createEncoder();
        REQUIRE(isOk(encRes));
        VideoEncoder *enc = value(encRes);
        MediaConfig   ecfg;
        ecfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::RationalType(25, 1)));
        ecfg.set(MediaConfig::GopLength, int32_t(1)); // all-intra → deterministic, no reorder
        // Emit SPS/PPS in-band before each IDR so FFmpeg's raw-AU decoder has
        // parameter sets (x264 otherwise carries them only out-of-band).
        ecfg.set(MediaConfig::VideoRepeatHeaders, true);
        enc->configure(ecfg);

        auto decPin = VideoCodec::fromString("H264:FFmpeg");
        REQUIRE(isOk(decPin));
        auto decRes = value(decPin).createDecoder();
        REQUIRE(isOk(decRes));
        VideoDecoder *dec = value(decRes);
        CHECK(dec->codec().backend().name() == String("FFmpeg"));

        auto in = makeGradient(PixelFormat::YUV8_420_Planar_Rec709, 64, 64);
        REQUIRE(in.isValid());

        int decoded = 0;
        UncompressedVideoPayload::Ptr lastImg;
        auto drainDec = [&]() {
                while (true) {
                        Frame f = dec->receiveFrame();
                        if (!f.isValid()) break;
                        auto img = tests::firstUncompressedVideo(f);
                        if (img.isValid()) { ++decoded; lastImg = img; }
                }
        };
        for (int i = 0; i < 4; ++i) {
                REQUIRE(enc->submitFrame(tests::frameWith(in)) == Error::Ok);
                while (true) {
                        Frame f = enc->receiveFrame();
                        if (!f.isValid()) break;
                        auto pkt = tests::firstCompressedVideo(f);
                        if (pkt.isValid() && pkt->isValid() && !pkt->isEndOfStream()) {
                                dec->submitFrame(tests::frameWith(pkt));
                                drainDec();
                        }
                }
        }
        enc->flush();
        while (true) {
                Frame f = enc->receiveFrame();
                if (!f.isValid()) break;
                auto pkt = tests::firstCompressedVideo(f);
                if (pkt.isValid() && pkt->isValid() && !pkt->isEndOfStream()) {
                        dec->submitFrame(tests::frameWith(pkt));
                        drainDec();
                }
        }
        dec->flush();
        drainDec();
        delete enc;
        delete dec;

        CHECK(decoded >= 1);
        REQUIRE(lastImg.isValid());
        CHECK(lastImg->desc().size().width() == 64);
        CHECK(lastImg->desc().size().height() == 64);
}
#endif // PROMEKI_ENABLE_X264
