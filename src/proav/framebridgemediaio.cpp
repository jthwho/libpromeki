/**
 * @file      framebridgemediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framebridgemediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/metadata.h>
#include <promeki/mediatimestamp.h>
#include <promeki/clockdomain.h>
#include <promeki/logger.h>
#include <promeki/url.h>
#include <promeki/mediaiorequest.h>
#include <promeki/thread.h>
#include <chrono>
#include <thread>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(FrameBridgeFactory)

// ---------------------------------------------------------------------------
// pmfb:// URL → Config translator.
//
// Canonical form: pmfb://<name>[?FrameBridgeKey=value&...]
//
// The authority is the FrameBridge logical name (required).  All other
// knobs travel through the URL query map with their canonical
// MediaConfig key names (FrameBridgeRingDepth, FrameBridgeSyncMode,
// ...) — MediaIO::applyQueryToConfig (called by createFromUrl after
// this callback returns) handles the coercion and validation.
// ---------------------------------------------------------------------------

Error FrameBridgeFactory::urlToConfig(const Url &url, Config *outConfig) const {
        if (url.host().isEmpty()) {
                promekiErr("pmfb URL requires a non-empty name "
                           "(e.g. pmfb://my-bridge): got '%s'",
                           url.toString().cstr());
                return Error::InvalidArgument;
        }
        outConfig->set(MediaConfig::FrameBridgeName, url.host());
        return Error::Ok;
}

MediaIOFactory::Config::SpecMap FrameBridgeFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::FrameBridgeName, String());
        s(MediaConfig::FrameBridgeRingDepth, int32_t(2));
        s(MediaConfig::FrameBridgeMetadataReserveBytes, int32_t(64 * 1024));
        s(MediaConfig::FrameBridgeAudioHeadroomFraction, 0.20);
        s(MediaConfig::FrameBridgeAccessMode, int32_t(0600));
        s(MediaConfig::FrameBridgeGroupName, String());
        s(MediaConfig::FrameBridgeSyncMode, true);
        s(MediaConfig::FrameBridgeWaitForConsumer, true);
        return specs;
}

MediaIO *FrameBridgeFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new FrameBridgeMediaIO(parent);
        io->setConfig(config);
        return io;
}

FrameBridgeMediaIO::FrameBridgeMediaIO(ObjectBase *parent)
        : SharedThreadMediaIO(parent), _bridge(FrameBridge::UPtr::create()) {}

FrameBridgeMediaIO::~FrameBridgeMediaIO() {
        if (isOpen()) (void)close().wait();
        if (_bridge) _bridge->close();
}

Error FrameBridgeMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        const String           name = cfg.getAs<String>(MediaConfig::FrameBridgeName);
        if (name.isEmpty()) {
                promekiErr("FrameBridgeMediaIO: FrameBridgeName is required");
                return Error::InvalidArgument;
        }

        // Direction is config-driven via MediaConfig::OpenMode.  Default
        // (Read) opens us as a source — the caller reads frames from us
        // and we consume from the bridge; Write opens us as a sink — the
        // caller writes frames into us and we publish to the bridge.
        Enum       modeEnum = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        _isOutput = isWrite;

        MediaDesc resolved;
        FrameRate frameRate;

        if (isWrite) {
                // Caller writes into us -> we publish to the bridge.
                FrameBridge::Config bcfg;
                bcfg.mediaDesc = cmd.pendingMediaDesc;
                bcfg.audioDesc = cmd.pendingAudioDesc;
                if (!bcfg.mediaDesc.isValid()) {
                        return Error::Invalid;
                }
                bcfg.ringDepth = cfg.getAs<int32_t>(MediaConfig::FrameBridgeRingDepth, int32_t(2));
                bcfg.metadataReserveBytes = static_cast<size_t>(
                        cfg.getAs<int32_t>(MediaConfig::FrameBridgeMetadataReserveBytes, int32_t(64 * 1024)));
                bcfg.audioHeadroomFraction = cfg.getAs<double>(MediaConfig::FrameBridgeAudioHeadroomFraction, 0.20);
                bcfg.accessMode =
                        static_cast<uint32_t>(cfg.getAs<int32_t>(MediaConfig::FrameBridgeAccessMode, int32_t(0600)));
                bcfg.groupName = cfg.getAs<String>(MediaConfig::FrameBridgeGroupName, String());
                bcfg.waitForConsumer = cfg.getAs<bool>(MediaConfig::FrameBridgeWaitForConsumer, true);

                Error err = _bridge->openOutput(name, bcfg);
                if (err.isError()) return err;

                resolved = bcfg.mediaDesc;
                frameRate = bcfg.mediaDesc.frameRate();
        } else {
                // Caller reads from us -> we consume from the bridge.
                const bool sync = cfg.getAs<bool>(MediaConfig::FrameBridgeSyncMode, true);
                Error      err = _bridge->openInput(name, sync);
                if (err.isError()) return err;

                resolved = _bridge->mediaDesc();
                frameRate = resolved.frameRate();
        }

        MediaIOPortGroup *group = addPortGroup("framebridge");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(frameRate);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (isWrite) {
                if (addSink(group, resolved) == nullptr) return Error::Invalid;
        } else {
                if (addSource(group, resolved) == nullptr) return Error::Invalid;
        }
        return Error::Ok;
}

Error FrameBridgeMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_bridge) _bridge->close();
        return Error::Ok;
}

Error FrameBridgeMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        // Read from the bridge (we were opened in Input mode).
        if (_isOutput) return Error::NotSupported;
        // Poll for a fresh TICK without a wall-clock deadline.
        // Returning Error::TryAgain from a leaf source strands the
        // pipeline pump (it expects an upstream Write to drive the
        // next frameReady, which never happens here), so we block
        // until either a frame arrives or cancelBlockingWork() trips
        // the bridge's abort flag.
        for (;;) {
                Error      rerr;
                Frame::Ptr f = _bridge->readFrame(&rerr);
                if (rerr.isError()) return rerr;
                if (f) {
                        // Stamp the source name for downstream correlation.
                        f.modify()->metadata().set(Metadata::stringToID(String("SourceName")), _bridge->name());
                        // Stamp the publisher's queue timestamp.  The
                        // FrameBridge wire protocol carries a steady_clock
                        // value, which is SystemMonotonic on POSIX and
                        // comparable across processes on the same host.
                        f.modify()->metadata().set(
                                Metadata::FrameBridgeTimeStamp,
                                MediaTimeStamp(_bridge->lastFrameTimeStamp(), ClockDomain::SystemMonotonic));
                        cmd.frame = f;
                        cmd.currentFrame = f->metadata().getAs<int64_t>(Metadata::FrameNumber, int64_t(0));
                        return Error::Ok;
                }
                if (_bridge->isAborted()) return Error::Cancelled;
                Thread::sleepMs(2);
        }
}

void FrameBridgeMediaIO::cancelBlockingWork() {
        // Called on the thread invoking MediaIO::close(), which may be
        // different from the strand thread currently running a blocked
        // writeFrame.  Tripping the bridge's abort flag makes that
        // writeFrame return Error::Cancelled promptly so the strand is
        // free to process the Close we're about to submit.
        if (_bridge) _bridge->abort();
}

Error FrameBridgeMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        // Write to the bridge (we were opened in Output mode).
        if (!_isOutput) return Error::NotSupported;
        if (!cmd.frame) return Error::Invalid;
        // Service pending accepts so a newly-arriving consumer joins
        // before the next TICK.
        _bridge->service();
        Error err = _bridge->writeFrame(cmd.frame);
        if (err.isError()) return err;
        cmd.currentFrame++;
        cmd.frameCount = FrameCount(cmd.currentFrame.value());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
