/**
 * @file      tests/codectesthelpers.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Tiny header-only helpers for tests that need to round-trip a
 * compressed Image through the VideoDecoder contract.  The pre-task-36
 * convention used Image::convert() for compressed → uncompressed
 * dispatch; that path is gone (Image::convert is strictly CSC now), so
 * each test that wants to inspect decoded pixels has to spin up a
 * VideoDecoder session.  These helpers keep the boilerplate to one
 * line at each callsite.
 */

#pragma once

#include <doctest/doctest.h>
#include <promeki/codec.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediapacket.h>
#include <promeki/pixeldesc.h>
#include <promeki/videocodec.h>

namespace promeki { namespace tests {

/**
 * @brief One-shot decode of a compressed @ref Image to @p target via a
 *        @ref VideoDecoder session.
 *
 * Looks the right decoder up through the typed @ref VideoCodec
 * registry (PixelDesc::videoCodec().createDecoder()), creates a
 * single-frame session, configures it with the requested output
 * format and the source image's geometry (the passthrough decoder
 * needs that explicitly; real codecs read it from the bitstream
 * but ignore the hint), wraps the source's compressed bytes as a
 * @ref MediaPacket, submits, pulls one frame, and returns it.
 *
 * Returns an invalid Image on any failure path so REQUIRE() can
 * catch it at the call site.
 */
inline ::promeki::Image decodeCompressedToImage(const ::promeki::Image &src,
                                                const ::promeki::PixelDesc &target) {
        using namespace ::promeki;
        if(!src.isValid() || !src.isCompressed()) return Image();
        VideoCodec vc = src.pixelDesc().videoCodec();
        if(!vc.isValid() || !vc.canDecode()) return Image();
        VideoDecoder *dec = vc.createDecoder();
        if(dec == nullptr) return Image();

        // Some decoders only emit a small set of native uncompressed
        // formats (e.g. SVT JPEG XS hands back planar YUV at matching
        // bit depth).  If the caller's target isn't in
        // supportedOutputs() we leave OutputPixelDesc unset so the
        // codec picks its own native default for the bitstream
        // variant on hand, then finish the hop to the caller's actual
        // target via Image::convert (CSC) below.
        const List<int> supported = dec->supportedOutputs();
        bool targetOk = supported.isEmpty();
        for(int s : supported) {
                if(s == static_cast<int>(target.id())) { targetOk = true; break; }
        }

        MediaConfig cfg;
        if(targetOk) cfg.set(MediaConfig::OutputPixelDesc, target);
        cfg.set(MediaConfig::VideoSize, Size2Du32(src.width(), src.height()));
        dec->configure(cfg);
        MediaPacket pkt(src.plane(0), src.pixelDesc());
        Error err = dec->submitPacket(pkt);
        if(err.isError()) { delete dec; return Image(); }
        Image out = dec->receiveFrame();
        delete dec;
        if(!out.isValid()) return Image();
        if(out.pixelDesc() == target) return out;
        // CSC hop to the caller's actual target.
        return out.convert(target, out.metadata());
}

/**
 * @brief One-shot encode of an uncompressed @ref Image to @p target via a
 *        @ref VideoEncoder session.
 *
 * Mirrors @ref decodeCompressedToImage on the encode side: looks the
 * codec up via the typed @ref VideoCodec registry, creates a single-
 * frame session, configures it with the requested target compressed
 * PixelDesc plus the caller's @ref MediaConfig, runs one
 * submitFrame / receivePacket, and returns the encoded image.
 */
inline ::promeki::Image encodeImageToCompressed(const ::promeki::Image &src,
                                                const ::promeki::PixelDesc &target,
                                                const ::promeki::MediaConfig &cfg = {}) {
        using namespace ::promeki;
        if(!src.isValid() || src.isCompressed()) return Image();
        if(!target.isValid() || !target.isCompressed()) return Image();
        VideoCodec vc = target.videoCodec();
        if(!vc.isValid() || !vc.canEncode()) return Image();
        VideoEncoder *enc = vc.createEncoder();
        if(enc == nullptr) return Image();
        MediaConfig sessionCfg = cfg;
        sessionCfg.set(MediaConfig::OutputPixelDesc, target);
        enc->configure(sessionCfg);

        // Prep CSC: if the source PixelDesc isn't in the target's
        // encodeSources list, run a CSC into the first listed source
        // so the encoder sees a format it knows how to ingest.
        Image input = src;
        const auto &sources = target.encodeSources();
        bool sourceOk = sources.isEmpty();
        for(PixelDesc::ID s : sources) {
                if(src.pixelDesc().id() == s) { sourceOk = true; break; }
        }
        if(!sourceOk && !sources.isEmpty()) {
                input = src.convert(PixelDesc(sources[0]),
                                    src.metadata(), cfg);
                if(!input.isValid()) { delete enc; return Image(); }
        }

        if(Error e = enc->submitFrame(input); e.isError()) {
                delete enc;
                return Image();
        }
        MediaPacket::Ptr pkt = enc->receivePacket();
        delete enc;
        if(!pkt) return Image();
        // Wrap the packet bytes as a compressed Image so the existing
        // tests can inspect compressedSize() and round-trip back.
        return Image::fromBuffer(pkt->view().buffer(),
                                 src.width(), src.height(),
                                 pkt->pixelDesc(), src.metadata());
}

} } // namespace promeki::tests
