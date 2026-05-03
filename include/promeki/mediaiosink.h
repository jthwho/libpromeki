/**
 * @file      mediaiosink.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/framecount.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/metadata.h>
#include <promeki/mediaioport.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIO;
class MediaIOPortGroup;
class MediaIORequest;

/**
 * @brief Write-side port of a @ref MediaIO.
 * @ingroup mediaio_user
 *
 * A @ref MediaIOSink accepts frames into a @ref MediaIO instance.  It
 * carries the write API that used to live on @ref MediaIO directly —
 * the per-port @c writeFrame entry point, per-port format-negotiation
 * surface (@ref expectedDesc / @ref proposeInput), and the per-port
 * write-side signals (@c frameWantedSignal, @c writeErrorSignal).
 *
 * Sinks are created by the backend during open and parented to
 * their @ref MediaIOPortGroup, which is in turn parented to the
 * owning @ref MediaIO.  Per-type indexing is independent of source
 * ports — a single @ref MediaIO may simultaneously expose @c sink(0)
 * and @c source(0) as distinct ports.
 *
 * @par Default name
 * If the @p name argument is empty the constructor synthesizes
 * @c "sink{index}" (e.g. @c "sink0", @c "sink1") so every sink has a
 * stable, human-readable identifier even when the backend has nothing
 * specific to call it.
 *
 * @par Port groups
 * Sinks placed in a multi-sink @ref MediaIOPortGroup are dispatched in
 * lockstep: a sync-grouped write is held on the strand until every
 * sink in the group has a frame queued for the same group position,
 * then the group's frames go to the backend together as one
 * @ref MediaIOCommandWrite.  Sinks in single-port groups behave
 * independently.  Pending-write counting lives on the group, since a
 * single command advances the whole group at once;
 * @ref pendingWrites is a per-sink convenience that forwards to the
 * group's count.
 */
class MediaIOSink : public MediaIOPort {
                PROMEKI_OBJECT(MediaIOSink, MediaIOPort)
        public:
                /**
                 * @brief Constructs a sink port and binds it to @p group.
                 *
                 * @param group The port group this sink belongs to;
                 *              must be non-null.  The owning
                 *              @ref MediaIO is reached via
                 *              @c group->mediaIO().
                 * @param index Per-type sink index assigned by the
                 *              creating @ref MediaIO.
                 * @param name  Optional human-readable port name.  If
                 *              empty, defaults to @c "sink{index}".
                 */
                MediaIOSink(MediaIOPortGroup *group, int index, const String &name = String());

                /** @brief Destructor. */
                ~MediaIOSink() override;

                /** @brief Always returns @c MediaIOPort::Sink. */
                Role role() const override { return MediaIOPort::Sink; }

                // ---- Pre-open input expectations ----

                /**
                 * @brief Sets the @ref MediaDesc the caller intends to feed this sink.
                 *
                 * The hint is consumed by the backend's @c open()
                 * routine and lets it pre-size buffers, pick a default
                 * codec, etc.  Must be called before
                 * @ref MediaIO::open.
                 */
                Error setExpectedDesc(const MediaDesc &desc);

                /** @brief Returns the previously-set expected @ref MediaDesc. */
                const MediaDesc &expectedDesc() const { return _expectedMediaDesc; }

                /**
                 * @brief Sets the @ref AudioDesc the caller intends to feed this sink.
                 *
                 * Same semantics as @ref setExpectedDesc, for audio
                 * format hinting.
                 */
                Error setExpectedAudioDesc(const AudioDesc &desc);

                /** @brief Returns the previously-set expected @ref AudioDesc. */
                const AudioDesc &expectedAudioDesc() const { return _expectedAudioDesc; }

                /** @brief Sets pre-open container @ref Metadata for this sink. */
                Error setExpectedMetadata(const Metadata &meta);

                /** @brief Returns the previously-set @ref Metadata. */
                const Metadata &expectedMetadata() const { return _expectedMetadata; }

