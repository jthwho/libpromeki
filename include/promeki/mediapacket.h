/**
 * @file      mediapacket.h
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
#include <promeki/mediatimestamp.h>
#include <promeki/duration.h>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract polymorphic base for any encoded bitstream packet.
 * @ingroup proav
 *
 * A @ref MediaPacket represents one compressed access unit (AU) flowing
 * through a pipeline.  The common fields — a byte range on a shared
 * @ref Buffer, presentation / decode timestamps, nominal duration, and
 * a @ref Metadata container — apply to every kind of packet we model;
 * codec-identity, keyframe/parameter-set/etc. flags, sample counts, and
 * other per-family properties live on concrete subclasses such as
 * @ref VideoPacket, @ref AudioPacket, and future Ancillary/SDI/ST2110
 * packet types.
 *
 * @par Subclassing
 *
 * Subclasses use @ref PROMEKI_SHARED_DERIVED to participate in
 * @c SharedPtr-based ownership, override @ref kind() to identify
 * themselves for cheap dispatch without RTTI, and override
 * @ref clone() to return the correct derived type during
 * copy-on-write.  Concrete subclasses are *not* marked @c final —
 * further specialization (for example, @c H264VideoPacket or
 * @c MpegVideoPacket deriving from @ref VideoPacket) is an intentional
 * extension point.
 *
 * @par Stream-level metadata
 *
 * End-of-stream and corruption marking are carried in @ref metadata()
 * rather than as per-packet flags so that the same keys work uniformly
 * across packets, @ref Frame, @ref Image, and @ref Audio:
 *
 * - @c Metadata::EndOfStream (bool) — terminal packet in the stream.
 * - @c Metadata::Corrupt (bool) — payload is known to be corrupt.
 * - @c Metadata::CorruptReason (String) — human-readable explanation.
 *
 * Convenience accessors @ref isEndOfStream, @ref isCorrupt,
 * @ref corruptReason, @ref markEndOfStream, and @ref markCorrupt are
 * provided so callers don't need to remember the keys.
 *
 * @par Ownership
 *
 * MediaPacket uses the library's native shared-object pattern
 * (@ref PROMEKI_SHARED).  Callers manage lifetime via
 * @ref MediaPacket::Ptr .  Polymorphic construction goes through
 * @c takeOwnership:
 *
 * @code
 * MediaPacket::Ptr pkt = MediaPacket::Ptr::takeOwnership(
 *         new VideoPacket(view, PixelFormat::H264));
 * @endcode
 *
 * Concrete subclasses also expose their own @c Ptr alias and
 * @c ::Ptr::create helpers; the converting constructor on
 * @ref SharedPtr lets you hand a @c VideoPacket::Ptr into code that
 * accepts @c MediaPacket::Ptr without cloning.
 */
class MediaPacket {
        public:
                RefCount _promeki_refct;
                virtual MediaPacket *_promeki_clone() const = 0;

                /** @brief Shared-pointer alias for pipeline-wide packet handoff. */
                using Ptr = SharedPtr<MediaPacket, /*CopyOnWrite=*/true, MediaPacket>;

                /** @brief List of shared pointers to base @c MediaPacket instances. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Enumerates the concrete packet families the library models.
                 *
                 * Returned by @ref kind() for cheap type dispatch without
                 * RTTI.  Subclasses pick a value here and surface it so
                 * pipeline glue can @c switch on the kind before falling
                 * back to @c sharedPointerCast for field access.
                 */
                enum Kind {
                        Video,       ///< Encoded video access unit; see @ref VideoPacket.
                        Audio,       ///< Encoded audio access unit; see @ref AudioPacket.
                        Ancillary    ///< Reserved for future SDI / ST2110 / HDMI ancillary data.
                };

                /** @brief Constructs an empty packet (no payload, invalid timestamps). */
                MediaPacket() = default;

                /**
                 * @brief Constructs a packet that wraps an explicit view.
                 * @param view Byte range on a shared buffer; may be invalid.
                 */
                explicit MediaPacket(const BufferView &view) : _view(view) { }

                MediaPacket(const MediaPacket &) = default;
                MediaPacket(MediaPacket &&) = default;
                MediaPacket &operator=(const MediaPacket &) = default;
                MediaPacket &operator=(MediaPacket &&) = default;

