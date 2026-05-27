/**
 * @file      commandmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/clock.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIOPortGroup;
class MediaIOSource;
class MediaIOSink;

/**
 * @brief Abstract intermediate that owns the per-command dispatch
 *        contract for backends that work in terms of @c executeCmd hooks.
 * @ingroup mediaio_backend
 *
 * @ref CommandMediaIO sits between @ref MediaIO (which owns the public
 * always-async API plus cached state and ports) and the strategy
 * subclasses (@ref InlineMediaIO, @ref SharedThreadMediaIO,
 * @ref DedicatedThreadMediaIO).  Backend authors override the
 * @c executeCmd virtuals here; strategy classes override
 * @ref MediaIO::submit to drive @ref dispatch on whichever thread they
 * choose.
 *
 * @par Open-failure cleanup contract
 * If @c executeCmd(MediaIOCommandOpen) returns non-OK, @ref dispatch
 * automatically runs @c executeCmd(MediaIOCommandClose) on the same
 * instance before returning.  Backends MUST tolerate being closed
 * from a partially-opened state without crashing — typically by
 * checking whether each resource is valid before releasing it.  The
 * same applies to a normal Close after a successful open.
 *
 * @par Re-entrancy
 * Backends MUST NOT call @ref MediaIO::submit on @c this from inside
 * their own @c executeCmd; doing so would deadlock the
 * @ref SharedThreadMediaIO strand or serialize behind itself on the
 * @ref DedicatedThreadMediaIO worker.  Backends that need to chain
 * operations expose the chain at the public API level.
 *
 * @par Port-construction helpers
 * @ref addPortGroup, @ref addSource, and @ref addSink are protected
 * helpers backends call from @c executeCmd(MediaIOCommandOpen) to
 * declare the ports they expose.  Storage lives on @ref MediaIO; the
 * helpers update those containers via friend access.
 */
class CommandMediaIO : public MediaIO {
                PROMEKI_OBJECT(CommandMediaIO, MediaIO)
        public:
                /** @brief Constructs a CommandMediaIO with optional parent. */
                CommandMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~CommandMediaIO() override;

        protected:
                /**
                 * @brief Visits @p cmd by type and routes to the
                 *        matching @c executeCmd overload.
                 *
                 * Strategy classes call this from inside their
                 * @ref MediaIO::submit override (after queue-wait
                 * accounting, before @ref MediaIO::completeCommand).
                 * The returned @ref Error is the result of the
                 * backend's @c executeCmd; the caller writes it into
                 * @ref MediaIOCommand::result before invoking
                 * @ref MediaIO::completeCommand.
                 *
                 * Implements the open-failure cleanup contract: if
                 * the @c executeCmd(Open) result is non-OK, runs
                 * @c executeCmd(Close) on the same instance before
                 * returning the original open error.  Backends never
                 * see the cleanup close as a separate event because
                 * @ref MediaIO::completeCommand only sees the open
                 * cmd's eventual result.
                 *
                 * Also applies the per-frame @ref Frame::configUpdate
                 * delta on Write commands by calling @ref configChanged
                 * before @c executeCmd(Write).
                 */
                Error dispatch(MediaIOCommand::Ptr cmd);

                /**
                 * @brief Handles a CmdOpen command.
                 *
                 * Default implementation returns @c Error::NotImplemented.
                 * Backends that support opening override and populate
                 * the cmd's Output fields.
                 */
                virtual Error executeCmd(MediaIOCommandOpen &cmd);

                /**
                 * @brief Handles a CmdClose command.
                 *
                 * Default returns @c Error::Ok.  Backends with open
                 * resources override to release them; must tolerate
                 * being called from a partially-opened state per the
                 * open-failure cleanup contract.
                 */
                virtual Error executeCmd(MediaIOCommandClose &cmd);

                /**
                 * @brief Handles a CmdRead command.
                 *
                 * Default returns @c Error::NotSupported.  Source-capable
                 * backends override to populate @c cmd.frame and
                 * @c cmd.currentFrame.
                 */
                virtual Error executeCmd(MediaIOCommandRead &cmd);