                /**
                 * @brief Negotiates an acceptable input format with the backend.
                 *
                 * Forwards to the backend task's @c proposeInput
                 * virtual.  The backend examines @p offered and
                 * returns @p preferred — the closest format it can
                 * accept directly without internal conversion.
                 *
                 * @param offered    The format the caller would like to feed.
                 * @param preferred  Output: the backend's preferred
                 *                   shape.  May be the same as
                 *                   @p offered when a direct match is
                 *                   available.  May be invalid when
                 *                   the backend has no preference.
                 * @return @c Error::Ok on success.
                 */
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const;

                // ---- Write API ----

                /**
                 * @brief Returns the maximum number of unwritten frames the sink will queue.
                 *
                 * The default is set by the backend during open and
                 * may be overridden per-instance via
                 * @ref setWriteDepth.
                 */
                int writeDepth() const { return _writeDepth; }

                /** @brief Overrides the per-sink write depth. */
                void setWriteDepth(int n);

                /**
                 * @brief Returns the number of write commands in flight.
                 *
                 * Forwards to the owning @ref MediaIOPortGroup's
                 * counter — pending-write accounting is per-group
                 * because a single backend tick advances the whole
                 * group at once.  In a single-sink group this is
                 * indistinguishable from a per-sink count.
                 */
                int pendingWrites() const;

                /**
                 * @brief Returns the number of additional writes the sink will accept now.
                 *
                 * Computed from @c writeDepth() minus the in-flight
                 * pending count and the backend task's own internal
                 * output queue depth.  Returns 0 when no further
                 * writes will be accepted without blocking.
                 */
                int writesAccepted() const;

                /**
                 * @brief Submits a frame to the sink.
                 *
                 * Builds a @ref MediaIOCommandWrite, dispatches it
                 * through the owning @ref MediaIO's submit gate, and
                 * returns a request the caller can @c .wait() on or
                 * attach a @c .then() continuation to.  Per the
                 * always-async API rule callers that want
                 * synchronous behavior write
                 * @c sink->writeFrame(f).wait().
                 *
                 * @par Capacity gate
                 * When @ref writesAccepted reports zero the request
                 * resolves immediately with @c Error::TryAgain
                 * without queueing anything.  Callers wait for the
                 * @c frameWantedSignal before retrying.
                 *
                 * @par Pre-open / closed
                 * Resolves with @c Error::NotOpen when the owning
                 * @ref MediaIO is not open or is in the closing
                 * state, or @c Error::Invalid when the sink is
                 * detached.
                 *
                 * @param frame The frame to write.  May be invalid
                 *              for flush-only commands.
                 * @return A request resolving with the per-write
                 *         outcome.  The same @c writeError signal as
                 *         before still fires for back-compat with
                 *         signal-driven consumers — request callers
                 *         can ignore it and read the resolution from
                 *         the request instead.
                 */
                MediaIORequest writeFrame(const Frame::Ptr &frame);

                // ---- Signals ----

                /** @brief Emitted when the backend is ready for another frame. @signal */
                PROMEKI_SIGNAL(frameWanted);

                /**
                 * @brief Emitted when an asynchronous write resolves with an error.
                 * @signal
                 *
                 * Per the always-async API, every @ref writeFrame
                 * returns immediately and the actual write runs on the
                 * strand worker thread.  Callers that hold the
                 * returned @ref MediaIORequest learn about errors
                 * directly from its resolution; this signal is the
                 * fan-out path for fire-and-forget consumers and for
                 * downstream observers ( @ref MediaIOPortConnection,
                 * pipeline status panes ) that don't track individual
                 * requests.
                 */
                PROMEKI_SIGNAL(writeError, Error);

        private:
                int        _writeDepth = 4;
                FrameCount _writeFrameCount;
                MediaDesc  _expectedMediaDesc;
                AudioDesc  _expectedAudioDesc;
                Metadata   _expectedMetadata;
};

PROMEKI_NAMESPACE_END
