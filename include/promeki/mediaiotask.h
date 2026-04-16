/**
 * @file      mediaiotask.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/mediaio.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Backend interface for media I/O.
 * @ingroup proav
 *
 * MediaIOTask is the abstract base class for all MediaIO backends
 * (TPG, ImageFile, AudioFile, etc.).  It is *only* driven by MediaIO
 * — backends should never be used directly.  All interaction is
 * through commands processed on a worker thread.
 *
 * Backends override the private executeCmd() virtuals.  All inputs
 * and outputs flow through the command struct, so the task does not
 * need to maintain any state visible to MediaIO — only its own
 * internal resources (file handles, generators, etc.).
 *
 * @par Friendship
 *
 * MediaIO is `friend`ed so it can call the private virtuals.  In C++,
 * derived classes can override private virtual functions even though
 * they cannot call them through the base.  This NVI-style pattern
 * enforces the contract that backends are only invoked through MediaIO.
 *
 * @par Lifetime
 *
 * The task instance is owned by MediaIO.  MediaIO calls close()
 * before destroying the task.  Task destructors should release their
 * own internal resources but should NOT attempt to close() — that is
 * MediaIO's responsibility.
 *
 * @par Open-failure cleanup contract
 *
 * If @c executeCmd(MediaIOCommandOpen &) returns an error, MediaIO
 * will automatically dispatch @c executeCmd(MediaIOCommandClose &) on
 * the same task instance to give it a chance to release any
 * resources it allocated before the failure.  Backends MUST handle
 * being closed from a failed-open state without crashing — typically
 * by checking whether each resource is valid before releasing it.
 * The same applies to a normal Close after a successful open.
 *
 * @par Parameterized commands
 *
 * Backends can expose operations beyond the standard set by
 * overriding @c executeCmd(MediaIOCommandParams &).  Dispatch on
 * @c cmd.name and read @c cmd.params; populate @c cmd.result for
 * the caller.  Return @c Error::NotSupported for unrecognized names.
 */
class MediaIOTask {
        friend class MediaIO;
        public:
                /** @brief Constructs a MediaIOTask. */
                MediaIOTask() = default;

                /** @brief Virtual destructor for polymorphic ownership. */
                virtual ~MediaIOTask();

                MediaIOTask(const MediaIOTask &) = delete;
                MediaIOTask &operator=(const MediaIOTask &) = delete;
                MediaIOTask(MediaIOTask &&) = delete;
                MediaIOTask &operator=(MediaIOTask &&) = delete;

        protected:
                // ---- Live-telemetry helpers ----
                //
                // Backends use these to report exception-path events
                // that the MediaIO base class cannot observe on its
                // own.  Happy-path BytesPerSecond / FramesPerSecond
                // are already tracked automatically when an
                // executeCmd() call produces or consumes a valid
                // frame; these helpers exist for counters the base
                // path cannot infer — drops, repeats, and late
                // arrivals.  They are safe to call from the backend
                // worker thread and are cheap (one atomic increment
                // each).  No-op when the task has not been attached
                // to a MediaIO yet (defensive).

                /** @brief Increments the lifetime dropped-frame counter. */
                void noteFrameDropped();

                /** @brief Increments the lifetime repeated-frame counter. */
                void noteFrameRepeated();

                /** @brief Increments the lifetime late-frame counter. */
                void noteFrameLate();

                /**
                 * @brief Stamps the work-begin benchmark on the active frame.
                 *
                 * Call this at the point where the task begins its real
                 * per-frame processing work (after any pacing or
                 * throttling).  Paired with @c stampWorkEnd(), this
                 * brackets the actual processing cost so the framework
                 * can report it separately from end-to-end latency.
                 *
                 * The infrastructure sets the active benchmark pointer
                 * before calling @c executeCmd() and clears it after,
                 * so this works for both reads and writes with no
                 * arguments needed.  No-op when benchmarking is
                 * disabled or the task is unattached.
                 */
                void stampWorkBegin();

                /**
                 * @brief Stamps the work-end benchmark on the active frame.
                 *
                 * Call this when the task's per-frame processing is
                 * complete.  See @c stampWorkBegin() for details.
                 */
                void stampWorkEnd();

                /**
                 * @brief Returns the MediaIO that owns this task.
                 *
                 * Set by MediaIO immediately after adopting the task
                 * from a factory or @c adoptTask().  Null while the
                 * task is unattached (e.g. inside the factory, before
                 * assignment).
                 *
                 * @return The owning MediaIO, or nullptr.
                 */
                MediaIO *mediaIo() const { return _owner; }

