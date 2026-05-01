/**
 * @file      mediaioportgroup.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/atomic.h>
#include <promeki/error.h>
#include <promeki/framenumber.h>
#include <promeki/framecount.h>
#include <promeki/framerate.h>
#include <promeki/ratetracker.h>
#include <promeki/timestamp.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/clock.h>
#include <promeki/mediaio.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIO;
class MediaIOPort;
class MediaIORequest;

/**
 * @brief Grouping of @ref MediaIOPort siblings sharing a clock and step.
 * @ingroup mediaio_user
 *
 * Every @ref MediaIOPort on a @ref MediaIO belongs to exactly one
 * @ref MediaIOPortGroup.  A group binds together the ports that share
 * a single timing reference and advance in lockstep:
 *
 *  - Ports in a multi-port group are synchronized.  Writes on a
 *    multi-sink group are held until every sink has a frame queued
 *    for the same group position, then dispatched as a single
 *    backend @ref MediaIOCommandWrite.
 *  - Ports in a single-port group are independent — their group is
 *    just a holder for that port's clock and step state.
 *
 * @par Per-source reads (Phase 1)
 * Today @ref MediaIOCommandRead carries a single @c Frame::Ptr and
 * targets a specific source within the group.  The multi-source
 * "atomic distribute one frame per source" path is reserved for a
 * later phase — this page documents the per-source shape that ships.
 *
 * The @ref step value, current frame number, frame count, seekability,
 * and seek dispatch all live on the group, since by definition every
 * port in the group shares them.  The group also owns the @ref Clock
 * for that timing reference; ports access the clock through their
 * group rather than minting a fresh one per port.
 *
 * @par Unit of accounting
 * The group is also the unit of accounting — rate tracking, dropped /
 * repeated / late frame counts, and pending command counts all live
 * here rather than on individual ports.  A single backend
 * @ref MediaIOCommandRead or @ref MediaIOCommandWrite advances the
 * whole group at once, so a "tick" is naturally per group.  Reporting
 * one dropped frame on a paired audio + video group correctly counts
 * one missed tick rather than inflating the metric to two.
 *
 * @par Lifetime
 * Port groups are created by the backend during open and parented
 * to the owning @ref MediaIO via @ref ObjectBase parenting.  Each port
 * is in turn parented to its group, so destruction cascades MediaIO →
 * PortGroup → Port — destroying a group destroys all of its member
 * ports, and destroying the @ref MediaIO destroys every group and
 * every port.  A group must not outlive its @ref MediaIO.
 *
 * @par Phase 1 scope
 * This is a structural scaffold.  Per-position state members (step,
 * current frame, frame count, canSeek, seek dispatch, full Clock
 * wiring) are added as Phase 3 / Phase 4 of the multi-port refactor
 * migrate them off @ref MediaIO.
 */
class MediaIOPortGroup : public ObjectBase {
                PROMEKI_OBJECT(MediaIOPortGroup, ObjectBase)
                friend class MediaIO;
                friend class MediaIOSink;
                friend class MediaIOSource;
                friend class MediaIOReadCache;
                friend class CommandMediaIO;
        public:
                /** @brief List of port references owned by the group. */
                using PortList = promeki::List<MediaIOPort *>;

                /**
                 * @brief Constructs a port group owned by @p mediaIO.
                 *
                 * The group is parented to @p mediaIO via @ref ObjectBase
                 * parenting and is therefore auto-destroyed when
                 * @p mediaIO is destroyed.  A group must always have a
                 * non-null @ref Clock — paired ports advance against a
                 * single timing reference by definition, so there is
                 * no meaningful "no clock" state.  Backends that don't
                 * have a hardware clock typically pass a
                 * @ref MediaIOClock; the @ref MediaIO::addPortGroup
                 * helper handles that case implicitly.
                 *
                 * @param mediaIO The owning @ref MediaIO; must be non-null.
                 * @param name    Human-readable group name (e.g.
                 *                @c "av" for a paired audio + video
                 *                group, @c "main" for a single-port
                 *                independent group).  May be empty.
                 * @param clock   The group's timing reference; must be
                 *                non-null at construction.
                 */
                MediaIOPortGroup(MediaIO *mediaIO, const String &name, const Clock::Ptr &clock);

                /** @brief Destructor. */
                ~MediaIOPortGroup() override;

                /** @brief Returns the @ref MediaIO that owns this group. */
                MediaIO *mediaIO() const { return _mediaIO; }

                /** @brief Returns the human-readable group name. */
                const String &name() const { return _name; }

                /**
                 * @brief Returns the list of ports in the group.
                 *
                 * Ports are added by @ref MediaIO helpers when they
                 * create sources or sinks bound to this group.  The
                 * order matches the order ports were added.
                 */
                const PortList &ports() const { return _ports; }

                /**
                 * @brief Adds a port to the group.
                 *
                 * Intended for use by @ref MediaIO during port
                 * creation; not meant to be called after the group is
                 * in use.  Does not write @p port's group back-pointer —
                 * the caller is responsible for both sides of the
                 * wiring (typically @ref MediaIOPort already records
                 * the pointer in its constructor).
                 */
                void addPort(MediaIOPort *port);

                /**
                 * @brief Returns the group's @ref Clock.
                 *
                 * Always non-null for a constructed group — the
                 * timing reference is required at construction.  Every
                 * port in the group advances against this clock.
                 */
                const Clock::Ptr &clock() const { return _clock; }

