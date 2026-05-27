/**
 * @file      tests/codectesthelpers.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Out-of-line bodies for the codec round-trip helpers declared in
 * codectesthelpers.h.  Lives in its own TU so the heavy template
 * instantiations (sharedPointerCast, Frame iteration,
 * VideoEncoder/VideoDecoder lifecycle, UncompressedVideoPayload
 * pixel-plane walks) are parsed and codegen'd exactly once across the
 * unit-test build instead of in every TU that pulls the header in.
 */

#include "codectesthelpers.h"

#include <cstdlib>
#include <doctest/doctest.h>
#include <promeki/error.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>
#include <promeki/videocodec.h>
#include <promeki/videodecoder.h>
#include <promeki/videoencoder.h>

namespace promeki {
        namespace tests {

                Frame frameWith(MediaPayload::Ptr payload) {
                        Frame f;
                        if (payload.isValid()) f.addPayload(std::move(payload));
                        return f;
                }

                CompressedVideoPayload::Ptr firstCompressedVideo(const Frame &f) {
                        if (!f.isValid()) return CompressedVideoPayload::Ptr();
                        for (const VideoPayload::Ptr &vp : f.videoPayloads()) {
                                if (!vp.isValid()) continue;
                                CompressedVideoPayload::Ptr cvp = sharedPointerCast<CompressedVideoPayload>(vp);
                                if (cvp.isValid()) return cvp;
                        }
                        return CompressedVideoPayload::Ptr();
                }

                UncompressedVideoPayload::Ptr firstUncompressedVideo(const Frame &f) {
                        if (!f.isValid()) return UncompressedVideoPayload::Ptr();
                        for (const VideoPayload::Ptr &vp : f.videoPayloads()) {
                                if (!vp.isValid()) continue;
                                UncompressedVideoPayload::Ptr uvp = sharedPointerCast<UncompressedVideoPayload>(vp);
                                if (uvp.isValid()) return uvp;
                        }
                        return UncompressedVideoPayload::Ptr();
                }

                CompressedAudioPayload::Ptr firstCompressedAudio(const Frame &f) {
                        if (!f.isValid()) return CompressedAudioPayload::Ptr();
                        for (const AudioPayload::Ptr &ap : f.audioPayloads()) {
                                if (!ap.isValid()) continue;
                                CompressedAudioPayload::Ptr cap = sharedPointerCast<CompressedAudioPayload>(ap);
                                if (cap.isValid()) return cap;
                        }
                        return CompressedAudioPayload::Ptr();
                }

                PcmAudioPayload::Ptr firstPcmAudio(const Frame &f) {
                        if (!f.isValid()) return PcmAudioPayload::Ptr();
                        for (const AudioPayload::Ptr &ap : f.audioPayloads()) {
                                if (!ap.isValid()) continue;
                                PcmAudioPayload::Ptr pcm = sharedPointerCast<PcmAudioPayload>(ap);
                                if (pcm.isValid()) return pcm;
                        }
                        return PcmAudioPayload::Ptr();
                }

                UncompressedVideoPayload::Ptr decodeCompressedPayload(const CompressedVideoPayload::Ptr &src,
                                                                      const PixelFormat                 &target) {
                        if (!src.isValid() || !src->isValid()) return UncompressedVideoPayload::Ptr();
                        VideoCodec vc = src->desc().pixelFormat().videoCodec();
                        if (!vc.isValid() || !vc.canDecode()) return UncompressedVideoPayload::Ptr();

                        const List<PixelFormat> supported = vc.decoderSupportedOutputs();
                        bool                    targetOk = supported.isEmpty();
                        for (const PixelFormat &s : supported) {
                                if (s == target) {
                                        targetOk = true;
                                        break;
                                }
                        }

                        MediaConfig cfg;
                        if (targetOk) cfg.set(MediaConfig::OutputPixelFormat, target);
                        cfg.set(MediaConfig::VideoSize, src->desc().size());
                        auto decResult = vc.createDecoder(&cfg);
                        if (error(decResult).isError()) return UncompressedVideoPayload::Ptr();
                        VideoDecoder *dec = value(decResult);
                        Frame         inFrame;
                        inFrame.addPayload(src);
                        Error err = dec->submitFrame(inFrame);
                        if (err.isError()) {
                                delete dec;
                                return UncompressedVideoPayload::Ptr();
                        }
                        Frame                         outFrame = dec->receiveFrame();
                        UncompressedVideoPayload::Ptr out;
                        if (outFrame.isValid()) {
                                auto vps = outFrame.videoPayloads();
                                if (!vps.isEmpty()) out = sharedPointerCast<UncompressedVideoPayload>(vps[0]);
                        }
                        delete dec;
                        if (!out.isValid() || !out->isValid()) return UncompressedVideoPayload::Ptr();
                        if (out->desc().pixelFormat() == target) return out;
                        return out->convert(target, out->desc().metadata());
                }

