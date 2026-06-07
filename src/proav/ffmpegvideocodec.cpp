/**
 * @file      ffmpegvideocodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Generic FFmpeg (libavcodec) VideoEncoder / VideoDecoder backend.  One
 * backend (@c "FFmpeg") fills the gaps left by our directly-implemented
 * codec backends (x264, NVENC/NVDEC, libjpeg-turbo, JPEG XS, V4L2):
 *
 *   - @b Decode — H.264, HEVC, AV1 and ProRes, using FFmpeg's native
 *     LGPL decoders.  This is the "fall back to FFmpeg" path: on a box
 *     without an NVIDIA GPU (no NVDEC) the temporal codecs still decode
 *     in software, and AV1 / ProRes decode is available where we have no
 *     other backend at all.  (VP9 is intentionally absent: the library
 *     has no @c PixelFormat::VP9 compressed-format ID, so no container
 *     backend can surface a VP9 access unit to feed a decoder — wiring
 *     one in is a self-contained format-enum addition for the day it is
 *     needed.)
 *   - @b Encode — ProRes (all six variants, via FFmpeg's @c prores_ks
 *     encoder) and Motion-JPEG (@c mjpeg).  Our vendored FFmpeg is built
 *     LGPL-only (no @c --enable-gpl / libx264 / libx265 / libvpx /
 *     libaom), so H.264 / HEVC / AV1 @em encode is intentionally not
 *     offered here — x264 / NVENC own H.264, and the others have no
 *     LGPL-clean FFmpeg encoder to wire in.
 *
 * @par Backend weight — fallback
 *
 * Every (codec, FFmpeg) pair registers at @c BackendWeight::Vendored-20,
 * one band below x264's software encoder (@c Vendored-10) and well below
 * the hardware / vendored backends (@c Vendored).  So with no explicit
 * pin the framework prefers our own codecs and only auto-selects FFmpeg
 * for a codec nothing else services.  Callers force FFmpeg with an
 * explicit pin (@c "H264:FFmpeg") or @c MediaConfig::CodecBackend.
 *
 * @par Pixel-format mapping (no libswscale)
 *
 * The vendored FFmpeg ships only libavcodec / libavutil / libswresample
 * — there is no libswscale — so this backend never asks FFmpeg to scale
 * or convert.  It maps a curated set of planar / semi-planar YUV
 * @ref PixelFormat values 1:1 onto the matching @c AVPixelFormat and
 * copies planes directly.  Any further conversion the caller needs is
 * the pipeline planner's job via the library's own CSC — exactly the
 * contract the x264 backend follows (it advertises planar-YUV inputs and
 * lets the planner splice a CSC ahead of it).
 *
 * Like every other session backend, the codec sessions are container-
 * agnostic: a source @ref Frame carries an @ref UncompressedVideoPayload
 * in / a @ref CompressedVideoPayload out for encode (and vice-versa for
 * decode), one raw codec access unit per payload.
 */

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/colormodel.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/enums_color.h>
#include <promeki/deque.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/ffmpegsupport.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/rational.h>
#include <promeki/string.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videocodec.h>
#include <promeki/videodecoder.h>
#include <promeki/videoencoder.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -------------------------------------------------------------------
        // Decode codec-family mapping
        // -------------------------------------------------------------------

        // Maps a promeki VideoCodec ID onto the libavcodec decoder ID.
        // Returns AV_CODEC_ID_NONE for codecs this backend does not decode.
        AVCodecID avDecodeIdFor(VideoCodec::ID id) {
                switch (id) {
                        case VideoCodec::H264: return AV_CODEC_ID_H264;
                        case VideoCodec::HEVC: return AV_CODEC_ID_HEVC;
                        case VideoCodec::AV1: return AV_CODEC_ID_AV1;
                        case VideoCodec::ProRes_422_Proxy:
                        case VideoCodec::ProRes_422_LT:
                        case VideoCodec::ProRes_422:
                        case VideoCodec::ProRes_422_HQ:
                        case VideoCodec::ProRes_4444:
                        case VideoCodec::ProRes_4444_XQ: return AV_CODEC_ID_PRORES;
                        default: return AV_CODEC_ID_NONE;
                }
        }

        // -------------------------------------------------------------------
        // PixelFormat <-> AVPixelFormat mapping (1:1, no scaling/conversion)
        // -------------------------------------------------------------------

        // An H.273 codepoint carried in a bitstream / container / config is
        // "concrete" only when it names a real value: 0 (unset), 2
        // (Unspecified) and 255 (Auto) all mean "no opinion — keep whatever
        // the lower-precedence source said".
        bool isConcreteColorCode(int code) { return code != 0 && code != 2 && code != 255; }

        // The Rec.709 *base* PixelFormat for a decoded AVFrame pixel format.
        // Only the planar / semi-planar YUV layouts this backend copies
        // plane-for-plane are mapped; anything else returns Invalid and the
        // caller fails the decode with a clear message.  @ref promekiPixelFormatFor
        // then rebases this onto the frame's actual ColorModel via
        // @ref PixelFormat::withColorModel.
        PixelFormat::ID baseFormatForAvFmt(int avFmt) {
                switch (avFmt) {
                        case AV_PIX_FMT_YUV420P: return PixelFormat::YUV8_420_Planar_Rec709;
                        case AV_PIX_FMT_YUVJ420P: return PixelFormat::YUV8_420_Planar_Rec601_Full;
                        case AV_PIX_FMT_YUV422P: return PixelFormat::YUV8_422_Planar_Rec709;
                        case AV_PIX_FMT_YUVJ422P: return PixelFormat::YUV8_422_Planar_Rec709;
                        case AV_PIX_FMT_YUV444P: return PixelFormat::YUV8_444_Planar_Rec709;
                        case AV_PIX_FMT_YUVJ444P: return PixelFormat::YUV8_444_Planar_Rec709;
                        case AV_PIX_FMT_YUV420P10LE: return PixelFormat::YUV10_420_Planar_LE_Rec709;
                        case AV_PIX_FMT_YUV422P10LE: return PixelFormat::YUV10_422_Planar_LE_Rec709;
                        case AV_PIX_FMT_YUV444P10LE: return PixelFormat::YUV10_444_Planar_LE_Rec709;
                        // ProRes 4444 decodes with an alpha plane; the 3-plane
                        // promeki target drops it (we only copy planeCount planes).
                        case AV_PIX_FMT_YUVA444P10LE: return PixelFormat::YUV10_444_Planar_LE_Rec709;
                        case AV_PIX_FMT_YUV420P12LE: return PixelFormat::YUV12_420_Planar_LE_Rec709;
                        case AV_PIX_FMT_YUV422P12LE: return PixelFormat::YUV12_422_Planar_LE_Rec709;
                        case AV_PIX_FMT_YUV444P12LE: return PixelFormat::YUV12_444_Planar_LE_Rec709;
                        case AV_PIX_FMT_YUVA444P12LE: return PixelFormat::YUV12_444_Planar_LE_Rec709;
                        case AV_PIX_FMT_NV12: return PixelFormat::YUV8_420_SemiPlanar_Rec709;
                        case AV_PIX_FMT_P010LE: return PixelFormat::YUV10_420_SemiPlanar_LE_Rec709;
                        default: return PixelFormat::Invalid;
                }
        }

        // Resolves the promeki output @ref PixelFormat for a decoded AVFrame.
        // The colour space is *always* carried by the singular PixelFormat:
        // the Rec.709 base for the layout is rebased onto the frame's resolved
        // @ref ColorModel via @ref PixelFormat::withColorModel, which returns a
        // well-known catalog variant when one exists and registers a runtime
        // variant otherwise.  No colour information is carried in metadata.
        PixelFormat::ID promekiPixelFormatFor(int avFmt, ColorModel::ID cm) {
                const PixelFormat::ID base = baseFormatForAvFmt(avFmt);
                if (base == PixelFormat::Invalid || cm == ColorModel::Invalid) return base;
                return PixelFormat::withColorModel(PixelFormat(base), cm).id();
        }

        // Maps a promeki uncompressed-input @ref PixelFormat onto the
        // AVPixelFormat fed to an FFmpeg encoder.  Mirrors the encoder
        // supportedInputs lists exactly; AV_PIX_FMT_NONE means "not an input
        // this backend feeds FFmpeg".
        int avPixelFormatForInput(PixelFormat::ID id) {
                switch (id) {
                        case PixelFormat::YUV10_422_Planar_LE_Rec709: return AV_PIX_FMT_YUV422P10LE;
                        case PixelFormat::YUV10_444_Planar_LE_Rec709: return AV_PIX_FMT_YUV444P10LE;
                        case PixelFormat::YUV8_420_Planar_Rec709: return AV_PIX_FMT_YUV420P;
                        case PixelFormat::YUV8_422_Planar_Rec709: return AV_PIX_FMT_YUV422P;
                        default: return AV_PIX_FMT_NONE;
                }
        }

        // -------------------------------------------------------------------
        // Encode codec-family mapping (ProRes + MJPEG only — LGPL build)
        // -------------------------------------------------------------------

        // The prores_ks "profile" private-option value for each ProRes
        // variant.  Returns -1 for non-ProRes codecs.
        int proresProfileFor(VideoCodec::ID id) {
                switch (id) {
                        case VideoCodec::ProRes_422_Proxy: return 0;
                        case VideoCodec::ProRes_422_LT: return 1;
                        case VideoCodec::ProRes_422: return 2;
                        case VideoCodec::ProRes_422_HQ: return 3;
                        case VideoCodec::ProRes_4444: return 4;
                        case VideoCodec::ProRes_4444_XQ: return 5;
                        default: return -1;
                }
        }

        bool isProRes(VideoCodec::ID id) { return proresProfileFor(id) >= 0; }

        // The compressed @ref PixelFormat that tags an emitted MJPEG packet,
        // chosen from the (already-validated) input pixel format.
        PixelFormat::ID jpegCompressedFormatFor(PixelFormat::ID input) {
                switch (input) {
                        case PixelFormat::YUV8_420_Planar_Rec709: return PixelFormat::JPEG_YUV8_420_Rec709;
                        case PixelFormat::YUV8_422_Planar_Rec709: return PixelFormat::JPEG_YUV8_422_Rec709;
                        default: return PixelFormat::Invalid;
                }
        }

        // -------------------------------------------------------------------
        // Shared plane-copy helpers
        // -------------------------------------------------------------------

        // Copies the planes of a decoded AVFrame into a freshly-allocated
        // promeki @ref UncompressedVideoPayload, plane-for-plane, honouring
        // each side's row stride.  The promeki layout is the single source of
        // truth for plane count and destination geometry — any extra source
        // plane (e.g. ProRes-4444 alpha) is simply not copied.
        void copyFrameToPayload(const AVFrame *fr, UncompressedVideoPayload *dst) {
                const PixelFormat    &pf = dst->desc().pixelFormat();
                const PixelMemLayout &ml = pf.memLayout();
                const size_t          planes = pf.planeCount();
                const size_t          width = dst->desc().size().width();
                for (size_t c = 0; c < planes && c < AV_NUM_DATA_POINTERS; ++c) {
                        const size_t dstStride = ml.lineStride(c, width);
                        if (dstStride == 0) continue;
                        const size_t planeBytes = pf.planeSize(c, dst->desc());
                        const size_t rows = planeBytes / dstStride;
                        const int    srcStride = fr->linesize[c];
                        if (srcStride <= 0 || fr->data[c] == nullptr) continue;
                        const size_t copyBytes = std::min<size_t>(dstStride, static_cast<size_t>(srcStride));
                        uint8_t      *dstPtr = dst->data()[c].data();
                        const uint8_t *srcPtr = fr->data[c];
                        if (dstPtr == nullptr) continue;
                        for (size_t r = 0; r < rows; ++r) {
                                std::memcpy(dstPtr + r * dstStride, srcPtr + r * static_cast<size_t>(srcStride),
                                            copyBytes);
                        }
                }
        }

        // -------------------------------------------------------------------
        // FfmpegVideoDecoder
        // -------------------------------------------------------------------

        class FfmpegVideoDecoder : public VideoDecoder {
                public:
                        FfmpegVideoDecoder() = default;

                        ~FfmpegVideoDecoder() override { teardown(); }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                CompressedVideoPayload::Ptr payload = selectInputPayload(frame);
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        promekiWarnThrottled(1000,
                                                "FfmpegVideoDecoder::submitFrame: no compressed video payload on frame");
                                        setError(Error::Invalid, "no compressed video payload on frame");
                                        return _lastError;
                                }
                                if (!ensureDecoder()) return _lastError;

                                auto view = payload->plane(0);
                                if (view.size() == 0) return Error::Ok;

                                // Key the source Frame + per-payload state by a
                                // monotonic counter handed to FFmpeg as the packet
                                // PTS; avcodec hands it back on the decoded frame's
                                // best_effort_timestamp so reordered (B-frame)
                                // output still pairs with the right source.
                                const int64_t key = _inputCounter++;
                                // Capture the container's declared colorimetry from
                                // the compressed payload's PixelFormat ColorModel
                                // (the demuxer rebases it from a colr atom when one
                                // is present).  It fills colour in when the bitstream
                                // itself signals none — see effectiveColorModel.
                                _pending.insert(key, PendingSource{frame, payload->pts(), payload->metadata(),
                                                                    payload->streamIndex(),
                                                                    payload->desc().pixelFormat().colorModel().id()});

                                // Reuse the persistent packet (av_packet_unref clears
                                // any prior borrowed state).  The data is borrowed from
                                // the payload's BufferView — not refcounted — so unref
                                // never frees it; we clear data/size after the send.
                                av_packet_unref(_pkt);
                                _pkt->data = const_cast<uint8_t *>(static_cast<const uint8_t *>(view.data()));
                                _pkt->size = static_cast<int>(view.size());
                                _pkt->pts = key;
                                _pkt->dts = key;
                                int rc = avcodec_send_packet(_ctx, _pkt);
                                _pkt->data = nullptr;
                                _pkt->size = 0;
                                if (rc < 0 && rc != AVERROR(EAGAIN)) {
                                        setError(Error::DecodeFailed,
                                                 String::sprintf("avcodec_send_packet failed: %s", ffmpegErrorString(rc).cstr()));
                                        return _lastError;
                                }
                                drainFrames();
                                return Error::Ok;
                        }

                        Frame receiveFrame() override {
                                if (_outQueue.isEmpty()) return Frame();
                                return _outQueue.popFromFront();
                        }

                        Error flush() override {
                                if (_ctx != nullptr) {
                                        avcodec_send_packet(_ctx, nullptr); // enter draining mode
                                        drainFrames();
                                }
                                return Error::Ok;
                        }

                        Error reset() override {
                                _outQueue.clear();
                                _pending.clear();
                                _inputCounter = 0;
                                if (_ctx != nullptr) avcodec_flush_buffers(_ctx);
                                return Error::Ok;
                        }

                private:
                        struct PendingSource {
                                        Frame          source;
                                        MediaTimeStamp pts;
                                        Metadata       meta;
                                        int            streamIndex = -1;
                                        ColorModel::ID containerCm = ColorModel::Invalid;
                        };

                        bool ensureDecoder() {
                                if (_ctx != nullptr) return true;
                                const AVCodecID id = avDecodeIdFor(codec().id());
                                const AVCodec  *dec = avcodec_find_decoder(id);
                                if (dec == nullptr) {
                                        setError(Error::NotSupported, "no FFmpeg decoder for this codec");
                                        return false;
                                }
                                _ctx = avcodec_alloc_context3(dec);
                                if (_ctx == nullptr) {
                                        setError(Error::LibraryFailure, "avcodec_alloc_context3 failed");
                                        return false;
                                }
                                // ProRes: stamp the QuickTime codec_tag from the
                                // codec's own well-known FourCC (apco/apcs/apcn/
                                // apch/ap4h/ap4x).  This lets libavcodec recognise
                                // the exact profile and decode at the bitstream's
                                // native bit depth — 10-bit for the 422 family,
                                // 12-bit 4:4:4 for 4444 / 4444 XQ (mapped to
                                // YUV12_444_Planar_LE_Rec709 in promekiPixelFormatFor).
                                // Leaving it unset makes the decoder log "Unknown
                                // prores profile 0" and silently clamp 4444 / XQ
                                // content down to 10-bit, losing two bits of
                                // precision on genuine 12-bit assets (e.g. anything
                                // a hardware ProRes codec or another NLE produced).
                                // FourCC::value() is big-endian (c0 in the MSB);
                                // libavcodec's codec_tag follows MKTAG (c0 in the
                                // LSB), so byte-swap.
                                const List<PixelFormat> cpfList = codec().compressedPixelFormats();
                                if (isProRes(codec().id()) && !cpfList.isEmpty()) {
                                        const List<FourCC> fl = cpfList[0].fourccList();
                                        if (!fl.isEmpty()) {
                                                const uint32_t be = fl[0].value();
                                                _ctx->codec_tag = (be >> 24) | ((be >> 8) & 0x0000ff00u) |
                                                                  ((be << 8) & 0x00ff0000u) | (be << 24);
                                        }
                                }

                                // Multi-threaded decode (the AVCodecContext default
                                // is single-threaded — thread_count == 1).  0 =
                                // auto-detect the core count, like x264's i_threads;
                                // MediaConfig::CodecThreads caps it (e.g. to avoid
                                // oversubscription with many parallel sessions).
                                // Frame threading adds a few frames of startup
                                // latency (FF_THREAD_FRAME) but our drain loop +
                                // flush handle it, and reordered/delayed output
                                // still pairs with its source via best_effort_timestamp.
                                _ctx->thread_count = std::max(0, config().getAs<int32_t>(MediaConfig::CodecThreads, 0));

                                // Caller VUI colour-description overrides (Auto /
                                // Unspecified = "use the bitstream / container
                                // value").  Read once; applied at highest
                                // precedence in effectiveColorModel().
                                _cfgPrimaries = config()
                                                        .getAs<ColorPrimaries>(MediaConfig::VideoColorPrimaries,
                                                                               ColorPrimaries::Auto)
                                                        .value();
                                _cfgTransfer = config()
                                                       .getAs<TransferCharacteristics>(
                                                               MediaConfig::VideoTransferCharacteristics,
                                                               TransferCharacteristics::Auto)
                                                       .value();
                                _cfgMatrix = config()
                                                     .getAs<MatrixCoefficients>(MediaConfig::VideoMatrixCoefficients,
                                                                                MatrixCoefficients::Auto)
                                                     .value();

                                int rc = avcodec_open2(_ctx, dec, nullptr);
                                if (rc < 0) {
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("avcodec_open2 failed: %s", ffmpegErrorString(rc).cstr()));
                                        teardown();
                                        return false;
                                }
                                // Persistent packet / frame reused across every submit
                                // and drain — avoids an alloc/free pair per access unit.
                                _pkt = av_packet_alloc();
                                _frame = av_frame_alloc();
                                if (_pkt == nullptr || _frame == nullptr) {
                                        setError(Error::LibraryFailure, "av_packet_alloc / av_frame_alloc failed");
                                        teardown();
                                        return false;
                                }
                                return true;
                        }

                        void drainFrames() {
                                while (true) {
                                        int rc = avcodec_receive_frame(_ctx, _frame);
                                        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
                                        if (rc < 0) {
                                                setError(Error::DecodeFailed,
                                                         String::sprintf("avcodec_receive_frame failed: %s",
                                                                         ffmpegErrorString(rc).cstr()));
                                                break;
                                        }
                                        emitFrame(_frame);
                                        av_frame_unref(_frame);
                                }
                        }

                        // Resolves the @ref ColorModel for a decoded frame.  The
                        // bitstream's own VUI (the AVFrame's color_primaries /
                        // color_trc / colorspace) is primary — it's frame-accurate
                        // and present for every codec this backend decodes
                        // (ProRes / H.26x / AV1 / VP9).  A caller config override
                        // (@c MediaConfig::VideoColorPrimaries etc.) wins per axis.
                        // When the bitstream signals no colour at all, the
                        // container colorimetry (@p containerCm, carried on the
                        // compressed payload's PixelFormat from a @c colr atom)
                        // fills the gap.  A YUV decode never has an Identity
                        // matrix, so an unspecified matrix is inferred from the
                        // primaries to keep @ref ColorModel::fromH273 on the
                        // Y'CbCr branch rather than the RGB one.
                        ColorModel::ID effectiveColorModel(const AVFrame *fr, ColorModel::ID containerCm) const {
                                int prim = fr->color_primaries;
                                int trc = fr->color_trc;
                                int mtx = fr->colorspace;
                                if (isConcreteColorCode(_cfgPrimaries)) prim = _cfgPrimaries;
                                if (isConcreteColorCode(_cfgTransfer)) trc = _cfgTransfer;
                                if (isConcreteColorCode(_cfgMatrix)) mtx = _cfgMatrix;
                                const bool haveAny = isConcreteColorCode(prim) || isConcreteColorCode(trc) ||
                                                     isConcreteColorCode(mtx);
                                if (!haveAny && containerCm != ColorModel::Invalid) return containerCm;
                                if (!isConcreteColorCode(mtx)) {
                                        mtx = (prim == 9) ? 9 : (prim == 5 || prim == 6) ? 6 : 1;
                                }
                                return ColorModel::fromH273(static_cast<uint8_t>(prim), static_cast<uint8_t>(trc),
                                                            static_cast<uint8_t>(mtx));
                        }

                        void emitFrame(AVFrame *fr) {
                                // Recover the source Frame + per-payload state for
                                // this decoded picture by its passed-through PTS.
                                PendingSource ps;
                                const int64_t key = fr->best_effort_timestamp;
                                if (_pending.contains(key)) {
                                        ps = _pending.value(key);
                                        _pending.remove(key);
                                } else if (!_pending.isEmpty()) {
                                        // Fallback: pair with the oldest pending entry.
                                        auto it = _pending.begin();
                                        ps = it->second;
                                        _pending.remove(it->first);
                                }

                                const ColorModel::ID cm = effectiveColorModel(fr, ps.containerCm);
                                const PixelFormat::ID pfId = promekiPixelFormatFor(fr->format, cm);
                                if (pfId == PixelFormat::Invalid) {
                                        const char *n = av_get_pix_fmt_name(static_cast<AVPixelFormat>(fr->format));
                                        promekiWarnThrottled(1000,
                                                "FfmpegVideoDecoder: decoded pixel format %s has no promeki mapping",
                                                n ? n : "?");
                                        setError(Error::PixelFormatNotSupported,
                                                 "decoded pixel format has no promeki mapping");
                                        return;
                                }

                                ImageDesc desc(Size2Du32(static_cast<uint32_t>(fr->width),
                                                         static_cast<uint32_t>(fr->height)),
                                               PixelFormat(pfId));
                                if (!ps.meta.isEmpty()) desc.metadata() = ps.meta;

                                // Preferred path: wrap the decoded planes zero-copy.
                                // We can do so whenever promeki's per-plane stride can
                                // be made to equal FFmpeg's linesize — i.e. linesize is
                                // the tight stride plus some trailing padding, which we
                                // record as per-plane linePad (lineAlign 1).  That holds
                                // for every standard resolution (and any frame whose
                                // width is a multiple of the decoder's alignment).  When
                                // it does not — e.g. a width the codec macroblock-padded
                                // beyond a simple per-line pad — we fall back to the
                                // plane-by-plane copy, which honours each side's stride.
                                UncompressedVideoPayload::Ptr img = tryWrapDecoded(fr, desc);
                                if (!img.isValid()) {
                                        img = allocator()->allocateVideoPayload(desc);
                                        if (!img.isValid()) {
                                                setError(Error::LibraryFailure,
                                                         "failed to allocate output video payload");
                                                return;
                                        }
                                        copyFrameToPayload(fr, img.modify());
                                }
                                if (ps.pts.isValid()) img.modify()->setPts(ps.pts);
                                if (ps.streamIndex >= 0) img.modify()->setStreamIndex(ps.streamIndex);
                                _outQueue.pushToBack(buildOutputFrame(ps.source, std::move(img)));
                        }

                        // Builds a zero-copy UncompressedVideoPayload over the planes
                        // of `fr`, carrying FFmpeg's per-plane linesize as the payload
                        // descriptor's per-plane linePad.  Returns an invalid Ptr when
                        // the layout can't be expressed as tight+pad (caller then
                        // copies).  `descMeta` supplies size / pixel format / metadata.
                        UncompressedVideoPayload::Ptr tryWrapDecoded(AVFrame *fr, const ImageDesc &descMeta) {
                                const PixelFormat    &pf = descMeta.pixelFormat();
                                const PixelMemLayout &ml = pf.memLayout();
                                const size_t          planes = pf.planeCount();
                                if (planes == 0 || planes > AV_NUM_DATA_POINTERS) {
                                        return UncompressedVideoPayload::Ptr();
                                }
                                const size_t width = descMeta.size().width();
                                ImageDesc    wrapDesc = descMeta; // carries per-plane pad
                                size_t       planeSizes[AV_NUM_DATA_POINTERS] = {0};
                                for (size_t c = 0; c < planes; ++c) {
                                        const size_t tight = ml.lineStride(c, width);
                                        const int    lsRaw = fr->linesize[c];
                                        if (tight == 0 || lsRaw <= 0) return UncompressedVideoPayload::Ptr();
                                        const size_t ls = static_cast<size_t>(lsRaw);
                                        if (ls < tight) return UncompressedVideoPayload::Ptr();
                                        const size_t planeTight = pf.planeSize(c, descMeta);
                                        const size_t rows = planeTight / tight;
                                        if (rows == 0) return UncompressedVideoPayload::Ptr();
                                        wrapDesc.setLineAlign(c, 1);
                                        wrapDesc.setLinePad(c, ls - tight);
                                        planeSizes[c] = ls * rows;
                                }
                                BufferView view = ffmpegWrapFramePlanes(fr, planeSizes, planes);
                                if (view.count() != planes) return UncompressedVideoPayload::Ptr();
                                return UncompressedVideoPayload::Ptr::create(wrapDesc, view);
                        }

                        void teardown() {
                                if (_frame != nullptr) av_frame_free(&_frame);
                                if (_pkt != nullptr) av_packet_free(&_pkt);
                                if (_ctx != nullptr) avcodec_free_context(&_ctx);
                        }

                        AVCodecContext         *_ctx = nullptr;
                        AVPacket               *_pkt = nullptr;
                        AVFrame                *_frame = nullptr;
                        int64_t                 _inputCounter = 0;
                        Map<int64_t, PendingSource> _pending;
                        Deque<Frame>            _outQueue;
                        // Caller VUI colour overrides (Auto = 255 = no override).
                        int                     _cfgPrimaries = 255;
                        int                     _cfgTransfer = 255;
                        int                     _cfgMatrix = 255;
        };

        // -------------------------------------------------------------------
        // FfmpegVideoEncoder (ProRes + MJPEG)
        // -------------------------------------------------------------------

        class FfmpegVideoEncoder : public VideoEncoder {
                public:
                        FfmpegVideoEncoder() = default;

                        ~FfmpegVideoEncoder() override { teardown(); }

                        void onConfigure(const MediaConfig &config) override {
                                int32_t q = config.getAs<int32_t>(MediaConfig::JpegQuality);
                                if (q > 0) _jpegQuality = std::clamp<int>(q, 1, 100);
                                _frameRate = config.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate());
                        }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                UncompressedVideoPayload::Ptr payload = selectInputPayload(frame);
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        promekiWarnThrottled(1000,
                                                "FfmpegVideoEncoder::submitFrame: no uncompressed video payload on frame");
                                        setError(Error::Invalid, "no uncompressed video payload on frame");
                                        return _lastError;
                                }
                                if (!ensureEncoder(payload->desc())) return _lastError;

                                const ImageDesc &idesc = payload->desc();
                                if (idesc.size().width() != _width || idesc.size().height() != _height ||
                                    idesc.pixelFormat().id() != _inputFmtId) {
                                        setError(Error::Invalid,
                                                 "FfmpegVideoEncoder does not support mid-stream format/size changes");
                                        return _lastError;
                                }

                                // Reuse the persistent input frame; we borrow the
                                // payload planes (no av_frame_get_buffer), so unref is
                                // a cheap reset and we clear the pointers after the send.
                                AVFrame *fr = _frame;
                                av_frame_unref(fr);
                                fr->width  = static_cast<int>(_width);
                                fr->height = static_cast<int>(_height);
                                fr->format = _avPixFmt;
                                // Point FFmpeg straight at the payload planes —
                                // send_frame copies what it needs for this
                                // synchronous, intra-only encode, so no
                                // av_frame_get_buffer round-trip is required.
                                const PixelMemLayout &ml = idesc.pixelFormat().memLayout();
                                const size_t          planes = idesc.pixelFormat().planeCount();
                                for (size_t c = 0; c < planes && c < AV_NUM_DATA_POINTERS; ++c) {
                                        fr->data[c]     = const_cast<uint8_t *>(payload->plane(c).data());
                                        fr->linesize[c] = static_cast<int>(ml.lineStride(c, _width));
                                }
                                fr->pts = _inputCounter++;
                                if (_qscaleMode) fr->quality = _ctx->global_quality;

                                _currentSource = frame;
                                _currentPts    = payload->pts();
                                _videoEchoDone = false;

                                int rc = avcodec_send_frame(_ctx, fr);
                                // Release plane references — they are borrowed from the
                                // payload and must not be freed with the frame.
                                for (int c = 0; c < AV_NUM_DATA_POINTERS; ++c) {
                                        fr->data[c]     = nullptr;
                                        fr->linesize[c] = 0;
                                }
                                if (rc < 0) {
                                        setError(Error::EncodeFailed,
                                                 String::sprintf("avcodec_send_frame failed: %s", ffmpegErrorString(rc).cstr()));
                                        return _lastError;
                                }
                                drainPackets();

                                // ProRes / MJPEG are 1-in-1-out intra codecs, so a
                                // packet always fired above and echoed the source.
                                // Guard anyway: if somehow nothing fired, still echo
                                // the source's audio / ANC so it reaches the sink.
                                if (!_videoEchoDone && _currentSource.isValid() &&
                                    (!_currentSource.audioPayloads().isEmpty() ||
                                     !_currentSource.ancPayloads().isEmpty())) {
                                        _outQueue.pushToBack(
                                                buildOutputFrame(_currentSource, CompressedVideoPayload::Ptr()));
                                        _videoEchoDone = true;
                                }
                                return Error::Ok;
                        }

                        Frame receiveFrame() override {
                                if (_outQueue.isEmpty()) {
                                        if (_flushed && !_eosEmitted) {
                                                _eosEmitted = true;
                                                ImageDesc cdesc(Size2Du32(0, 0), PixelFormat(_compressedFmtId));
                                                auto      eos = CompressedVideoPayload::Ptr::create(cdesc);
                                                eos.modify()->markEndOfStream();
                                                Frame f;
                                                f.addPayload(eos);
                                                return f;
                                        }
                                        return Frame();
                                }
                                return _outQueue.popFromFront();
                        }

                        Error flush() override {
                                if (_ctx != nullptr) {
                                        avcodec_send_frame(_ctx, nullptr);
                                        drainPackets();
                                }
                                _flushed = true;
                                return Error::Ok;
                        }

                        Error reset() override {
                                _outQueue.clear();
                                _flushed       = false;
                                _eosEmitted    = false;
                                _inputCounter  = 0;
                                _currentSource = Frame();
                                _videoEchoDone = false;
                                if (_ctx != nullptr) avcodec_flush_buffers(_ctx);
                                return Error::Ok;
                        }

                private:
                        bool ensureEncoder(const ImageDesc &idesc) {
                                if (_ctx != nullptr) return true;

                                const VideoCodec::ID cid = codec().id();
                                const int            avInPix = avPixelFormatForInput(idesc.pixelFormat().id());
                                if (avInPix == AV_PIX_FMT_NONE) {
                                        setError(Error::PixelFormatNotSupported,
                                                 String::sprintf("FfmpegVideoEncoder: unsupported input format %s",
                                                                 idesc.pixelFormat().name().cstr()));
                                        return false;
                                }

                                const AVCodec *enc = nullptr;
                                int            proresProfile = -1;
                                if (isProRes(cid)) {
                                        enc = avcodec_find_encoder_by_name("prores_ks");
                                        if (enc == nullptr) enc = avcodec_find_encoder(AV_CODEC_ID_PRORES);
                                        proresProfile = proresProfileFor(cid);
                                        // ProRes carries its codec identity on the
                                        // compressed PixelFormat — reuse the codec's
                                        // own well-known compressed format.
                                        _compressedFmtId = codec().compressedPixelFormats().isEmpty()
                                                                   ? PixelFormat::Invalid
                                                                   : codec().compressedPixelFormats()[0].id();
                                } else if (cid == VideoCodec::JPEG) {
                                        enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
                                        _compressedFmtId = jpegCompressedFormatFor(idesc.pixelFormat().id());
                                }
                                if (enc == nullptr) {
                                        setError(Error::NotSupported, "no FFmpeg encoder for this codec");
                                        return false;
                                }
                                if (_compressedFmtId == PixelFormat::Invalid) {
                                        setError(Error::Invalid, "no compressed PixelFormat for this codec/input");
                                        return false;
                                }

                                _ctx = avcodec_alloc_context3(enc);
                                if (_ctx == nullptr) {
                                        setError(Error::LibraryFailure, "avcodec_alloc_context3 failed");
                                        return false;
                                }
                                _width      = idesc.size().width();
                                _height     = idesc.size().height();
                                _inputFmtId = idesc.pixelFormat().id();
                                _avPixFmt   = avInPix;

                                _ctx->width   = static_cast<int>(_width);
                                _ctx->height  = static_cast<int>(_height);
                                _ctx->pix_fmt = static_cast<AVPixelFormat>(avInPix);
                                // Multi-threaded encode (default is single-threaded).
                                // SLICE threads only, NOT frame threads: frame
                                // threading delays a packet by one frame per thread,
                                // which would desync this session's 1-in-1-out
                                // packet↔source pairing (each emitted packet is paired
                                // with the most-recently-submitted Frame's audio / ANC
                                // / PTS).  prores_ks and mjpeg both advertise
                                // AV_CODEC_CAP_SLICE_THREADS, so this still parallelises
                                // across cores within each frame with zero added delay.
                                // MediaConfig::CodecThreads caps the count (0 = auto).
                                _ctx->thread_count = std::max(0, config().getAs<int32_t>(MediaConfig::CodecThreads, 0));
                                _ctx->thread_type  = FF_THREAD_SLICE;
                                // 1 tick == 1 frame.  Fall back to 25/1 when no rate
                                // was configured — intra codecs don't rate-control on
                                // it, but FFmpeg wants a sane non-zero time base.
                                FrameRate fr = _frameRate.isValid() ? _frameRate : FrameRate(FrameRate::RationalType(25, 1));
                                _ctx->time_base.num = static_cast<int>(fr.denominator());
                                _ctx->time_base.den = static_cast<int>(fr.numerator());

                                if (proresProfile >= 0) {
                                        av_opt_set_int(_ctx->priv_data, "profile", proresProfile, 0);
                                } else {
                                        // MJPEG: drive quality off MediaConfig::JpegQuality
                                        // via fixed-qscale rate control.  Map 1..100
                                        // (higher = better) onto FFmpeg's qscale 31..1
                                        // (lower = better).
                                        const int qscale = std::clamp(31 - (_jpegQuality - 1) * 30 / 99, 1, 31);
                                        _ctx->flags |= AV_CODEC_FLAG_QSCALE;
                                        _ctx->global_quality = FF_QP2LAMBDA * qscale;
                                        _qscaleMode = true;
                                        // Our planar inputs are limited-range (Rec.709);
                                        // FFmpeg's MJPEG encoder otherwise insists on
                                        // full-range yuvj* unless compliance is relaxed.
                                        _ctx->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
                                }

                                int rc = avcodec_open2(_ctx, enc, nullptr);
                                if (rc < 0) {
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("avcodec_open2 failed: %s", ffmpegErrorString(rc).cstr()));
                                        teardown();
                                        return false;
                                }
                                // Persistent input frame / output packet reused across
                                // every submit and drain — no per-frame alloc/free pair.
                                _frame = av_frame_alloc();
                                _pkt = av_packet_alloc();
                                if (_frame == nullptr || _pkt == nullptr) {
                                        setError(Error::LibraryFailure, "av_frame_alloc / av_packet_alloc failed");
                                        teardown();
                                        return false;
                                }
                                return true;
                        }

                        void drainPackets() {
                                while (true) {
                                        int rc = avcodec_receive_packet(_ctx, _pkt);
                                        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
                                        if (rc < 0) {
                                                setError(Error::EncodeFailed,
                                                         String::sprintf("avcodec_receive_packet failed: %s",
                                                                         ffmpegErrorString(rc).cstr()));
                                                break;
                                        }
                                        emitPacket(_pkt);
                                        av_packet_unref(_pkt);
                                }
                        }

                        void emitPacket(AVPacket *avpkt) {
                                ImageDesc cdesc(Size2Du32(_width, _height), PixelFormat(_compressedFmtId));
                                // Zero-copy: wrap the encoder's packet buffer.  Fall
                                // back to a copy only if the packet isn't refcountable.
                                BufferView view = ffmpegWrapPacket(avpkt);
                                if (view.count() != 1) {
                                        Buffer buf(static_cast<size_t>(avpkt->size));
                                        std::memcpy(buf.data(), avpkt->data, static_cast<size_t>(avpkt->size));
                                        buf.setSize(static_cast<size_t>(avpkt->size));
                                        view = BufferView(buf, 0, buf.size());
                                }
                                auto pkt = CompressedVideoPayload::Ptr::create(cdesc, view);
                                pkt.modify()->setPts(_currentPts);
                                pkt.modify()->setDts(_currentPts);
                                // Intra-only: every access unit is a keyframe.
                                pkt.modify()->setFrameType(FrameType::I);
                                pkt.modify()->setFlag(MediaPayload::Keyframe, true);
                                if (_frameRate.isValid()) {
                                        Rational<int> r(static_cast<int>(_frameRate.numerator()),
                                                        static_cast<int>(_frameRate.denominator()));
                                        pkt.modify()->setDuration(Duration::fromSamples(int64_t(1), r));
                                }
                                if (_videoEchoDone) {
                                        Frame videoOnly;
                                        videoOnly.addPayload(pkt);
                                        _outQueue.pushToBack(std::move(videoOnly));
                                } else {
                                        _outQueue.pushToBack(buildOutputFrame(_currentSource, std::move(pkt)));
                                        _videoEchoDone = true;
                                }
                        }

                        void teardown() {
                                if (_frame != nullptr) av_frame_free(&_frame);
                                if (_pkt != nullptr) av_packet_free(&_pkt);
                                if (_ctx != nullptr) avcodec_free_context(&_ctx);
                        }

                        AVCodecContext *_ctx = nullptr;
                        AVFrame        *_frame = nullptr;
                        AVPacket       *_pkt = nullptr;
                        uint32_t        _width  = 0;
                        uint32_t        _height = 0;
                        PixelFormat::ID _inputFmtId = PixelFormat::Invalid;
                        PixelFormat::ID _compressedFmtId = PixelFormat::Invalid;
                        int             _avPixFmt = AV_PIX_FMT_NONE;
                        FrameRate       _frameRate;
                        int             _jpegQuality = 90;
                        bool            _qscaleMode = false;
                        int64_t         _inputCounter = 0;
                        Deque<Frame>    _outQueue;
                        Frame           _currentSource;
                        MediaTimeStamp  _currentPts;
                        bool            _videoEchoDone = false;
                        bool            _flushed = false;
                        bool            _eosEmitted = false;
        };

        // -------------------------------------------------------------------
        // Static registration — fallback weight (one band below x264's
        // software encoder; well below the hardware / vendored backends).
        // -------------------------------------------------------------------

        // Universal-fallback weight: below x264 (Vendored-10), NVENC/NVDEC and
        // the libjpeg-turbo JPEG backend (all Vendored), so FFmpeg is only
        // auto-selected for codecs nothing else services.  An explicit pin
        // ("H264:FFmpeg") or MediaConfig::CodecBackend always overrides it.
        constexpr int kFfmpegVideoWeight = BackendWeight::Vendored - 20;

        struct FfmpegVideoRegistrar {
                        FfmpegVideoRegistrar() {
                                ffmpegInstallLogBridge();
                                auto bk = VideoCodec::registerBackend("FFmpeg");
                                if (error(bk).isError()) return;
                                const VideoCodec::Backend backend = value(bk);

                                // ---- Decoders: native LGPL temporal codecs + ProRes ----
                                // (VP9 omitted on purpose — no PixelFormat::VP9
                                // compressed-format ID exists, so no container
                                // backend can surface a VP9 access unit to feed it.)
                                const VideoCodec::ID decoders[] = {
                                        VideoCodec::H264,
                                        VideoCodec::HEVC,
                                        VideoCodec::AV1,
                                        VideoCodec::ProRes_422_Proxy,
                                        VideoCodec::ProRes_422_LT,
                                        VideoCodec::ProRes_422,
                                        VideoCodec::ProRes_422_HQ,
                                        VideoCodec::ProRes_4444,
                                        VideoCodec::ProRes_4444_XQ,
                                };
                                for (VideoCodec::ID id : decoders) {
                                        VideoDecoder::registerBackend({
                                                .codecId = id,
                                                .backend = backend,
                                                .weight  = kFfmpegVideoWeight,
                                                // Empty: this backend emits whatever
                                                // planar/semi-planar surface the stream
                                                // decodes to; the planner CSCs onward.
                                                .supportedOutputs = {},
                                                .factory = []() -> VideoDecoder * {
                                                        return new FfmpegVideoDecoder();
                                                },
                                        });
                                }

                                // ---- Encoders: ProRes (6 variants) ----
                                const List<int> proresInputs422 = {
                                        static_cast<int>(PixelFormat::YUV10_422_Planar_LE_Rec709),
                                };
                                const List<int> proresInputs4444 = {
                                        static_cast<int>(PixelFormat::YUV10_444_Planar_LE_Rec709),
                                };
                                const VideoCodec::ID prores422[] = {
                                        VideoCodec::ProRes_422_Proxy, VideoCodec::ProRes_422_LT,
                                        VideoCodec::ProRes_422, VideoCodec::ProRes_422_HQ,
                                };
                                for (VideoCodec::ID id : prores422) {
                                        VideoEncoder::registerBackend({
                                                .codecId = id,
                                                .backend = backend,
                                                .weight  = kFfmpegVideoWeight,
                                                .supportedInputs = proresInputs422,
                                                .factory = []() -> VideoEncoder * {
                                                        return new FfmpegVideoEncoder();
                                                },
                                        });
                                }
                                const VideoCodec::ID prores4444[] = {
                                        VideoCodec::ProRes_4444,
                                        VideoCodec::ProRes_4444_XQ,
                                };
                                for (VideoCodec::ID id : prores4444) {
                                        VideoEncoder::registerBackend({
                                                .codecId = id,
                                                .backend = backend,
                                                .weight  = kFfmpegVideoWeight,
                                                .supportedInputs = proresInputs4444,
                                                .factory = []() -> VideoEncoder * {
                                                        return new FfmpegVideoEncoder();
                                                },
                                        });
                                }

                                // ---- Encoder: Motion-JPEG (4:2:0 / 4:2:2) ----
                                VideoEncoder::registerBackend({
                                        .codecId = VideoCodec::JPEG,
                                        .backend = backend,
                                        .weight  = kFfmpegVideoWeight,
                                        .supportedInputs =
                                                {
                                                        static_cast<int>(PixelFormat::YUV8_420_Planar_Rec709),
                                                        static_cast<int>(PixelFormat::YUV8_422_Planar_Rec709),
                                                },
                                        .factory = []() -> VideoEncoder * { return new FfmpegVideoEncoder(); },
                                });
                        }
        };

        static FfmpegVideoRegistrar _ffmpegVideoRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
