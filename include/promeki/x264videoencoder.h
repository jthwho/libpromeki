/**
 * @file      x264videoencoder.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_X264
#include <promeki/namespace.h>
#include <promeki/videoencoder.h>
#include <promeki/uniqueptr.h>
#include <promeki/list.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;
class Frame;

/**
 * @brief Software H.264 encoder backend built on libx264.
 * @ingroup proav
 *
 * Registered against @ref VideoCodec::H264 under the backend name
 * @c "x264" (so @c VideoCodec("H264:x264") or
 * @c MediaConfig::CodecBackend = @c "x264" select it), it is the portable
 * software fallback that runs everywhere — including headless / ARM boxes
 * with no NVENC.  Hardware backends outrank it by weight, so an unpinned
 * @c VideoCodec("H264") prefers NVENC when a GPU is present and falls back
 * to this encoder otherwise (see @ref VideoEncoder backend selection).
 *
 * @par Licensing
 * libx264 is GPL-2.0-or-later; a build with @c PROMEKI_ENABLE_X264 on is
 * a GPL work (see @c THIRD-PARTY-LICENSES).  The whole translation unit is
 * gated on @c PROMEKI_ENABLE_X264.
 *
 * @par Input formats
 * Planar YUV at 8 / 10-bit and 4:2:0 / 4:2:2 / 4:4:4 — see
 * @ref supportedInputList.  Planes are fed to libx264 zero-copy (libx264
 * copies the pixels into its own frame pool during
 * @c x264_encoder_encode, so the source buffers need not outlive the
 * call).
 *
 * @par Session lifecycle
 * The real @c x264_t is built lazily on the first @ref submitFrame, once
 * the input dimensions / pixel format are known.  Output is emitted in
 * decode order (Annex-B); with B-frames disabled (the default for the
 * MediaIO path) each submit yields its packet synchronously.  Call
 * @ref flush at end-of-stream and keep draining @ref receiveFrame until a
 * frame carrying @c MediaPayload::Flags::EndOfStream arrives.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VideoEncoder.
 */
class X264VideoEncoder : public VideoEncoder {
        public:
                X264VideoEncoder();
                ~X264VideoEncoder() override;

                /**
                 * @brief Uncompressed @ref PixelFormat IDs this backend ingests.
                 *
                 * Mirrors the encoder's input classification table exactly —
                 * the planner picks the TPG / CSC output format from this
                 * list, so every ID here must be one @ref submitFrame
                 * accepts.  Planar YUV only: 8 / 10-bit × 4:2:0 / 4:2:2 /
                 * 4:4:4 (10-bit variants are little-endian 16-bit planar).
                 */
                static List<int> supportedInputList();

                Error submitFrame(const Frame &frame) override;
                Frame receiveFrame() override;
                Error flush() override;
                Error reset() override;
                void  requestKeyframe() override;

        protected:
                void onConfigure(const MediaConfig &config) override;

        private:
                struct Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr _impl;
                bool    _requestKey = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_X264
