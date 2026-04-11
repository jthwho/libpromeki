/**
 * @file      mediaiotask.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask.h>

PROMEKI_NAMESPACE_BEGIN

MediaIOTask::~MediaIOTask() = default;

Error MediaIOTask::executeCmd(MediaIOCommandOpen &cmd) {
        return Error::NotImplemented;
}

Error MediaIOTask::executeCmd(MediaIOCommandClose &cmd) {
        return Error::Ok;
}

Error MediaIOTask::executeCmd(MediaIOCommandRead &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandWrite &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandSeek &cmd) {
        return Error::IllegalSeek;
}

Error MediaIOTask::executeCmd(MediaIOCommandParams &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandStats &cmd) {
        return Error::Ok;
}

// ---- Live-telemetry helper forwarders ----
//
// All three forward into the owning MediaIO's per-instance counters.
// MediaIOTask is a friend of MediaIO (declared in mediaio.h), so the
// private atomic fields are accessible here.  Each helper guards
// against a null owner so tasks constructed in isolation (e.g. unit
// tests) don't crash if they invoke the helpers before being adopted.

void MediaIOTask::noteFrameDropped() {
        if(_owner == nullptr) return;
        _owner->_framesDroppedTotal.fetchAndAdd(1);
}

void MediaIOTask::noteFrameRepeated() {
        if(_owner == nullptr) return;
        _owner->_framesRepeatedTotal.fetchAndAdd(1);
}

void MediaIOTask::noteFrameLate() {
        if(_owner == nullptr) return;
        _owner->_framesLateTotal.fetchAndAdd(1);
}

PROMEKI_NAMESPACE_END
