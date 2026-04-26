/**
 * @file      mediaiotask_framebridge.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_framebridge.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/metadata.h>
#include <promeki/mediatimestamp.h>
#include <promeki/clockdomain.h>
#include <promeki/logger.h>
#include <promeki/url.h>
#include <chrono>
#include <thread>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_FrameBridge)

// ---------------------------------------------------------------------------
// pmfb:// URL → Config translator.
//
// Canonical form: pmfb://<name>[?FrameBridgeKey=value&...]
//
// The authority is the FrameBridge logical name (required).  All other
// knobs travel through the URL query map with their canonical
// MediaConfig key names (FrameBridgeRingDepth, FrameBridgeSyncMode,
// ...) — MediaIO::applyQueryToConfig (called by createFromUrl after
// this callback returns) handles the coercion and validation.  Keeping
// one spelling across Config, JSON, CLI, and URL entry points means a
// log line naming "FrameBridgeRingDepth" is searchable through every
// layer without translation tables.
// ---------------------------------------------------------------------------
static Error frameBridgeUrlToConfig(const Url &url, MediaIO::Config *outConfig) {
        if(url.host().isEmpty()) {
                promekiErr("pmfb URL requires a non-empty name "
                           "(e.g. pmfb://my-bridge): got '%s'",
                           url.toString().cstr());
                return Error::InvalidArgument;
        }
        outConfig->set(MediaConfig::FrameBridgeName, url.host());
        return Error::Ok;
}

MediaIO::FormatDesc MediaIOTask_FrameBridge::formatDesc() {
        MediaIO::FormatDesc desc{};
        desc.name = "FrameBridge";
        desc.displayName = "Frame Bridge";
        desc.description = "Cross-process shared-memory frame transport";
        desc.extensions = {};
        desc.canBeSource = true;   // reads from bridge (consumer)
        desc.canBeSink = true;     // writes to bridge  (producer)
        desc.canBeTransform = false;
        desc.create = []() -> MediaIOTask * {
                return new MediaIOTask_FrameBridge();
        };
        desc.configSpecs = []() -> MediaIO::Config::SpecMap {
                MediaIO::Config::SpecMap specs;
                auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                        const VariantSpec *gs = MediaConfig::spec(id);
                        specs.insert(id, gs ? VariantSpec(*gs).setDefault(def)
                                            : VariantSpec().setDefault(def));
                };
                s(MediaConfig::FrameBridgeName, String());
                s(MediaConfig::FrameBridgeRingDepth, int32_t(2));
                s(MediaConfig::FrameBridgeMetadataReserveBytes,
                  int32_t(64 * 1024));
                s(MediaConfig::FrameBridgeAudioHeadroomFraction, 0.20);
                s(MediaConfig::FrameBridgeAccessMode, int32_t(0600));
                s(MediaConfig::FrameBridgeGroupName, String());
                s(MediaConfig::FrameBridgeSyncMode, true);
                s(MediaConfig::FrameBridgeWaitForConsumer, true);
                return specs;
        };
        desc.defaultMetadata = []() -> Metadata {
                Metadata m;
                return m;
        };
        desc.schemes = { "pmfb" };
        desc.urlToConfig = frameBridgeUrlToConfig;
        return desc;
}

MediaIOTask_FrameBridge::MediaIOTask_FrameBridge()
        : _bridge(FrameBridge::UPtr::create()) {
}

MediaIOTask_FrameBridge::~MediaIOTask_FrameBridge() {
        if(_bridge) _bridge->close();
}

Error MediaIOTask_FrameBridge::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        const String name = cfg.getAs<String>(MediaConfig::FrameBridgeName);
        if(name.isEmpty()) {
                promekiErr("MediaIOTask_FrameBridge: FrameBridgeName is required");
                return Error::InvalidArgument;
        }

        // The semantics chosen here mirror how existing MediaIO backends
        // use the direction flags: Input means "this task consumes frames
        // from the caller" (i.e. the caller writes and we transport), so
        // the bridge is opened in Output mode.  Output means "this task
        // produces frames" — the caller reads, so we open the bridge
        // as Input and pull from a remote publisher.
        if(cmd.mode == MediaIO_Sink) {
                // Caller writes into us → we publish to the bridge.
                _isOutput = true;
                FrameBridge::Config bcfg;
                bcfg.mediaDesc = cmd.pendingMediaDesc;
                bcfg.audioDesc = cmd.pendingAudioDesc;
                if(!bcfg.mediaDesc.isValid()) {
                        return Error::Invalid;
                }
                bcfg.ringDepth = cfg.getAs<int32_t>(
                        MediaConfig::FrameBridgeRingDepth, int32_t(2));
                bcfg.metadataReserveBytes = static_cast<size_t>(
                        cfg.getAs<int32_t>(MediaConfig::FrameBridgeMetadataReserveBytes,
                                           int32_t(64 * 1024)));
                bcfg.audioHeadroomFraction = cfg.getAs<double>(
                        MediaConfig::FrameBridgeAudioHeadroomFraction, 0.20);
                bcfg.accessMode = static_cast<uint32_t>(
                        cfg.getAs<int32_t>(MediaConfig::FrameBridgeAccessMode,
                                           int32_t(0600)));
                bcfg.groupName = cfg.getAs<String>(
                        MediaConfig::FrameBridgeGroupName, String());
                bcfg.waitForConsumer = cfg.getAs<bool>(
                        MediaConfig::FrameBridgeWaitForConsumer, true);

                Error err = _bridge->openOutput(name, bcfg);
                if(err.isError()) return err;

                cmd.mediaDesc = bcfg.mediaDesc;
                cmd.audioDesc = bcfg.audioDesc;
                cmd.metadata = cmd.pendingMetadata;
                cmd.frameRate = bcfg.mediaDesc.frameRate();
                cmd.canSeek = false;
                cmd.frameCount = MediaIO::FrameCountInfinite;
                return Error::Ok;
        } else if(cmd.mode == MediaIO_Source) {
                // Caller reads from us → we consume from the bridge.
                _isOutput = false;
                const bool sync = cfg.getAs<bool>(
                        MediaConfig::FrameBridgeSyncMode, true);
                Error err = _bridge->openInput(name, sync);
                if(err.isError()) return err;

                cmd.mediaDesc = _bridge->mediaDesc();
                cmd.audioDesc = _bridge->audioDesc();
                cmd.metadata = cmd.pendingMetadata;
                cmd.frameRate = cmd.mediaDesc.frameRate();
                cmd.canSeek = false;
                cmd.frameCount = MediaIO::FrameCountInfinite;
                return Error::Ok;
        }
        return Error::NotSupported;
}

Error MediaIOTask_FrameBridge::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(_bridge) _bridge->close();
        return Error::Ok;
}

Error MediaIOTask_FrameBridge::executeCmd(MediaIOCommandRead &cmd) {
        // Read from the bridge (we were opened in Input mode).
        if(_isOutput) return Error::NotSupported;
        // Poll for a fresh TICK with a wide-ish deadline so slow
        // sources (low frame rates, or paused publishers) don't
        // bounce the caller with TryAgain every few hundred ms.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(1000);
        while(std::chrono::steady_clock::now() < deadline) {
                Error rerr;
                Frame::Ptr f = _bridge->readFrame(&rerr);
                if(rerr.isError()) return rerr;
                if(f) {
                        // Stamp the source UUID for downstream correlation.
                        f.modify()->metadata().set(
                                Metadata::stringToID(String("SourceUUID")),
                                _bridge->uuid().toString());
                        // Stamp the publisher's queue timestamp.  The
                        // FrameBridge wire protocol carries a steady_clock
                        // value, which is SystemMonotonic on POSIX and
                        // comparable across processes on the same host.
                        f.modify()->metadata().set(
                                Metadata::FrameBridgeTimeStamp,
                                MediaTimeStamp(_bridge->lastFrameTimeStamp(),
                                               ClockDomain::SystemMonotonic));
                        cmd.frame = f;
                        cmd.currentFrame = f->metadata().getAs<int64_t>(
                                Metadata::FrameNumber, int64_t(0));
                        return Error::Ok;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return Error::TryAgain;
}

void MediaIOTask_FrameBridge::cancelBlockingWork() {
        // Called on the thread invoking MediaIO::close(), which may be
        // different from the strand thread currently running a blocked
        // writeFrame.  Tripping the bridge's abort flag makes that
        // writeFrame return Error::Cancelled promptly so the strand is
        // free to process the Close we're about to submit.
        if(_bridge) _bridge->abort();
}

Error MediaIOTask_FrameBridge::executeCmd(MediaIOCommandWrite &cmd) {
        // Write to the bridge (we were opened in Output mode).
        if(!_isOutput) return Error::NotSupported;
        if(!cmd.frame) return Error::Invalid;
        // Service pending accepts so a newly-arriving consumer joins
        // before the next TICK.
        _bridge->service();
        Error err = _bridge->writeFrame(cmd.frame);
        if(err.isError()) return err;
        cmd.currentFrame++;
        cmd.frameCount = FrameCount(cmd.currentFrame.value());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
