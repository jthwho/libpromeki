/**
 * @file      mediaioportgroup.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaioportgroup.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaioport.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediaioreadcache.h>
#include <promeki/strand.h>
#include <cassert>
#include <cmath>

PROMEKI_NAMESPACE_BEGIN

MediaIOPortGroup::MediaIOPortGroup(MediaIO *mediaIO, const String &name, const Clock::Ptr &clock)
        : ObjectBase(mediaIO), _mediaIO(mediaIO), _name(name), _clock(clock) {
        // A group must have a clock — paired ports share a timing
        // reference by definition, so there is no meaningful "no
        // clock" state.  The CommandMediaIO::addPortGroup overload that
        // takes no clock handles the common synthesized-clock case.
        assert(clock.isValid() && "MediaIOPortGroup requires a non-null clock");
}

MediaIOPortGroup::~MediaIOPortGroup() = default;

void MediaIOPortGroup::addPort(MediaIOPort *port) {
        _ports += port;
}

void MediaIOPortGroup::setRate(double r) {
        // Reject non-finite rates outright.  In debug builds we trip
        // an assert so the misuse surfaces immediately; in release we
        // silently no-op rather than corrupt the accumulator.
        assert(std::isfinite(r) && "MediaIOPortGroup::setRate rejects non-finite rate");
        if (!std::isfinite(r)) return;

        if (r == _rate) return;
        // Outstanding prefetched reads were submitted with the old
        // rate; they're stale relative to the new direction.  Cancel
        // them and discard any results that already came back.  Also
        // clear the EOF latch — the new direction may make more frames
        // available (e.g. flipping forward-EOF to reverse).
        if (_mediaIO != nullptr && _mediaIO->isOpen()) {
                _mediaIO->cancelPendingWork();
                for (MediaIOPort *p : _ports) {
                        if (p == nullptr) continue;
                        if (p->role() != MediaIOPort::Source) continue;
                        auto *src = static_cast<MediaIOSource *>(p);
                        src->_readCache.cancelAll();
                }
                _atEnd = false;
        }
        _rate = r;
        _rateAccumulator = 0.0;
}

int MediaIOPortGroup::nextStep() {
        // Truncate-toward-zero accumulator: emits |floor(|acc|)| of
        // the right sign, then drains that many whole units back out.
        // For rate=0.5 the sequence is 0,1,0,1; for rate=2.5 it's
        // 2,3,2,3; for rate=-0.5 it's 0,-1,0,-1; for rate=0 always 0.
        _rateAccumulator += _rate;
        int step = static_cast<int>(_rateAccumulator);
        _rateAccumulator -= static_cast<double>(step);
        return step;
}

MediaIORequest MediaIOPortGroup::setClock(const Clock::Ptr &clock) {
        if (_mediaIO == nullptr) return MediaIORequest::resolved(Error::Invalid);
        if (!_mediaIO->isOpen() || _mediaIO->isClosing()) return MediaIORequest::resolved(Error::NotOpen);

        auto *cmdSet = new MediaIOCommandSetClock();
        cmdSet->group = this;
        cmdSet->clock = clock;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdSet);
        MediaIORequest      req(cmd);
        _mediaIO->submit(cmd);
        return req;
}

MediaIORequest MediaIOPortGroup::seekToFrame(const FrameNumber &frameNumber, MediaIOSeekMode mode) {
        if (_mediaIO == nullptr) return MediaIORequest::resolved(Error::Invalid);
        if (!_mediaIO->isOpen() || _mediaIO->isClosing()) return MediaIORequest::resolved(Error::NotOpen);
        if (!_canSeek) return MediaIORequest::resolved(Error::IllegalSeek);

        // Cancel any prefetched reads from the old position before
        // submitting the seek.  Otherwise the next read would return
        // a stale frame from the pre-seek queue.
        _mediaIO->cancelPendingWork();
        for (MediaIOPort *p : _ports) {
                if (p == nullptr) continue;
                if (p->role() != MediaIOPort::Source) continue;
                auto *src = static_cast<MediaIOSource *>(p);
                src->_readCache.cancelAll();
        }
        // Seeking past EOF clears the EOF latch — the new position
        // may be valid even if the previous one was end-of-stream.
        _atEnd = false;
        // Reset the rate accumulator so the post-seek tick lands at
        // the requested frame rather than carrying fractional phase
        // from the pre-seek position.
        _rateAccumulator = 0.0;

        auto *cmdSeek = new MediaIOCommandSeek();
        cmdSeek->frameNumber = frameNumber;
        cmdSeek->mode = (mode == MediaIO_SeekDefault) ? _mediaIO->_defaultSeekMode : mode;
        cmdSeek->group = this;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdSeek);
        MediaIORequest      req(cmd);
        _mediaIO->submit(cmd);
        return req;
}

PROMEKI_NAMESPACE_END
