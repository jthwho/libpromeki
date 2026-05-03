/**
 * @file      tests/unit/mediaio_test_helpers.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Header-only test scaffolding for unit tests that exercise the
 * @ref promeki::MediaIO API without standing up a real backend or a
 * ThreadPool strand.  Two helpers are provided:
 *
 *   - @ref promeki::tests::InlineTestMediaIO — an @ref InlineMediaIO
 *     subclass with overridable @c executeCmd hooks fronted by
 *     @c std::function callbacks.  Useful for canned-response unit
 *     tests where the caller wants every command to resolve
 *     synchronously on the calling thread.
 *
 *   - @ref promeki::tests::PausedTestMediaIO — a @ref CommandMediaIO
 *     subclass whose @c submit queues commands without dispatching.
 *     The test drives execution explicitly via @c processOne /
 *     @c processAll.  This makes pre-dispatch cancellation tests
 *     deterministic without racing a real strand worker.
 */

#pragma once

#include <functional>

#include <promeki/commandmediaio.h>
#include <promeki/inlinemediaio.h>
#include <promeki/list.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiostats.h>
#include <promeki/timestamp.h>

namespace promeki {
        namespace tests {

                /**
 * @brief Inline-strategy MediaIO with callback-driven @c executeCmd hooks.
 *
 * Each @c executeCmd overload defaults to returning @c Error::Ok so
 * the helper is usable out-of-the-box for tests that just need an
 * "open" MediaIO to call APIs against.  Tests that want to inject
 * custom behavior set the matching @c on... callback.
 *
 * The helper does not register itself with @ref MediaIOFactory — it
 * is constructed directly on the stack by the test.
 */
                class InlineTestMediaIO : public ::promeki::InlineMediaIO {
                                PROMEKI_OBJECT(InlineTestMediaIO, InlineMediaIO)
                        public:
                                using OpenHook = std::function<Error(MediaIOCommandOpen &)>;
                                using CloseHook = std::function<Error(MediaIOCommandClose &)>;
                                using ReadHook = std::function<Error(MediaIOCommandRead &)>;
                                using WriteHook = std::function<Error(MediaIOCommandWrite &)>;
                                using SeekHook = std::function<Error(MediaIOCommandSeek &)>;
                                using ParamsHook = std::function<Error(MediaIOCommandParams &)>;
                                using StatsHook = std::function<Error(MediaIOCommandStats &)>;
                                using SetClockHook = std::function<Error(MediaIOCommandSetClock &)>;

                                InlineTestMediaIO(ObjectBase *parent = nullptr) : InlineMediaIO(parent) {}
                                ~InlineTestMediaIO() override = default;

                                OpenHook     onOpen;
                                CloseHook    onClose;
                                ReadHook     onRead;
                                WriteHook    onWrite;
                                SeekHook     onSeek;
                                ParamsHook   onParams;
                                StatsHook    onStats;
                                SetClockHook onSetClock;

                        protected:
                                Error executeCmd(MediaIOCommandOpen &cmd) override {
                                        return onOpen ? onOpen(cmd) : Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandClose &cmd) override {
                                        return onClose ? onClose(cmd) : Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandRead &cmd) override {
                                        return onRead ? onRead(cmd) : Error::NotSupported;
                                }
                                Error executeCmd(MediaIOCommandWrite &cmd) override {
                                        return onWrite ? onWrite(cmd) : Error::NotSupported;
                                }
                                Error executeCmd(MediaIOCommandSeek &cmd) override {
                                        return onSeek ? onSeek(cmd) : Error::IllegalSeek;
                                }
                                Error executeCmd(MediaIOCommandParams &cmd) override {
                                        return onParams ? onParams(cmd) : Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandStats &cmd) override {
                                        return onStats ? onStats(cmd) : Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandSetClock &cmd) override {
                                        return onSetClock ? onSetClock(cmd)
                                                          : ::promeki::CommandMediaIO::executeCmd(cmd);
                                }
                };

