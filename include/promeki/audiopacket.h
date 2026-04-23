/**
 * @file      audiopacket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/audiocodec.h>
#include <promeki/mediapacket.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Encoded audio access unit produced by an @ref AudioEncoder or
 *        consumed by an @ref AudioDecoder.
 * @ingroup proav
 *
 * AudioPacket specializes @ref MediaPacket for compressed audio access
 * units.  For block-based audio codecs (Opus, AAC, MP3, FLAC) each
 * packet holds exactly one encoded audio frame whose decoded length is
 * recorded in the base's @ref duration.  The payload lives in the
 * base's @ref BufferView; multiple AudioPackets may share a single
 * backing buffer when the encoder emits them concatenated.
 *
 * @par Codec identity
 *
 * The @ref AudioCodec wrapper (e.g. @c AudioCodec::Opus,
 * @c AudioCodec::AAC) is attached to each packet so downstream
 * dispatch doesn't have to consult the emitting encoder.  This is the
 * audio counterpart to @ref VideoPacket::pixelFormat.
 *
 * @par Flags
 *
 * Audio packets do not carry a per-packet flag mask by default.
 * Traditional video concepts (keyframe / parameter-set / discardable)
 * don't translate cleanly to most audio codecs — codecs that need such
 * per-packet state (FLAC sync points, Opus DTX boundaries) should
 * introduce codec-specific flags on a further subclass rather than
 * shoehorning them into a base vocabulary.  Stream-level status —
 * end-of-stream, corruption — lives on the base's metadata.
 *
 * @par Extension
 *
 * AudioPacket is intentionally not @c final.  Codec-specific
 * subclasses may derive from it to carry container-specific framing
 * (for example, Ogg page boundaries for Vorbis) or in-band side data.
 *
 * @par Example — single allocation
 * @code
 * AudioPacket::Ptr pkt = AudioPacket::Ptr::create(
 *         encodedBuffer, AudioCodec(AudioCodec::Opus));
 * pkt.modify()->setPts(pts);
 * pkt.modify()->setDuration(Duration::fromSamples(960, 48000));
 * @endcode
 */
class AudioPacket : public MediaPacket {
        public:
                virtual AudioPacket *_promeki_clone() const override {
                        return new AudioPacket(*this);
                }

                /** @brief Shared-pointer alias for AudioPacket ownership. */
                using Ptr = SharedPtr<AudioPacket, /*CopyOnWrite=*/true, AudioPacket>;

                /** @brief List of shared pointers to AudioPacket instances. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an empty, invalid audio packet. */
                AudioPacket() = default;

                /**
                 * @brief Constructs a packet from a pre-built view and a codec identity.
                 */
                AudioPacket(const BufferView &view, const AudioCodec &audioCodec) :
                        MediaPacket(view), _audioCodec(audioCodec) { }

                /**
                 * @brief Constructs a packet that owns a whole buffer as its payload.
                 */
                AudioPacket(Buffer::Ptr buffer, const AudioCodec &audioCodec) :
                        MediaPacket(buffer ? BufferView(buffer, 0, buffer->size())
                                           : BufferView()),
                        _audioCodec(audioCodec) { }

                Kind kind() const override { return Audio; }

                /** @brief True when the payload and audio codec identity are both set. */
                bool isValid() const override {
                        return MediaPacket::isValid() && _audioCodec.isValid();
                }

                /** @brief Returns the audio codec identity. */
                const AudioCodec &audioCodec() const { return _audioCodec; }

                /** @brief Sets the audio codec identity. */
                void setAudioCodec(const AudioCodec &ac) { _audioCodec = ac; }

                AudioPacket(const AudioPacket &) = default;
                AudioPacket(AudioPacket &&) = default;
                AudioPacket &operator=(const AudioPacket &) = default;
                AudioPacket &operator=(AudioPacket &&) = default;

        private:
                AudioCodec _audioCodec;
};

PROMEKI_NAMESPACE_END