        private:
                MediaIO     *_owner = nullptr;
                Benchmark   *_activeBenchmark = nullptr;

                /**
                 * @brief Handles a CmdOpen command.
                 *
                 * Reads inputs from cmd.mode, cmd.config, cmd.pendingMediaDesc,
                 * cmd.pendingMetadata, cmd.pendingAudioDesc.  Fills outputs
                 * cmd.mediaDesc, cmd.audioDesc, cmd.metadata, cmd.frameRate,
                 * cmd.canSeek, cmd.frameCount on success.
                 *
                 * @param cmd The open command.
                 * @return Error::Ok on success, or an error.
                 */
                virtual Error executeCmd(MediaIOCommandOpen &cmd);

                /**
                 * @brief Handles a CmdClose command.
                 *
                 * Releases the backend's open resources.  The default
                 * returns Error::Ok.
                 *
                 * @param cmd The close command.
                 * @return Error::Ok on success.
                 */
                virtual Error executeCmd(MediaIOCommandClose &cmd);

                /**
                 * @brief Handles a CmdRead command.
                 *
                 * Reads cmd.step for the step increment.  On success,
                 * sets cmd.frame and cmd.currentFrame.
                 *
                 * @param cmd The read command.
                 * @return Error::Ok, Error::EndOfFile, or an error.
                 */
                virtual Error executeCmd(MediaIOCommandRead &cmd);

                /**
                 * @brief Handles a CmdWrite command.
                 *
                 * Reads cmd.frame as input.  On success, sets
                 * cmd.currentFrame and cmd.frameCount.
                 *
                 * @param cmd The write command.
                 * @return Error::Ok or an error.
                 */
                virtual Error executeCmd(MediaIOCommandWrite &cmd);

                /**
                 * @brief Handles a CmdSeek command.
                 *
                 * Reads cmd.frameNumber as the target.  On success,
                 * sets cmd.currentFrame.
                 *
                 * @param cmd The seek command.
                 * @return Error::Ok or Error::IllegalSeek.
                 */
                virtual Error executeCmd(MediaIOCommandSeek &cmd);

                /**
                 * @brief Handles a backend-specific parameterized command.
                 *
                 * Dispatch on @c cmd.name and read @c cmd.params;
                 * populate @c cmd.result on success.  The default
                 * implementation returns @c Error::NotSupported.
                 *
                 * @param cmd The parameterized command.
                 * @return Error::Ok on success, NotSupported for
                 *         unrecognized names, or another error.
                 */
                virtual Error executeCmd(MediaIOCommandParams &cmd);

                /**
                 * @brief Handles a stats query.
                 *
                 * Populate @c cmd.stats with whatever runtime metrics
                 * the backend tracks.  See @c MediaIOStats for the
                 * standard key conventions.  The default
                 * implementation returns @c Error::Ok with an empty
                 * stats config.
                 *
                 * @param cmd The stats command.
                 * @return Error::Ok on success.
                 */
                virtual Error executeCmd(MediaIOCommandStats &cmd);

                /**
                 * @brief Called when a write command carries a non-empty
                 *        @ref Frame::configUpdate delta.
                 *
                 * Invoked on the worker thread immediately before
                 * @c executeCmd(MediaIOCommandWrite &) for the same
                 * frame, so the backend sees the new parameters before
                 * the frame is processed.  The default implementation
                 * is a no-op — backends that support dynamic
                 * reconfiguration (e.g. bitrate changes on an encoder)
                 * override this to merge @p delta into their running
                 * config and adjust internal state.
                 *
                 * @param delta Only the keys that changed.
                 */
                virtual void configChanged(const MediaConfig &delta);

                /**
                 * @brief Returns the number of frames the task is
                 *        holding internally beyond what
                 *        @c pendingWrites tracks.
                 *
                 * The base MediaIO uses this in @c writesAccepted() to
                 * account for frames that have been processed by the
                 * write side but not yet consumed by the read side
                 * (e.g. a converter's output FIFO).  The default
                 * returns 0 (no internal buffering).
                 *
                 * Called from the main thread — implementations must
                 * be safe against concurrent strand activity.
                 *
                 * @return Buffered frame count (≥ 0).
                 */
                virtual int pendingOutput() const;
};

PROMEKI_NAMESPACE_END
