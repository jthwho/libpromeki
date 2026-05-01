/**
 * @file      inlinemediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/inlinemediaio.h>

#include <promeki/mediaiostats.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

InlineMediaIO::InlineMediaIO(ObjectBase *parent) : CommandMediaIO(parent) {}

InlineMediaIO::~InlineMediaIO() = default;

void InlineMediaIO::submit(MediaIOCommand::Ptr cmd) {
        // Inline strategy: run dispatch + completeCommand right here.
        // Per-command telemetry — QueueWaitDuration is effectively
        // zero (no queue), but populate it for uniformity with the
        // other strategies' stats reporting.
        const TimeStamp dispatchTime = TimeStamp::now();
        MediaIOCommand *raw = cmd.modify();
        raw->stats.set(MediaIOStats::QueueWaitDuration, Duration());
        if (cmd->cancelled.value()) {
                raw->result = Error::Cancelled;
        } else {
                raw->result = dispatch(cmd);
        }
        const TimeStamp endTime = TimeStamp::now();
        raw->stats.set(MediaIOStats::ExecuteDuration, endTime - dispatchTime);
        completeCommand(cmd);
}

PROMEKI_NAMESPACE_END