                /**
                 * @brief Returns the number of write commands in flight on the group.
                 *
                 * A @ref MediaIOCommandWrite for any sink in the group
                 * is held by the strand until every sink in the group
                 * has a frame queued for the same group position, then
                 * dispatched as a single backend call.  This counter
                 * tracks commands that have been queued or are
                 * currently executing.  Single-sink groups behave the
                 * same as a per-sink count.
                 */
                int pendingWrites() const { return _pendingWriteCount.value(); }

                /**
                 * @brief Returns the number of read commands in flight on the group.
                 *
                 * Pending-read accounting is per-group so paired
                 * audio + video (or other multi-source) groups roll up
                 * to one tick per group.  Single-source groups behave
                 * the same as a per-source count.
                 */
                int pendingReads() const { return _pendingReadCount.value(); }

                /**
                 * @brief Returns the current frame number on the group.
                 *
                 * Updated by the strand worker after each successful
                 * @ref MediaIOCommandRead.  All sources in a multi-source
                 * group share this position; single-port groups own
                 * their own.
                 */
                FrameNumber currentFrame() const { return _currentFrame; }

                /**
                 * @brief Returns the per-group step value.
                 *
                 * Step is the per-group playback speed / direction:
                 * 1 for forward, 0 for hold, -1 for reverse, larger
                 * magnitudes for fast-forward / rewind.
                 */
                int step() const { return _step; }

                /**
                 * @brief Sets the group's step.
                 *
                 * Outstanding prefetched reads were submitted with
                 * the old step; they're stale relative to the new
                 * direction and are cancelled.  Also clears the EOF
                 * latch — the new direction may make more frames
                 * available (e.g. flipping forward-EOF to reverse).
                 */
                void setStep(int val);

                /**
                 * @brief Seeks every source in the group to @p frameNumber.
                 *
                 * Cancels any pending strand work and discards
                 * prefetched read results (they're stale relative to
                 * the new position), then dispatches a CmdSeek through
                 * @ref MediaIO::submit and returns the resulting
                 * @ref MediaIORequest.  @c MediaIO::SeekDefault is
                 * resolved to the task's preferred mode before
                 * dispatch.  Per the always-async API rule, callers
                 * that want synchronous behavior write
                 * @c group->seekToFrame(n).wait().
                 *
                 * @par Pre-open / non-seekable
                 * Resolves with @c Error::NotOpen when the owning
                 * @ref MediaIO is not open or is closing, or
                 * @c Error::IllegalSeek when the group reports
                 * @ref canSeek as @c false.
                 *
                 * @param frameNumber The zero-based frame number.
                 * @param mode        How to interpret the seek target.
                 */
                MediaIORequest seekToFrame(const FrameNumber &frameNumber,
                                           MediaIOSeekMode  mode = MediaIO_SeekDefault);

                /** @brief Returns the cached frame count for the group. */
                FrameCount frameCount() const { return _frameCount; }

                /** @brief True if the group's underlying stream is seekable. */
                bool canSeek() const { return _canSeek; }

                /** @brief Sets the group's seekability flag. */
                void setCanSeek(bool val) { _canSeek = val; }

                /** @brief Sets the group's frame count. */
                void setFrameCount(const FrameCount &val) { _frameCount = val; }

                /** @brief True once the group has reached end-of-stream. */
                bool atEnd() const { return _atEnd; }

                /** @brief Returns the group's frame rate. */
                const FrameRate &frameRate() const { return _frameRate; }

                /** @brief Sets the group's frame rate. */
                void setFrameRate(const FrameRate &val) { _frameRate = val; }

                /** @brief Returns the group's session origin timestamp. */
                const TimeStamp &originTime() const { return _originTime; }

                /** @brief Sets the group's session origin timestamp. */
                void setOriginTime(const TimeStamp &val) { _originTime = val; }

                // ---- Per-group accounting ----
                //
                // A single backend tick (one CmdRead or CmdWrite)
                // advances the whole group, regardless of how many
                // ports it has — so the rate tracker, dropped /
                // repeated / late counters, and pending-command
                // counts all live here.  Reporting one dropped frame
                // on a paired audio + video group correctly counts
                // one missed tick rather than inflating the metric
                // to two.

                /** @brief Returns measured bytes per second across this group. */
                int64_t bytesPerSecond() const { return _rateTracker.bytesPerSecond(); }

                /** @brief Returns measured frames per second across this group. */
                double framesPerSecond() const { return _rateTracker.framesPerSecond(); }

                /** @brief Returns the lifetime dropped-frame count. */
                int64_t framesDroppedTotal() const { return _framesDroppedTotal.value(); }

                /** @brief Returns the lifetime repeated-frame count. */
                int64_t framesRepeatedTotal() const { return _framesRepeatedTotal.value(); }

                /** @brief Returns the lifetime late-frame count. */
                int64_t framesLateTotal() const { return _framesLateTotal.value(); }

        private:
                MediaIO          *_mediaIO = nullptr;
                String            _name;
                PortList          _ports;
                Clock::Ptr        _clock;
                Atomic<int>       _pendingWriteCount;
                Atomic<int>       _pendingReadCount;
                FrameNumber       _currentFrame;
                FrameCount        _frameCount;
                FrameRate         _frameRate;
                TimeStamp         _originTime;
                RateTracker       _rateTracker;
                Atomic<int64_t>   _framesDroppedTotal{0};
                Atomic<int64_t>   _framesRepeatedTotal{0};
                Atomic<int64_t>   _framesLateTotal{0};
                int               _step = 1;
                bool              _canSeek = false;
                bool              _atEnd = false;
};

PROMEKI_NAMESPACE_END
