/**
 * @file      ffmpegsupport.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Shared plumbing for the FFmpeg-backed codec backends (and any future
 * FFmpeg-backed work).  Keeps the libav* headers out of this promeki
 * header — only the thin promeki-facing surface lives here.
 */

#pragma once

#include <promeki/config.h>
#if PROMEKI_ENABLE_FFMPEG
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>

// Forward-declare the libav* types we expose by pointer, so this promeki
// header never pulls in the libavcodec / libavutil headers.  Their full
// definitions are only needed in the .cpp.
struct AVFrame;
struct AVPacket;
struct AVBufferRef;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Installs the libav* → promeki @ref Logger bridge.
 * @ingroup proav
 *
 * Routes every @c av_log message into the promeki @ref Logger instead of
 * FFmpeg's default stderr sink, so libavcodec / libavutil diagnostics
 * carry the same timestamp / thread / level decoration as the rest of
 * the library.  Level mapping:
 *
 *   - @c AV_LOG_PANIC / @c AV_LOG_FATAL → @c Logger::Err
 *   - @c AV_LOG_ERROR / @c AV_LOG_WARNING → @c Logger::Warn
 *   - @c AV_LOG_INFO and below → @c Logger::Debug, gated behind the
 *     @c FfmpegInternal debug category (@c PROMEKI_DEBUG=FfmpegInternal)
 *
 * FFmpeg's @c ERROR band is mapped to @c Warn rather than @c Err on
 * purpose: libavcodec emits it for recoverable per-packet stream
 * conditions ("non-existing PPS 0 referenced", "no frame!"), while the
 * codec backends surface genuine failures themselves via @ref Error
 * return codes.  The component prefix FFmpeg attaches ("[h264 @ …]") is
 * preserved.
 *
 * Idempotent and thread-safe — installed exactly once per process via
 * @c callOnce; the first caller wins and subsequent calls are no-ops.
 * Every FFmpeg backend calls this from its static registrar so the
 * bridge is in place before any libav* call.
 */
void ffmpegInstallLogBridge();

/**
 * @brief Formats an FFmpeg @c AVERROR code into a human-readable String.
 * @ingroup proav
 *
 * Thin wrapper around @c av_strerror.  Shared by every FFmpeg backend so
 * the error-string plumbing lives in one place.
 *
 * @param rc A negative @c AVERROR code.
 * @return The decoded message, or FFmpeg's generic fallback text.
 */
String ffmpegErrorString(int rc);

/**
 * @brief Wraps a single @c AVBufferRef as a zero-copy promeki @ref Buffer.
 * @ingroup proav
 *
 * Takes its @em own reference on @p ref (via @c av_buffer_ref) and holds it
 * for the lifetime of the returned Buffer's backing — the underlying FFmpeg
 * allocation stays alive until every promeki handle to it is dropped, at
 * which point the reference is released with @c av_buffer_unref.  No bytes
 * are copied; the Buffer's host pointer is @c ref->data.
 *
 * The backing is reported in @ref MemSpace::System (plain host RAM) and is
 * @em non-cloneable: it wraps memory FFmpeg owns, so
 * @ref Buffer::ensureExclusive cannot detach it.  Because a decoder may also
 * be holding the same buffer as a reference frame, callers must treat the
 * wrapped bytes as read-only.
 *
 * @param ref The AVBufferRef to wrap (must be non-null with non-null data).
 * @return A zero-copy Buffer over @p ref, or an invalid Buffer when @p ref
 *         is null / empty.
 */
Buffer ffmpegWrapBuffer(AVBufferRef *ref);

/**
 * @brief Builds a zero-copy @ref BufferView over the compressed bytes of a packet.
 * @ingroup proav
 *
 * Ensures @p pkt is reference-counted (@c av_packet_make_refcounted) and
 * wraps its backing @c AVBufferRef via @ref ffmpegWrapBuffer, returning a
 * single-slice BufferView covering @c [pkt->data, pkt->data + pkt->size).
 * Lets an encoder emit a @ref CompressedVideoPayload / @ref
 * CompressedAudioPayload without copying the packet out of FFmpeg.
 *
 * @param pkt The packet to wrap (its data must outlive nothing — the view
 *            holds its own reference).
 * @return A one-slice BufferView, or an empty BufferView on failure (the
 *         caller should fall back to a copy).
 */
BufferView ffmpegWrapPacket(AVPacket *pkt);

/**
 * @brief Builds a zero-copy @ref BufferView over the planes of a decoded frame.
 * @ingroup proav
 *
 * Exposes @p planeCount planes of @p fr, plane @c c covering
 * @c planeSizes[c] bytes starting at @c fr->data[c].  Each plane's owning
 * @c AVBufferRef is discovered via @c av_frame_get_plane_buffer; planes that
 * share one AVBufferRef (the common case — most decoders allocate every
 * plane in @c fr->buf[0]) are modelled as offset slices into a @em single
 * promeki @ref Buffer, while planes in distinct AVBufferRefs become distinct
 * Buffers.  Either way the resulting BufferView mirrors FFmpeg's real
 * allocation layout with no byte copies.
 *
 * Fails (returns an empty BufferView) when @p fr is not reference-counted
 * (a plane has no owning AVBufferRef) or a plane's exposed extent would run
 * past its buffer — the caller then falls back to a plane copy.
 *
 * @param fr         The decoded frame (non-const: plane-buffer lookup needs it).
 * @param planeSizes Per-plane byte extents to expose (length @p planeCount).
 * @param planeCount Number of planes to expose.
 * @return A BufferView with @p planeCount slices, or empty on failure.
 */
BufferView ffmpegWrapFramePlanes(AVFrame *fr, const size_t *planeSizes, size_t planeCount);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_FFMPEG