                /**
                 * @brief Handles a CmdWrite command.
                 *
                 * Default returns @c Error::NotSupported.  Sink-capable
                 * backends override to consume @c cmd.frame.
                 */
                virtual Error executeCmd(MediaIOCommandWrite &cmd);

                /**
                 * @brief Handles a CmdSeek command.
                 *
                 * Default returns @c Error::IllegalSeek.  Seekable
                 * backends override to honor @c cmd.frameNumber.
                 */
                virtual Error executeCmd(MediaIOCommandSeek &cmd);

                /**
                 * @brief Runs a parameterized get/set block.
                 *
                 * The framework owns this loop — backends normally do
                 * @em not override it.  It walks @c cmd.block and, in
                 * list order, dispatches each action to @ref getParam
                 * (for @c Get) or @ref setParam (for @c Set), recording
                 * every action's outcome back into the block.
                 *
                 * Every @c Set is validated via @ref validateParam before
                 * it is applied.  When @c cmd.block is atomic
                 * (@ref MediaIOParams::isAtomic) the whole block is
                 * validated up front; if any validation fails nothing is
                 * applied, the offending action carries its error, and the
                 * rest carry @c Error::TransactionAborted.  A @ref setParam
                 * failure during the apply pass triggers a best-effort
                 * rollback of the @c Set actions already committed
                 * (re-applying their prior values).  When the block is not
                 * atomic each @c Set is validated inline in list order; a
                 * rejection records the validation error on that action and
                 * skips its write, leaving the other actions unaffected.
                 *
                 * Returns @c Error::Ok when every action succeeded, the
                 * first failing action's error otherwise.  Backends that
                 * need full control of the block may still override this,
                 * but the common path is to override the three hooks
                 * below.
                 */
                virtual Error executeCmd(MediaIOCommandParams &cmd);

                /**
                 * @brief Reads one parameter's current value.
                 *
                 * Called by @ref executeCmd(MediaIOCommandParams &) for
                 * each @c Get action.  Default returns
                 * @c Error::NotSupported.  Override and dispatch on
                 * @p id, writing the value into @p out on success.
                 *
                 * @param id  Parameter to read.
                 * @param out Receives the current value on @c Error::Ok.
                 * @return @c Error::Ok on success; @c Error::NotSupported
                 *         for an unknown or write-only @p id.
                 */
                virtual Error getParam(MediaIOParamsID id, Variant &out);

                /**
                 * @brief Writes one parameter's value.
                 *
                 * Called by @ref executeCmd(MediaIOCommandParams &) for
                 * each @c Set action.  Default returns
                 * @c Error::NotSupported.  Override and dispatch on
                 * @p id.
                 *
                 * @param id    Parameter to write.
                 * @param value Value to apply.
                 * @return @c Error::Ok on success; @c Error::NotSupported
                 *         for an unknown or read-only @p id.
                 */
                virtual Error setParam(MediaIOParamsID id, const Variant &value);

                /**
                 * @brief Validates a prospective @c Set without applying it.
                 *
                 * Called by @ref executeCmd(MediaIOCommandParams &) before
                 * every @c Set — up front for an atomic block (so a
                 * rejection aborts before anything is applied) and inline
                 * just before each write for a non-atomic block (so a
                 * rejection skips only that write).  Default returns
                 * @c Error::Ok (accept).  Override to reject unknown ids or
                 * out-of-range / wrong-typed values so @ref setParam can
                 * trust its input; backends that cannot honor atomic
                 * semantics return @c Error::NotSupported here so an atomic
                 * block fails before anything is applied.
                 *
                 * @param id    Parameter that would be written.
                 * @param value Value that would be applied.
                 * @return @c Error::Ok when the @c Set is acceptable.
                 */
                virtual Error validateParam(MediaIOParamsID id, const Variant &value);

                /**
                 * @brief Handles an instance-wide stats query.
                 *
                 * Default returns @c Error::Ok with @c cmd.stats
                 * untouched.  Backends populate cumulative aggregate
                 * keys on @c cmd.stats; the framework's
                 * @c populateStandardStats overlays the standard
                 * MediaIO-managed keys after this hook runs.
                 */
                virtual Error executeCmd(MediaIOCommandStats &cmd);

