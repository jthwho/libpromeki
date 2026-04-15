/**
 * @file      frame.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/benchmark.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/mediapacket.h>
#include <promeki/metadata.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A media frame containing images, audio, compressed packets, and metadata.
 * @ingroup proav
 *
 * Aggregates one or more uncompressed image planes, one or more
 * audio tracks, one or more @ref MediaPacket "compressed packets",
 * and a metadata container into a single unit that represents a
 * frame of media content.
 *
 * The @ref packetList companion to @ref imageList / @ref audioList
 * holds encoded bitstream access units — the output of a @ref
 * VideoEncoder, the input to a @ref VideoDecoder, or the on-disk
 * samples read from a muxed container.  A single Frame may carry
 * uncompressed essence (images / audio) and compressed packets
 * simultaneously — for example, while a pipeline is mid-encode the
 * encoder task's output can be spliced back onto the same Frame
 * that carried the source image for that PTS.
 *
 * @par Example
 * @code
 * MediaDesc mdesc;
 * Frame::Ptr frame = Frame::Ptr::create(mdesc);
 * frame->setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));
 * Image img = frame->image(0);
 *
 * // After encoding, attach a compressed packet alongside the image:
 * frame.modify()->packetList().pushToBack(encoded);
 * @endcode
 */
class Frame {
        PROMEKI_SHARED_FINAL(Frame)
        public:
                /** @brief Shared pointer type for Frame. */
                using Ptr = SharedPtr<Frame>;
                /** @brief Plain value list of Frame objects. */
                using List = promeki::List<Frame>;
                /** @brief List of shared pointers to Frame objects. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an empty frame with no images, audio, or metadata. */
                Frame() = default;

                /**
                 * @brief Returns a const reference to the list of image planes.
                 * @return The image pointer list.
                 */
                const Image::PtrList &imageList() const { return _imageList; }

                /**
                 * @brief Returns a mutable reference to the list of image planes.
                 * @return The image pointer list.
                 */
                Image::PtrList &imageList() { return _imageList; }

                /**
                 * @brief Returns a const reference to the list of audio tracks.
                 * @return The audio pointer list.
                 */
                const Audio::PtrList &audioList() const { return _audioList; }

                /**
                 * @brief Returns a mutable reference to the list of audio tracks.
                 * @return The audio pointer list.
                 */
                Audio::PtrList &audioList() { return _audioList; }

                /**
                 * @brief Returns a const reference to the list of compressed packets.
                 *
                 * Each entry is a @ref MediaPacket carrying one encoded
                 * access unit (typically one coded video frame or one
                 * encoded audio frame).  Empty by default — backends
                 * that only deal in uncompressed essence never touch
                 * this list.
                 *
                 * @return The packet pointer list.
                 */
                const MediaPacket::PtrList &packetList() const { return _packetList; }

                /**
                 * @brief Returns a mutable reference to the list of compressed packets.
                 * @return The packet pointer list.
                 */
                MediaPacket::PtrList &packetList() { return _packetList; }

                /**
                 * @brief Returns a const reference to the frame metadata.
                 * @return The metadata container.
                 */
                const Metadata &metadata() const { return _metadata; }

                /**
                 * @brief Returns a mutable reference to the frame metadata.
                 * @return The metadata container.
                 */
                Metadata &metadata() { return _metadata; }

                /**
                 * @brief Returns a const reference to the frame benchmark.
                 * @return The benchmark pointer (may be null if benchmarking is disabled).
                 */
                const Benchmark::Ptr &benchmark() const { return _benchmark; }

                /**
                 * @brief Returns a mutable reference to the frame benchmark.
                 * @return The benchmark pointer.
                 */
                Benchmark::Ptr &benchmark() { return _benchmark; }

                /**
                 * @brief Sets the frame benchmark.
                 * @param bm The benchmark to attach.
                 */
                void setBenchmark(Benchmark::Ptr bm) { _benchmark = std::move(bm); return; }

        private:
                Image::PtrList         _imageList;
                Audio::PtrList         _audioList;
                MediaPacket::PtrList   _packetList;
                Metadata               _metadata;
                Benchmark::Ptr         _benchmark;
};

PROMEKI_NAMESPACE_END
