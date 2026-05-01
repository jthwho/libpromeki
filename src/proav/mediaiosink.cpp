/**
 * @file      mediaiosink.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiosink.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/framerate.h>
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/clockdomain.h>
#include <promeki/mediatimestamp.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <cstdint>

PROMEKI_NAMESPACE_BEGIN

namespace {

String defaultSinkName(const String &name, int index) {
        if (!name.isEmpty()) return name;
        return String("sink") + String::number(index);
}

// Mirror of the helper used on the read path: stamp synthetic pts /
// duration on any payloads that came in without timing.  The matching
// read-side copy lives in mediaio.cpp; both share the same body so a
// frame produced or consumed by any backend ships fully-stamped.
void ensurePayloadTiming(Frame::Ptr &frame, const MediaTimeStamp &synMts, const FrameRate &frameRate) {
        if (!frame.isValid()) return;
        const Duration oneFrame = frameRate.isValid() ? frameRate.frameDuration() : Duration();
        for (size_t i = 0; i < frame->payloadList().size(); ++i) {
                const MediaPayload::Ptr &p = frame->payloadList()[i];
                if (!p.isValid()) continue;
                MediaPayload *mp = frame.modify()->payloadList()[i].modify();
                if (!mp->pts().isValid()) mp->setPts(synMts);
                if (mp->hasDuration() && mp->duration().isZero() && !oneFrame.isZero()) {
                        mp->setDuration(oneFrame);
                }
        }
}

} // namespace

MediaIOSink::MediaIOSink(MediaIOPortGroup *group, int index, const String &name)
        : MediaIOPort(group, defaultSinkName(name, index), index) {}

MediaIOSink::~MediaIOSink() = default;

Error MediaIOSink::setExpectedDesc(const MediaDesc &desc) {
        MediaIO *io = mediaIO();
        if (io != nullptr && io->isOpen()) return Error::AlreadyOpen;
        _expectedMediaDesc = desc;
        return Error::Ok;
}

Error MediaIOSink::setExpectedAudioDesc(const AudioDesc &desc) {
        MediaIO *io = mediaIO();
        if (io != nullptr && io->isOpen()) return Error::AlreadyOpen;
        _expectedAudioDesc = desc;
        return Error::Ok;
}

Error MediaIOSink::setExpectedMetadata(const Metadata &meta) {
        MediaIO *io = mediaIO();
        if (io != nullptr && io->isOpen()) return Error::AlreadyOpen;
        _expectedMetadata = meta;
        return Error::Ok;
}

Error MediaIOSink::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        MediaIO *io = mediaIO();
        if (io == nullptr) {
                if (preferred != nullptr) *preferred = MediaDesc();
                return Error::NotSupported;
        }
        return io->proposeInput(offered, preferred);
}

void MediaIOSink::setWriteDepth(int n) {
        _writeDepth = n < 1 ? 1 : n;
}

int MediaIOSink::pendingWrites() const {
        MediaIOPortGroup *g = group();
        return g != nullptr ? g->pendingWrites() : 0;
}

int MediaIOSink::writesAccepted() const {
        MediaIO *io = mediaIO();
        int      used = pendingWrites();
        if (io != nullptr) used += io->pendingInternalWrites();
        int avail = _writeDepth - used;
        return avail > 0 ? avail : 0;
}

MediaIORequest MediaIOSink::writeFrame(const Frame::Ptr &frame) {
        MediaIO *io = mediaIO();
        if (io == nullptr) return MediaIORequest::resolved(Error::Invalid);
        if (!io->isOpen() || io->isClosing()) return MediaIORequest::resolved(Error::NotOpen);

        // Always-on capacity gate.  When the sink is full we refuse
        // up-front with @c TryAgain so the caller can retry after a
        // @c frameWanted signal instead of letting the queue grow
        // unbounded.  The previous bool-block contract gated only
        // for non-blocking writes; with the always-async API the
        // gate is uniform.
        if (writesAccepted() <= 0) {
                return MediaIORequest::resolved(Error::TryAgain);
        }

        MediaIOPortGroup *g = group();

        auto *cmdWrite = new MediaIOCommandWrite();
        cmdWrite->frame = frame;
        cmdWrite->group = g;
        cmdWrite->sink = this;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdWrite);

        // Stamp synthetic pts / duration before the cmd leaves the
        // user thread so the backend always sees fully-timed
        // payloads regardless of what the caller provided.
        if (cmdWrite->frame.isValid() && g != nullptr) {
                const FrameRate &rate = g->frameRate();
                int64_t          ns = rate.cumulativeTicks(INT64_C(1000000000),
                                                  _writeFrameCount.isFinite() ? _writeFrameCount.value() : 0);
                TimeStamp        synTs = g->originTime() + Duration::fromNanoseconds(ns);
                MediaTimeStamp   synMts(synTs, ClockDomain::Synthetic);
                ensurePayloadTiming(cmdWrite->frame, synMts, rate);
                _writeFrameCount++;
        }

        // Per-group claim.  The matching decrement runs in
        // @ref MediaIO::completeCommand for Write so the counter
        // accounts for backend-completed and cancelled cmds in the
        // same place.
        if (g != nullptr) g->_pendingWriteCount.fetchAndAdd(1);

        MediaIORequest req(cmd);
        io->submit(cmd);
        return req;
}

PROMEKI_NAMESPACE_END