                CompressedVideoPayload::Ptr encodePayloadToCompressed(const UncompressedVideoPayload::Ptr &src,
                                                                      const PixelFormat                   &target,
                                                                      const MediaConfig                   &cfg) {
                        if (!src.isValid() || !src->isValid()) return CompressedVideoPayload::Ptr();
                        if (!target.isValid() || !target.isCompressed()) return CompressedVideoPayload::Ptr();
                        VideoCodec vc = target.videoCodec();
                        if (!vc.isValid() || !vc.canEncode()) return CompressedVideoPayload::Ptr();
                        MediaConfig sessionCfg = cfg;
                        sessionCfg.set(MediaConfig::OutputPixelFormat, target);
                        auto encResult = vc.createEncoder(&sessionCfg);
                        if (error(encResult).isError()) return CompressedVideoPayload::Ptr();
                        VideoEncoder *enc = value(encResult);

                        UncompressedVideoPayload::Ptr input = src;
                        const auto                   &sources = target.encodeSources();
                        bool                          sourceOk = sources.isEmpty();
                        for (PixelFormat::ID s : sources) {
                                if (src->desc().pixelFormat().id() == s) {
                                        sourceOk = true;
                                        break;
                                }
                        }
                        if (!sourceOk && !sources.isEmpty()) {
                                input = src->convert(PixelFormat(sources[0]), src->desc().metadata(), cfg);
                                if (!input.isValid()) {
                                        delete enc;
                                        return CompressedVideoPayload::Ptr();
                                }
                        }

                        Frame inFrame;
                        inFrame.addPayload(input);
                        if (Error e = enc->submitFrame(inFrame); e.isError()) {
                                delete enc;
                                return CompressedVideoPayload::Ptr();
                        }
                        Frame                       outFrame = enc->receiveFrame();
                        CompressedVideoPayload::Ptr pkt;
                        if (outFrame.isValid()) {
                                auto vps = outFrame.videoPayloads();
                                if (!vps.isEmpty()) pkt = sharedPointerCast<CompressedVideoPayload>(vps[0]);
                        }
                        delete enc;
                        return pkt;
                }

                UncompressedVideoPayload::Ptr makeGradientRGB8Payload(std::size_t w, std::size_t h) {
                        auto p = UncompressedVideoPayload::allocate(
                                ImageDesc(w, h, PixelFormat(PixelFormat::RGB8_sRGB)));
                        if (!p.isValid()) return UncompressedVideoPayload::Ptr();
                        uint8_t     *data = p.modify()->data()[0].data();
                        const auto  &ml = p->desc().pixelFormat().memLayout();
                        const std::size_t stride = ml.lineStride(0, w);
                        for (std::size_t y = 0; y < h; ++y) {
                                uint8_t *row = data + y * stride;
                                for (std::size_t x = 0; x < w; ++x) {
                                        row[3 * x + 0] = static_cast<uint8_t>(x * 255 / (w - 1));
                                        row[3 * x + 1] = static_cast<uint8_t>(y * 255 / (h - 1));
                                        row[3 * x + 2] = 128;
                                }
                        }
                        return p;
                }

                double rgb8MeanAbsDiffPayload(const UncompressedVideoPayload &a, const UncompressedVideoPayload &b) {
                        REQUIRE(a.isValid());
                        REQUIRE(b.isValid());
                        REQUIRE(a.desc().width() == b.desc().width());
                        REQUIRE(a.desc().height() == b.desc().height());
                        const std::size_t w = a.desc().width();
                        const std::size_t h = a.desc().height();
                        const uint8_t    *pa = static_cast<const uint8_t *>(a.plane(0).data());
                        const uint8_t    *pb = static_cast<const uint8_t *>(b.plane(0).data());
                        const std::size_t sa = a.desc().pixelFormat().memLayout().lineStride(0, w);
                        const std::size_t sb = b.desc().pixelFormat().memLayout().lineStride(0, w);
                        double            sum = 0.0;
                        for (std::size_t y = 0; y < h; ++y) {
                                const uint8_t *ra = pa + y * sa;
                                const uint8_t *rb = pb + y * sb;
                                for (std::size_t x = 0; x < w * 3; ++x) {
                                        sum += std::abs(static_cast<int>(ra[x]) - static_cast<int>(rb[x]));
                                }
                        }
                        return sum / static_cast<double>(w * h * 3);
                }

        }
} // namespace promeki::tests
