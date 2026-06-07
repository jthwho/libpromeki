/**
 * @file      ffmpegsupport.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Shared FFmpeg plumbing: the libav* → promeki Logger bridge and the
 * AVERROR → String formatter, used by every FFmpeg-backed codec backend.
 */

#include <promeki/ffmpegsupport.h>

#if PROMEKI_ENABLE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
}

#include <cstdarg>
#include <cstddef>
#include <promeki/hostbufferimpl.h>
#include <promeki/logger.h>
#include <promeki/memspace.h>
#include <promeki/once.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -------------------------------------------------------------------
        // FfmpegBufferImpl — a zero-copy promeki Buffer backend over an
        // AVBufferRef.  Holds one reference on the FFmpeg buffer and releases
        // it on destruction, so a decoded AVFrame plane (or an encoded
        // AVPacket payload) can ride through promeki pipelines by value with
        // no byte copy.  Host-resident plain RAM, so it derives the host
        // pointer / fill / copyFromHost behaviour from HostMappedBufferImpl.
        //
        // Non-cloneable: the memory is FFmpeg's (and, for decoded reference
        // frames, may still be in use by the decoder), so it must be treated
        // as read-only and Buffer::ensureExclusive cannot detach it.
        class FfmpegBufferImpl : public HostMappedBufferImpl {
                public:
                        PROMEKI_SHARED_DERIVED(FfmpegBufferImpl)

                        // Takes ownership of one reference on @p ref (the caller
                        // must already have av_buffer_ref'd it for us).
                        explicit FfmpegBufferImpl(AVBufferRef *ref)
                            : HostMappedBufferImpl(MemSpace(MemSpace::System), ref->data,
                                                   static_cast<size_t>(ref->size), 0),
                              _ref(ref) {}

                        ~FfmpegBufferImpl() override {
                                if (_ref != nullptr) av_buffer_unref(&_ref);
                        }

                        bool canClone() const override { return false; }

                private:
                        AVBufferRef *_ref = nullptr;
        };

} // namespace

Buffer ffmpegWrapBuffer(AVBufferRef *ref) {
        if (ref == nullptr || ref->data == nullptr || ref->size == 0) return Buffer();
        AVBufferRef *own = av_buffer_ref(ref);
        if (own == nullptr) return Buffer();
        Buffer buf = Buffer::fromImpl(new FfmpegBufferImpl(own));
        // FfmpegBufferImpl took ownership of `own`; if fromImpl somehow
        // produced an invalid handle the impl (and its ref) is freed with it.
        return buf;
}

BufferView ffmpegWrapPacket(AVPacket *pkt) {
        if (pkt == nullptr || pkt->data == nullptr || pkt->size <= 0) return BufferView();
        if (av_packet_make_refcounted(pkt) < 0 || pkt->buf == nullptr) return BufferView();
        Buffer buf = ffmpegWrapBuffer(pkt->buf);
        if (!buf.isValid()) return BufferView();
        const ptrdiff_t off = pkt->data - pkt->buf->data;
        if (off < 0) return BufferView();
        const size_t offset = static_cast<size_t>(off);
        const size_t size = static_cast<size_t>(pkt->size);
        if (offset + size > buf.allocSize()) return BufferView();
        return BufferView(buf, offset, size);
}

BufferView ffmpegWrapFramePlanes(AVFrame *fr, const size_t *planeSizes, size_t planeCount) {
        if (fr == nullptr || planeSizes == nullptr || planeCount == 0) return BufferView();

        // Dedup wrapped Buffers by their owning AVBufferRef so planes that
        // share one FFmpeg allocation become offset slices into one promeki
        // Buffer.  planeCount is bounded by AV_NUM_DATA_POINTERS.
        struct Seen {
                        AVBufferRef *src = nullptr;
                        Buffer       buf;
        };
        Seen   seen[AV_NUM_DATA_POINTERS];
        size_t nseen = 0;

        BufferView view;
        for (size_t c = 0; c < planeCount && c < AV_NUM_DATA_POINTERS; ++c) {
                if (fr->data[c] == nullptr) return BufferView();
                AVBufferRef *pbuf = av_frame_get_plane_buffer(fr, static_cast<int>(c));
                if (pbuf == nullptr || pbuf->data == nullptr) return BufferView();

                Buffer buf;
                for (size_t s = 0; s < nseen; ++s) {
                        if (seen[s].src == pbuf) {
                                buf = seen[s].buf;
                                break;
                        }
                }
                if (!buf.isValid()) {
                        buf = ffmpegWrapBuffer(pbuf);
                        if (!buf.isValid()) return BufferView();
                        seen[nseen].src = pbuf;
                        seen[nseen].buf = buf;
                        ++nseen;
                }

                const ptrdiff_t off = fr->data[c] - pbuf->data;
                if (off < 0) return BufferView();
                const size_t offset = static_cast<size_t>(off);
                if (offset + planeSizes[c] > buf.allocSize()) return BufferView();
                view.pushToBack(buf, offset, planeSizes[c]);
        }
        return view;
}

namespace {

        // Verbose (INFO and below) libav* chatter is routed at Debug only when
        // this category is enabled, e.g. `PROMEKI_DEBUG=FfmpegInternal <cmd>`.
        // file/line aren't part of the av_log callback signature, so records
        // carry "ffmpeg" as the source — the FFmpeg component prefix in the
        // message body ("[h264 @ …]") identifies the originating subsystem.
        PROMEKI_DEBUG(FfmpegInternal);

        OnceFlag g_logBridgeInstalled;

        Logger::LogLevel mapAvLevel(int level) {
                if (level <= AV_LOG_FATAL) return Logger::LogLevel::Err;    // PANIC, FATAL
                if (level <= AV_LOG_WARNING) return Logger::LogLevel::Warn; // ERROR, WARNING
                return Logger::LogLevel::Debug;                             // INFO, VERBOSE, DEBUG, TRACE
        }

        // av_vlog invokes a custom callback unconditionally (it does NOT
        // pre-filter by av_log_get_level — that is the default callback's job),
        // so all filtering happens here.  We gate INFO-and-below behind the
        // FfmpegInternal category and bail before formatting to avoid the
        // per-message cost when verbose logging is off.
        void ffmpegLogCallback(void *avcl, int level, const char *fmt, va_list vl) {
                if (level > AV_LOG_WARNING && !_promeki_debug_enabled) return;

                char buf[1024];
                int  printPrefix = 1;
                int  n = av_log_format_line2(avcl, level, fmt, vl, buf, sizeof(buf), &printPrefix);
                if (n <= 0) return;

                String s(buf); // av_log_format_line2 NUL-terminates within line_size
                while (!s.isEmpty()) {
                        char c = s[s.size() - 1];
                        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
                        s.resize(s.size() - 1);
                }
                if (s.isEmpty()) return;

                Logger::defaultLogger().log(mapAvLevel(level), "ffmpeg", 0, s);
        }

} // namespace

void ffmpegInstallLogBridge() {
        callOnce(g_logBridgeInstalled, []() {
                av_log_set_callback(&ffmpegLogCallback);
                if (_promeki_debug_enabled) {
                        const unsigned v = avcodec_version();
                        Logger::defaultLogger().log(
                                Logger::LogLevel::Debug, "ffmpeg", 0,
                                String::sprintf("FFmpeg log bridge installed: libavcodec %u.%u.%u (%s)",
                                                AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v), AV_VERSION_MICRO(v),
                                                av_version_info()));
                }
        });
}

String ffmpegErrorString(int rc) {
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(rc, buf, sizeof(buf));
        return String(buf);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_FFMPEG
