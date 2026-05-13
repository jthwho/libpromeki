/**
 * @file      tests/codectesthelpers.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Tiny helpers for tests that need to round-trip a compressed payload
 * through the VideoDecoder/VideoEncoder contract.  Declarations only —
 * bodies live in codectesthelpers.cpp.  Each unit test TU that pulls in
 * this header therefore pays only for the function-prototype plumbing,
 * not the template-heavy bodies (which previously parsed + instantiated
 * sharedPointerCast / Frame iteration / decode/encode round-trip in
 * every including TU).
 */

#pragma once

#include <cstddef>
#include <promeki/frame.h>
#include <promeki/mediapayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/mediaconfig.h>

namespace promeki {
        namespace tests {

                /// @brief Wrap a single payload in a single-payload Frame so a
                ///        payload-shaped test can drive the new Frame-based codec
                ///        @ref VideoEncoder::submitFrame /
                ///        @ref VideoDecoder::submitFrame APIs.
                ::promeki::Frame frameWith(::promeki::MediaPayload::Ptr payload);

                /// @brief Return the first @ref CompressedVideoPayload on a Frame.
                ::promeki::CompressedVideoPayload::Ptr firstCompressedVideo(const ::promeki::Frame &f);

                /// @brief Return the first @ref UncompressedVideoPayload on a Frame.
                ::promeki::UncompressedVideoPayload::Ptr firstUncompressedVideo(const ::promeki::Frame &f);

                /// @brief Return the first @ref CompressedAudioPayload on a Frame.
                ::promeki::CompressedAudioPayload::Ptr firstCompressedAudio(const ::promeki::Frame &f);

                /// @brief Return the first @ref PcmAudioPayload on a Frame.
                ::promeki::PcmAudioPayload::Ptr firstPcmAudio(const ::promeki::Frame &f);

                /// @brief One-shot decode of a compressed payload to @p target.
                ::promeki::UncompressedVideoPayload::Ptr
                decodeCompressedPayload(const ::promeki::CompressedVideoPayload::Ptr &src,
                                        const ::promeki::PixelFormat                 &target);

                /// @brief One-shot encode of an uncompressed payload to @p target.
                ::promeki::CompressedVideoPayload::Ptr
                encodePayloadToCompressed(const ::promeki::UncompressedVideoPayload::Ptr &src,
                                          const ::promeki::PixelFormat                   &target,
                                          const ::promeki::MediaConfig                   &cfg = ::promeki::MediaConfig());

                /// @brief Builds a gradient RGB8 payload of the given size.
                ::promeki::UncompressedVideoPayload::Ptr makeGradientRGB8Payload(std::size_t w, std::size_t h);

                /// @brief Mean absolute difference per byte between two RGB8 payloads.
                double rgb8MeanAbsDiffPayload(const ::promeki::UncompressedVideoPayload &a,
                                              const ::promeki::UncompressedVideoPayload &b);

        }
} // namespace promeki::tests
