/**
 * @file      commandmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/commandmediaio.h>

#include <promeki/clock.h>
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
                        if (cw->frame.isValid() && !cw->frame->configUpdate().isEmpty()) {
                                configChanged(cw->frame->configUpdate());
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
        (void)cmd;
        return Error::NotSupported;
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