                /**
                 * @brief Handles a request to swap the group's @ref Clock.
                 *
                 * Default returns @c Error::NotSupported.  Backends that
                 * can use an external clock as a pacing reference (NDI
                 * sender, RTP sender) override to accept the swap and
                 * record the new clock for use by their write path.
                 *
                 * On @c Error::Ok the framework swaps
                 * @c cmd.group->_clock inside
                 * @ref MediaIO::completeCommand; backends never write
                 * to the group's clock pointer directly.  A null
                 * @c cmd.clock asks the backend to restore its default
                 * behavior.
                 */
                virtual Error executeCmd(MediaIOCommandSetClock &cmd);

                /**
                 * @brief Called when a Write command carries a
                 *        non-empty @ref Frame::configUpdate delta.
                 *
                 * Invoked by @ref dispatch on the same thread, just
                 * before @c executeCmd(Write).  Default is a no-op;
                 * backends with dynamic reconfiguration (e.g. encoder
                 * bitrate ramps) override.
                 */
                virtual void configChanged(const MediaConfig &delta);

                // ---- Port-construction helpers ----
                //
                // Backends call these from @c executeCmd(MediaIOCommandOpen)
                // to declare the ports they expose.  Every port belongs
                // to a @ref MediaIOPortGroup; backends with a single
                // independent port make a single-port group first.
                // All ports are auto-destroyed on close via the
                // ObjectBase parent/child cascade rooted at this MediaIO.

                /**
                 * @brief Creates a port group with a backend-supplied clock.
                 *
                 * Use this overload when the backend has its own
                 * timing reference (capture-card device clock, audio
                 * drain clock, PTP source).
                 *
                 * @param name  Human-readable group name; may be empty.
                 * @param clock The group's timing reference; non-null.
                 * @return The new group, or nullptr if @p clock is null.
                 */
                MediaIOPortGroup *addPortGroup(const String &name, const Clock::Ptr &clock);

                /**
                 * @brief Creates a port group with a default synthesized clock.
                 *
                 * Allocates a @ref MediaIOClock for the new group,
                 * late-bound so the group's required Clock invariant
                 * is preserved.  Use this overload for backends without
                 * a hardware timing reference.
                 *
                 * @param name Human-readable group name; may be empty.
                 * @return The new group.
                 */
                MediaIOPortGroup *addPortGroup(const String &name = String());

                /**
                 * @brief Creates a source port and registers it with @p group.
                 *
                 * @param group The owning port group; non-null.
                 * @param desc  The @ref MediaDesc this source produces.
                 * @param name  Optional port name; defaults to @c "src{index}".
                 * @return The new source, or nullptr on failure.
                 */
                MediaIOSource *addSource(MediaIOPortGroup *group, const MediaDesc &desc,
                                         const String &name = String());

                /**
                 * @brief Creates a sink port and registers it with @p group.
                 *
                 * @param group The owning port group; non-null.
                 * @param desc  The @ref MediaDesc this sink expects to receive.
                 * @param name  Optional port name; defaults to @c "sink{index}".
                 * @return The new sink, or nullptr on failure.
                 */
                MediaIOSink *addSink(MediaIOPortGroup *group, const MediaDesc &desc,
                                     const String &name = String());

                // ---- Live-telemetry helpers ----
                //
                // Forwards to the per-group atomic counters.  Safe to
                // call from any thread.  Cheap (one atomic increment).
                // No-op when @p group is null.

                /** @brief Increments the dropped-frame counter for @p group; no-op when @p group is null. */
                void noteFrameDropped(MediaIOPortGroup *group);

                /** @brief Increments the repeated-frame counter for @p group; no-op when @p group is null. */
                void noteFrameRepeated(MediaIOPortGroup *group);

                /** @brief Increments the late-frame counter for @p group; no-op when @p group is null. */
                void noteFrameLate(MediaIOPortGroup *group);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
