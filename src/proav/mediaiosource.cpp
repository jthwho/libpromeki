/**
 * @file      mediaiosource.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiosource.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        String defaultSourceName(const String &name, int index) {
                if (!name.isEmpty()) return name;
                return String("src") + String::number(index);
        }

} // namespace

MediaIOSource::MediaIOSource(MediaIOPortGroup *group, int index, const String &name)
    : MediaIOPort(group, defaultSourceName(name, index), index) {}

MediaIOSource::~MediaIOSource() = default;

bool MediaIOSource::frameAvailable() const {
        return _readCache.isHeadReady();
}

int MediaIOSource::readyReads() const {
        return _readCache.count();
}

int MediaIOSource::pendingReads() const {
        MediaIOPortGroup *g = group();
        return g != nullptr ? g->pendingReads() : 0;
}

void MediaIOSource::setPrefetchDepth(int n) {
        _readCache.setDepth(n);
        _prefetchDepthExplicit = true;
}

Error MediaIOSource::proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const {
        MediaIO *io = mediaIO();
        if (io == nullptr) {
                if (achievable != nullptr) *achievable = MediaDesc();
                return Error::NotSupported;
        }
        return io->proposeOutput(requested, achievable);
}

int MediaIOSource::step() const {
        MediaIOPortGroup *g = group();
        return g != nullptr ? g->step() : 1;
}

void MediaIOSource::setStep(int val) {
        MediaIOPortGroup *g = group();
        if (g != nullptr) g->setStep(val);
}

FrameNumber MediaIOSource::currentFrame() const {
        MediaIOPortGroup *g = group();
        return g != nullptr ? g->currentFrame() : FrameNumber();
}

MediaIORequest MediaIOSource::seekToFrame(const FrameNumber &frameNumber, MediaIOSeekMode mode) {
        MediaIOPortGroup *g = group();
        if (g == nullptr) return MediaIORequest::resolved(Error::Invalid);
        return g->seekToFrame(frameNumber, mode);
}

MediaIORequest MediaIOSource::readFrame() {
        MediaIO          *io = mediaIO();
        MediaIOPortGroup *g = group();
        if (io == nullptr || g == nullptr) return MediaIORequest::resolved(Error::Invalid);
        // Drain the cache before checking open / EOF state so the
        // synthetic trailing EOS pushed by @ref MediaIO::close (and
        // any reads that completed before close raced past us) are
        // observable through the same path as a normal frame
        // delivery.  Otherwise a consumer racing close() would see
        // @c Error::NotOpen instead of @c Error::EndOfFile and
        // misclassify the shutdown as an error.
        if (!_readCache.isEmpty()) return _readCache.readFrame();
        // Both !isOpen and isClosing surface as @c NotOpen — the
        // cache's @ref MediaIOReadCache::submitOneLocked refuses to
        // start fresh prefetch in either state, and falling through
        // would reach the cache's submit-failed fallback which
        // returns @c Error::Invalid.  That fallback is correct as a
        // backstop for "this source has no plumbing at all" (group
        // == nullptr and friends), but for a normal close cascade
        // the right error is @c NotOpen so consumer plumbing
        // (@ref MediaPipeline::onSourceConnectionError, the planner
        // open-probe) can filter close-time noise on a single
        // well-known sentinel.
        if (!io->isOpen() || io->isClosing()) return MediaIORequest::resolved(Error::NotOpen);
        // EOF latch on the group is honored without going down to
        // the cache — once end-of-stream has been observed, every
        // subsequent readFrame returns the latched EOF until a seek
        // or close clears it.  This keeps consumers from accidentally
        // resetting prefetch by pumping past the latch.
        if (g->_atEnd) return MediaIORequest::resolved(Error::EndOfFile);
        return _readCache.readFrame();
}

size_t MediaIOSource::cancelPending() {
        MediaIO *io = mediaIO();
        if (io == nullptr) return 0;
        if (!io->isOpen()) return 0;
        return _readCache.cancelAll();
}

PROMEKI_NAMESPACE_END