                /**
 * @brief Manually-pumped @ref CommandMediaIO subclass for cancellation tests.
 *
 * @c submit appends every incoming command to an internal queue
 * without dispatching it; the test calls @ref processOne or
 * @ref processAll to drive execution.  This mimics the
 * @ref SharedThreadMediaIO strand worker (the same per-command
 * telemetry is recorded and the same pre-dispatch cancellation
 * check is honored), but with deterministic timing.
 *
 * Tests use this helper to verify the "cancelled before dispatch"
 * branch of the cancellation contract: cancel the request *before*
 * calling @c processOne and the runner short-circuits to
 * @c Error::Cancelled exactly the way the strand would.
 *
 * Like @ref InlineTestMediaIO, the executeCmd hooks default to a
 * sensible no-op result.  Override the callbacks to inject custom
 * behavior.
 */
                class PausedTestMediaIO : public ::promeki::CommandMediaIO {
                                PROMEKI_OBJECT(PausedTestMediaIO, CommandMediaIO)
                        public:
                                using OpenHook = std::function<Error(MediaIOCommandOpen &)>;
                                using CloseHook = std::function<Error(MediaIOCommandClose &)>;
                                using ReadHook = std::function<Error(MediaIOCommandRead &)>;
                                using WriteHook = std::function<Error(MediaIOCommandWrite &)>;
                                using SeekHook = std::function<Error(MediaIOCommandSeek &)>;
                                using ParamsHook = std::function<Error(MediaIOCommandParams &)>;
                                using StatsHook = std::function<Error(MediaIOCommandStats &)>;

                                PausedTestMediaIO(ObjectBase *parent = nullptr) : CommandMediaIO(parent) {}
                                ~PausedTestMediaIO() override {
                                        // Resolve any queued commands so test futures
                                        // unblock — otherwise wait() in the test code
                                        // would hang on a destructor-time cmd that
                                        // never gets dispatched.
                                        processAll();
                                }

                                bool isIdle() const override { return _queue.isEmpty(); }

                                /**
                 * @brief Drains one queued command via the same
                 *        cancellation + telemetry path the
                 *        @ref SharedThreadMediaIO strand uses.
                 *
                 * @return @c true if a command was processed,
                 *         @c false if the queue was empty.
                 */
                                bool processOne() {
                                        if (_queue.isEmpty()) return false;
                                        MediaIOCommand::Ptr cmd = _queue.front();
                                        const TimeStamp     submitTime = _submitTimes.front();
                                        _queue.remove(static_cast<size_t>(0));
                                        _submitTimes.remove(static_cast<size_t>(0));
                                        runCommand(cmd, submitTime);
                                        return true;
                                }

                                /** @brief Drains every queued command in submit order. */
                                void processAll() {
                                        while (processOne()) {}
                                }

                                /** @brief Number of queued (not-yet-dispatched) commands. */
                                size_t pending() const { return _queue.size(); }

                                OpenHook   onOpen;
                                CloseHook  onClose;
                                ReadHook   onRead;
                                WriteHook  onWrite;
                                SeekHook   onSeek;
                                ParamsHook onParams;
                                StatsHook  onStats;

                        protected:
                                void submit(MediaIOCommand::Ptr cmd) override {
                                        _submitTimes.pushToBack(TimeStamp::now());
                                        _queue.pushToBack(cmd);
                                }

                                Error executeCmd(MediaIOCommandOpen &cmd) override {
                                        return onOpen ? onOpen(cmd) : Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandClose &cmd) override {
                                        return onClose ? onClose(cmd) : Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandRead &cmd) override {
                                        return onRead ? onRead(cmd) : Error::NotSupported;
                                }
                                Error executeCmd(MediaIOCommandWrite &cmd) override {
                                        return onWrite ? onWrite(cmd) : Error::NotSupported;
                                }
                                Error executeCmd(MediaIOCommandSeek &cmd) override {
                                        return onSeek ? onSeek(cmd) : Error::IllegalSeek;
                                }
                                Error executeCmd(MediaIOCommandParams &cmd) override {
                                        return onParams ? onParams(cmd) : Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandStats &cmd) override {
                                        return onStats ? onStats(cmd) : Error::Ok;
                                }

                        private:
                                void runCommand(MediaIOCommand::Ptr cmd, const TimeStamp &submitTime) {
                                        const TimeStamp dispatchTime = TimeStamp::now();
                                        MediaIOCommand *raw = cmd.modify();
                                        raw->stats.set(MediaIOStats::QueueWaitDuration, dispatchTime - submitTime);
                                        if (cmd->cancelled.value()) {
                                                raw->result = Error::Cancelled;
                                        } else {
                                                raw->result = dispatch(cmd);
                                        }
                                        const TimeStamp endTime = TimeStamp::now();
                                        raw->stats.set(MediaIOStats::ExecuteDuration, endTime - dispatchTime);
                                        completeCommand(cmd);
                                }

                                List<MediaIOCommand::Ptr> _queue;
                                List<TimeStamp>           _submitTimes;
                };

        } // namespace tests
} // namespace promeki
