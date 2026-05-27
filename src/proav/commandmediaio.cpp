/**
 * @file      commandmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/commandmediaio.h>

#include <promeki/clock.h>
#include <promeki/logger.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioclock.h>
#include <promeki/mediaioport.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>

PROMEKI_NAMESPACE_BEGIN

CommandMediaIO::CommandMediaIO(ObjectBase *parent) : MediaIO(parent) {}

CommandMediaIO::~CommandMediaIO() = default;

Error CommandMediaIO::dispatch(MediaIOCommand::Ptr cmd) {
        MediaIOCommand *raw = cmd.modify();
        Error           result = Error::NotSupported;
        switch (raw->kind()) {
                case MediaIOCommand::Open: {
                        auto *co = static_cast<MediaIOCommandOpen *>(raw);
                        result = executeCmd(*co);
                        // Open-failure cleanup contract: if executeCmd(Open)
                        // returned non-OK, immediately run executeCmd(Close)
                        // so the backend can release any half-allocated
                        // resources.  The original open error is preserved
                        // as the cmd's reported result; the cleanup close
                        // outcome is intentionally discarded — its job is
                        // to drain backend state, not to report a separate
                        // status to the caller.
                        if (result.isError()) {
                                MediaIOCommandClose cleanup;
                                (void)executeCmd(cleanup);
                        }
                        break;
                }
                case MediaIOCommand::Close: {
                        auto *cc = static_cast<MediaIOCommandClose *>(raw);
                        result = executeCmd(*cc);
                        break;
                }
                case MediaIOCommand::Read: {
                        auto *cr = static_cast<MediaIOCommandRead *>(raw);
                        result = executeCmd(*cr);
                        break;
                }
                case MediaIOCommand::Write: {
                        auto *cw = static_cast<MediaIOCommandWrite *>(raw);
                        // Apply any per-frame config update before the
                        // backend processes the frame so the backend
                        // sees the new config when it calls executeCmd.
                        if (cw->frame.isValid() && !cw->frame.configUpdate().isEmpty()) {
                                configChanged(cw->frame.configUpdate());
                        }
                        result = executeCmd(*cw);
                        break;
                }
                case MediaIOCommand::Seek: {
                        auto *cs = static_cast<MediaIOCommandSeek *>(raw);
                        result = executeCmd(*cs);
                        break;
                }
                case MediaIOCommand::Params: {
                        auto *cp = static_cast<MediaIOCommandParams *>(raw);
                        result = executeCmd(*cp);
                        break;
                }
                case MediaIOCommand::Stats: {
                        auto *cs = static_cast<MediaIOCommandStats *>(raw);
                        result = executeCmd(*cs);
                        break;
                }
                case MediaIOCommand::SetClock: {
                        auto *csc = static_cast<MediaIOCommandSetClock *>(raw);
                        result = executeCmd(*csc);
                        break;
                }
        }
        return result;
}

Error CommandMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        (void)cmd;
        return Error::NotImplemented;
}

Error CommandMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        return Error::Ok;
}

Error CommandMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        (void)cmd;
        return Error::NotSupported;
}

Error CommandMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        (void)cmd;
        return Error::NotSupported;
}

Error CommandMediaIO::executeCmd(MediaIOCommandSeek &cmd) {
        (void)cmd;
        return Error::IllegalSeek;
}

Error CommandMediaIO::executeCmd(MediaIOCommandParams &cmd) {
        MediaIOParams &block = cmd.block;
        const int      n = block.count();
        Error          firstError = Error::Ok;

        // Every Set is validated before it is applied.  For atomic blocks
        // the whole block is validated up front here so a rejection aborts
        // before anything is applied; non-atomic blocks validate each Set
        // inline in the apply pass below (a rejection skips just that Set).
        if (block.isAtomic()) {
                for (int i = 0; i < n; i++) {
                        MediaIOParamAction &a = block.action(i);
                        if (a.op != MediaIOParamOp::Set) continue;
                        Error e = validateParam(a.id, a.value);
                        if (e.isError()) {
                                a.error = e;
                                for (int j = 0; j < n; j++) {
                                        if (j != i) block.action(j).error = Error::TransactionAborted;
                                }
                                return e;
                        }
                }
        }

        // Records a committed Set so an atomic block can roll it back if a
        // later action fails.  hadPrior is false when the pre-write read
        // failed, in which case the value cannot be restored.
        struct Applied {
                MediaIOParamsID id;
                Variant         prior;
                bool            hadPrior;
        };
        List<Applied> committed;

        // Apply pass — strictly in list order so a Get always observes the
        // effect of any Set earlier in the block.
        for (int i = 0; i < n; i++) {
                MediaIOParamAction &a = block.action(i);

                if (a.op == MediaIOParamOp::Get) {
                        a.error = getParam(a.id, a.value);
                        if (a.error.isError() && firstError.isOk()) firstError = a.error;
                        continue;
                }

                // Set.  For atomic blocks, snapshot the current value first
                // so a later failure can roll this write back.
                if (block.isAtomic()) {
                        Variant prior;
                        Error   readErr = getParam(a.id, prior);
                        a.error = setParam(a.id, a.value);
                        if (a.error.isOk()) {
                                committed.pushToBack(Applied{a.id, prior, readErr.isOk()});
                                continue;
                        }

                        // Mid-apply failure: best-effort rollback of every
                        // committed Set, newest first.
                        for (size_t r = committed.size(); r-- > 0;) {
                                const Applied &ap = committed[r];
                                if (!ap.hadPrior) {
                                        promekiWarn("CommandMediaIO: cannot roll back param '%s' "
                                                    "(prior value was unreadable)",
                                                    ap.id.name().cstr());
                                        continue;
                                }
                                Error re = setParam(ap.id, ap.prior);
                                if (re.isError()) {
                                        promekiErr("CommandMediaIO: rollback of param '%s' failed: %s",
                                                   ap.id.name().cstr(), re.name().cstr());
                                }
                        }
                        // The failing action keeps its real error; everything
                        // else in the block is reported as aborted.
                        for (int j = 0; j < n; j++) {
                                if (j != i) block.action(j).error = Error::TransactionAborted;
                        }
                        return a.error;
                }

                // Non-atomic Set: validate immediately before applying.  A
                // rejection records the validation error and skips the
                // write; other actions are unaffected.
                a.error = validateParam(a.id, a.value);
                if (a.error.isError()) {
                        if (firstError.isOk()) firstError = a.error;
                        continue;
                }
                a.error = setParam(a.id, a.value);
                if (a.error.isError() && firstError.isOk()) firstError = a.error;
        }
        return firstError;
}

Error CommandMediaIO::getParam(MediaIOParamsID id, Variant &out) {
        (void)id;
        (void)out;
        return Error::NotSupported;
}

Error CommandMediaIO::setParam(MediaIOParamsID id, const Variant &value) {
        (void)id;
        (void)value;
        return Error::NotSupported;
}

Error CommandMediaIO::validateParam(MediaIOParamsID id, const Variant &value) {
        (void)id;
        (void)value;
        return Error::Ok;
}

Error CommandMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        (void)cmd;
        return Error::Ok;
}

Error CommandMediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        (void)cmd;
        return Error::NotSupported;
}

void CommandMediaIO::configChanged(const MediaConfig &delta) {
        (void)delta;
}

// ============================================================================
// Port-construction helpers — push into the MediaIO-owned containers
// via friend access.
// ============================================================================

MediaIOPortGroup *CommandMediaIO::addPortGroup(const String &name, const Clock::Ptr &clock) {
        if (!clock.isValid()) return nullptr;
        auto *group = new MediaIOPortGroup(this, name, clock);
        // Stamp the origin so the synthetic-pts arithmetic in the
        // read/write paths (originTime() + cumulative ns) produces a
        // valid TimeStamp from the very first frame.
        group->setOriginTime(TimeStamp::now());
        _portGroups += group;
        return group;
}

MediaIOPortGroup *CommandMediaIO::addPortGroup(const String &name) {
        // Default-clock case: allocate a MediaIOClock, construct the
        // group with it, then late-bind the clock's group pointer.
        // The dance preserves the group's "non-null clock at
        // construction" invariant while breaking the chicken-and-egg
        // between the clock (which needs a group to read currentFrame)
        // and the group (which needs a clock).
        auto      *rawClock = new MediaIOClock(nullptr);
        Clock::Ptr clockPtr = Clock::Ptr::takeOwnership(rawClock);
        auto      *group = new MediaIOPortGroup(this, name, clockPtr);
        rawClock->setGroup(group);
        // Stamp the origin so the synthetic-pts arithmetic in the
        // read/write paths produces a valid TimeStamp from frame 0.
        group->setOriginTime(TimeStamp::now());
        _portGroups += group;
        return group;
}

MediaIOSource *CommandMediaIO::addSource(MediaIOPortGroup *group, const MediaDesc &desc, const String &name) {
        if (group == nullptr) return nullptr;
        const int index = _sources.size();
        auto     *src = new MediaIOSource(group, index, name);
        group->addPort(src);
        _sources += src;
        src->setMediaDesc(desc);
        return src;
}

MediaIOSink *CommandMediaIO::addSink(MediaIOPortGroup *group, const MediaDesc &desc, const String &name) {
        if (group == nullptr) return nullptr;
        const int index = _sinks.size();
        auto     *sink = new MediaIOSink(group, index, name);
        group->addPort(sink);
        _sinks += sink;
        sink->setMediaDesc(desc);
        // Seed the sink's expected-input descriptor so callers that
        // want to read it back via @ref MediaIOSink::expectedDesc see
        // what the backend asked for.
        (void)sink->setExpectedDesc(desc);
        return sink;
}

void CommandMediaIO::noteFrameDropped(MediaIOPortGroup *group) {
        if (group == nullptr) return;
        group->_framesDroppedTotal.fetchAndAdd(1);
}

void CommandMediaIO::noteFrameRepeated(MediaIOPortGroup *group) {
        if (group == nullptr) return;
        group->_framesRepeatedTotal.fetchAndAdd(1);
}

void CommandMediaIO::noteFrameLate(MediaIOPortGroup *group) {
        if (group == nullptr) return;
        group->_framesLateTotal.fetchAndAdd(1);
}

PROMEKI_NAMESPACE_END
