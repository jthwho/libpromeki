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
#include <promeki/pixeldesc.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIODescription;
class MediaDesc;

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

                /**
                 * @brief Applies the standard @c Output* overrides from
                 *        @p config to @p input, returning the resulting
                 *        @ref MediaDesc.
                 *
                 * Every transform backend (CSC, FrameSync, SRC,
                 * VideoEncoder, VideoDecoder) consumes the same family
                 * of @ref MediaConfig keys to express "given this input
                 * shape, produce this output":
                 *
                 *  - @ref MediaConfig::OutputPixelDesc      — replaces the
                 *    @ref ImageDesc::pixelDesc of every video image.
                 *  - @ref MediaConfig::OutputFrameRate      — replaces
                 *    @ref MediaDesc::frameRate.
                 *  - @ref MediaConfig::OutputAudioRate      — replaces
                 *    @ref AudioDesc::sampleRate on every audio entry.
                 *  - @ref MediaConfig::OutputAudioChannels  — replaces
                 *    @ref AudioDesc::channels on every audio entry.
                 *  - @ref MediaConfig::OutputAudioDataType  — replaces
                 *    @ref AudioDesc::dataType on every audio entry.
                 *
                 * Keys at their default (invalid PixelDesc, invalid
                 * FrameRate, zero audio rate / channels, invalid Enum)
                 * mean "inherit from input" — the corresponding field
                 * passes through unchanged.
                 *
                 * The pipeline planner uses this helper to compute
                 * what shape a configured transform will produce
                 * without instantiating it.  Backend authors call it
                 * from @ref MediaIOTask::proposeOutput / @ref MediaIOTask::describe so every
                 * transform answers the planner uniformly.
                 *
                 * @param input  The MediaDesc the transform will consume.
                 * @param config The transform's MediaConfig (read-only).
                 * @return The MediaDesc the transform will produce.
                 */
                static MediaDesc applyOutputOverrides(const MediaDesc &input,
                                                     const MediaConfig &config);

                /**
                 * @brief Returns a canonical uncompressed PixelDesc that
                 *        stays in the same family as @p source.
                 *
                 * Transform backends that refuse compressed or
                 * paint-engine-less input (Burn, FrameSync, ...) use
                 * this to ask the pipeline planner for a same-family
                 * uncompressed substitute in @c proposeInput.  The
                 * planner's VideoDecoder / CSC bridges then close the
                 * gap.  The helper keeps the mapping in one place so
                 * the fallback does not drift between backends.
                 *
                 *  - YCbCr sources → @ref PixelDesc::YUV8_422_Rec709
                 *  - RGB / other → @ref PixelDesc::RGBA8_sRGB
                 *
                 * @param source The compressed or paint-engine-less
                 *               input PixelDesc.
                 * @return A canonical uncompressed PixelDesc.
                 */
                static PixelDesc defaultUncompressedPixelDesc(const PixelDesc &source);

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
                 * @brief Requests that any in-flight blocking command unwind.
                 *
                 * Called by @ref MediaIO::close on the caller's thread
                 * (not the strand) before the Close command is
                 * submitted.  Backends whose @c executeCmd can block
                 * waiting on external signals — for instance the
                 * @c FrameBridge publisher waiting for a consumer or
                 * sync ACK — should override this to prod that wait
                 * loose (typically by setting a thread-safe atomic
                 * checked inside the blocking call) so the queued
                 * Close isn't stuck behind a permanently-blocked
                 * command.
                 *
                 * Must be thread-safe with respect to whatever thread
                 * is running @c executeCmd at the moment.  The default
                 * implementation is a no-op.
                 */
                virtual void cancelBlockingWork();

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

                /**
                 * @brief Populates @p out with backend-specific
                 *        introspection details.
                 *
                 * MediaIO calls this from @ref MediaIO::describe after
                 * filling in identity (backend name, instance name,
                 * UUID, role flags) from @ref MediaIO::FormatDesc and any
                 * cached state.  The task supplements with format
                 * landscape (@c producibleFormats / @c acceptableFormats /
                 * @c preferredFormat), pre-open capabilities
                 * (@c canSeek, @c frameCount, @c frameRate,
                 * @c containerMetadata) probed from the underlying
                 * resource, and a probe diagnostic on failure.
                 *
                 * Synchronous and cheap by contract — backends should
                 * use the most efficient probe available (file header
                 * peek, OS device-enum, config-driven derivation).
                 * Default: no-op (fields previously populated by
                 * MediaIO are left untouched).
                 *
                 * @param out Pre-populated description to supplement.
                 * @return @c Error::Ok on success, or a probe error.
                 *         The probe error is also stamped into
                 *         @p out's @c probeStatus.
                 */
                virtual Error describe(MediaIODescription *out) const;

                /**
                 * @brief Reports what this task wants to receive given
                 *        an offered MediaDesc.
                 *
                 * MediaIO's pipeline planner calls this on every sink
                 * and transform to determine whether the upstream
                 * source's MediaDesc is directly consumable.  Three
                 * outcomes:
                 *  - @c Error::Ok with @c *preferred == @p offered
                 *    means accept-as-is.
                 *  - @c Error::Ok with @c *preferred != @p offered
                 *    means the planner should bridge from @p offered
                 *    to @c *preferred before delivering.
                 *  - @c Error::NotSupported means the task cannot
                 *    consume @p offered at all.
                 *
                 * Default implementation accepts anything (transparent
                 * passthrough).  Sinks and transforms with format
                 * constraints override.
                 *
                 * @param offered   The MediaDesc the planner would route in.
                 * @param preferred Receives the desc the task actually wants.
                 * @return @c Error::Ok or @c Error::NotSupported.
                 */
                virtual Error proposeInput(const MediaDesc &offered,
                                           MediaDesc *preferred) const;

                /**
                 * @brief Reports what this task can produce given a
                 *        requested MediaDesc.
                 *
                 * MediaIO's pipeline planner calls this on sources
                 * and transforms to see whether the source can be
                 * re-configured to produce a sink's preferred shape
                 * directly (cheaper than inserting a bridge).  Three
                 * outcomes:
                 *  - @c Error::Ok with @c *achievable == @p requested
                 *    means the source will produce exactly that.
                 *  - @c Error::Ok with @c *achievable != @p requested
                 *    means the source will produce something close —
                 *    the planner can compare and decide whether to
                 *    accept or bridge.
                 *  - @c Error::NotSupported means the source has no
                 *    flexibility — the planner should ignore the
                 *    request and bridge from the current output.
                 *
                 * Default implementation ignores @p requested and
                 * returns @c Error::NotSupported — most sources
                 * produce what they produce.  Configurable sources
                 * (TPG, V4L2) override.
                 *
                 * @param requested  The MediaDesc the planner would prefer.
                 * @param achievable Receives the desc the task can produce.
                 * @return @c Error::Ok or @c Error::NotSupported.
                 */
                virtual Error proposeOutput(const MediaDesc &requested,
                                            MediaDesc *achievable) const;
};

PROMEKI_NAMESPACE_END
