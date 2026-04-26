/**
 * @file      tests/codectesthelpers.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Tiny header-only helpers for tests that need to round-trip a
 * compressed payload through the VideoDecoder/VideoEncoder contract.
 */

#pragma once

#include <doctest/doctest.h>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>

namespace promeki {
        namespace tests {

                /**
 * @brief One-shot decode of a compressed payload to @p target.
 */
                inline ::promeki::UncompressedVideoPayload::Ptr
                decodeCompressedPayload(const ::promeki::CompressedVideoPayload::Ptr &src,
                                        const ::promeki::PixelFormat                 &target) {
                        using namespace ::promeki;
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
                        Error         err = dec->submitPayload(src);
                        if (err.isError()) {
                                delete dec;
                                return UncompressedVideoPayload::Ptr();
                        }
                        UncompressedVideoPayload::Ptr out = dec->receiveVideoPayload();
                        delete dec;
                        if (!out.isValid() || !out->isValid()) return UncompressedVideoPayload::Ptr();
                        if (out->desc().pixelFormat() == target) return out;
                        return out->convert(target, out->desc().metadata());
                }

                /**
 * @brief One-shot encode of an uncompressed payload to @p target.
 */
                inline ::promeki::CompressedVideoPayload::Ptr
                encodePayloadToCompressed(const ::promeki::UncompressedVideoPayload::Ptr &src,
                                          const ::promeki::PixelFormat                   &target,
                                          const ::promeki::MediaConfig                   &cfg = {}) {
                        using namespace ::promeki;
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

                        if (Error e = enc->submitPayload(input); e.isError()) {
                                delete enc;
                                return CompressedVideoPayload::Ptr();
                        }
                        CompressedVideoPayload::Ptr pkt = enc->receiveCompressedPayload();
                        delete enc;
                        return pkt;
                }

                /**
 * @brief Builds a gradient RGB8 payload of the given size.
 */
                inline ::promeki::UncompressedVideoPayload::Ptr makeGradientRGB8Payload(size_t w, size_t h) {
                        using namespace ::promeki;
                        auto p = UncompressedVideoPayload::allocate(
                                ImageDesc(w, h, PixelFormat(PixelFormat::RGB8_sRGB)));
                        if (!p.isValid()) return UncompressedVideoPayload::Ptr();
                        uint8_t     *data = p.modify()->data()[0].data();
                        const auto  &ml = p->desc().pixelFormat().memLayout();
                        const size_t stride = ml.lineStride(0, w);
                        for (size_t y = 0; y < h; ++y) {
                                uint8_t *row = data + y * stride;
                                for (size_t x = 0; x < w; ++x) {
                                        row[3 * x + 0] = static_cast<uint8_t>(x * 255 / (w - 1));
                                        row[3 * x + 1] = static_cast<uint8_t>(y * 255 / (h - 1));
                                        row[3 * x + 2] = 128;
                                }
                        }
                        return p;
                }

                /**
 * @brief Mean absolute difference per byte between two RGB8 payloads.
 */
                inline double rgb8MeanAbsDiffPayload(const ::promeki::UncompressedVideoPayload &a,
                                                     const ::promeki::UncompressedVideoPayload &b) {
                        using namespace ::promeki;
                        REQUIRE(a.isValid());
                        REQUIRE(b.isValid());
                        REQUIRE(a.desc().width() == b.desc().width());
                        REQUIRE(a.desc().height() == b.desc().height());
                        const size_t   w = a.desc().width();
                        const size_t   h = a.desc().height();
                        const uint8_t *pa = static_cast<const uint8_t *>(a.plane(0).data());
                        const uint8_t *pb = static_cast<const uint8_t *>(b.plane(0).data());
                        const size_t   sa = a.desc().pixelFormat().memLayout().lineStride(0, w);
                        const size_t   sb = b.desc().pixelFormat().memLayout().lineStride(0, w);
                        double         sum = 0.0;
                        for (size_t y = 0; y < h; ++y) {
                                const uint8_t *ra = pa + y * sa;
                                const uint8_t *rb = pb + y * sb;
                                for (size_t x = 0; x < w * 3; ++x) {
                                        sum += std::abs(static_cast<int>(ra[x]) - static_cast<int>(rb[x]));
                                }
                        }
                        return sum / static_cast<double>(w * h * 3);
                }

        }
} // namespace promeki::tests