                /** @brief Virtual destructor; subclasses manage their own fields. */
                virtual ~MediaPacket() = default;

                /** @brief Returns the concrete packet kind for dispatch. */
                virtual Kind kind() const = 0;

                /**
                 * @brief True when the packet has a payload worth dispatching.
                 *
                 * Subclasses may tighten this (e.g. @ref VideoPacket also
                 * checks that a pixel format is attached), but the base
                 * predicate is simply "has bytes."
                 */
                virtual bool isValid() const { return _view.isValid(); }

                // ---- Payload ------------------------------------------------

                /** @brief Returns the view onto the encoded bytes. */
                const BufferView &view() const { return _view; }

                /** @brief Returns a mutable reference to the view. */
                BufferView &view() { return _view; }

                /** @brief Replaces the view onto the encoded bytes. */
                void setView(const BufferView &v) { _view = v; }

                /** @brief Returns the underlying shared buffer (may be null). */
                const Buffer::Ptr &buffer() const { return _view.buffer(); }

                /**
                 * @brief Replaces the payload with a view spanning the whole given buffer.
                 *
                 * Shorthand for @c setView(BufferView(b, 0, b->size())) —
                 * the one-allocation-per-packet form.  Null @p b clears
                 * the payload.
                 */
                void setBuffer(Buffer::Ptr b) {
                        if(b) _view = BufferView(b, 0, b->size());
                        else  _view = BufferView();
                }

                /** @brief Returns the logical payload size in bytes. */
                size_t size() const { return _view.size(); }

                // ---- Timing -------------------------------------------------

                /** @brief Returns the presentation timestamp. */
                const MediaTimeStamp &pts() const { return _pts; }

                /** @brief Sets the presentation timestamp. */
                void setPts(const MediaTimeStamp &ts) { _pts = ts; }

                /**
                 * @brief Returns the decode timestamp.
                 *
                 * For codecs without B-frame reorder (audio, intra-only
                 * video, ancillary data) the DTS normally equals the PTS.
                 * B-frame-capable video codecs record the actual decode
                 * order here.
                 */
                const MediaTimeStamp &dts() const { return _dts; }

                /** @brief Sets the decode timestamp. */
                void setDts(const MediaTimeStamp &ts) { _dts = ts; }

                /** @brief Returns the nominal duration of the packet. */
                const Duration &duration() const { return _duration; }

                /** @brief Sets the nominal duration of the packet. */
                void setDuration(const Duration &d) { _duration = d; }

                // ---- Metadata ----------------------------------------------

                /** @brief Returns the metadata container. */
                const Metadata &metadata() const { return _metadata; }

                /** @brief Returns a mutable reference to the metadata container. */
                Metadata &metadata() { return _metadata; }

                // ---- Stream-level status (via metadata) --------------------

                /** @brief True when @c Metadata::EndOfStream is set on this packet. */
                bool isEndOfStream() const {
                        return _metadata.getAs<bool>(Metadata::EndOfStream);
                }

                /** @brief Sets @c Metadata::EndOfStream to @p v (default true). */
                void markEndOfStream(bool v = true) {
                        _metadata.set(Metadata::EndOfStream, v);
                }

                /** @brief True when @c Metadata::Corrupt is set on this packet. */
                bool isCorrupt() const {
                        return _metadata.getAs<bool>(Metadata::Corrupt);
                }

                /** @brief Returns the human-readable corruption reason, or an empty string. */
                String corruptReason() const {
                        return _metadata.getAs<String>(Metadata::CorruptReason);
                }

                /**
                 * @brief Marks the packet as corrupt with an optional explanation.
                 *
                 * Sets @c Metadata::Corrupt to @c true and — when
                 * @p reason is non-empty — @c Metadata::CorruptReason to
                 * the supplied string.
                 */
                void markCorrupt(const String &reason = String()) {
                        _metadata.set(Metadata::Corrupt, true);
                        if(!reason.isEmpty()) _metadata.set(Metadata::CorruptReason, reason);
                }

        private:
                BufferView      _view;
                MediaTimeStamp  _pts;
                MediaTimeStamp  _dts;
                Duration        _duration;
                Metadata        _metadata;
};

PROMEKI_NAMESPACE_END
