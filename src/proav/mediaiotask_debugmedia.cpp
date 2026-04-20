/**
 * @file      mediaiotask_debugmedia.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_debugmedia.h>
#include <promeki/debugmediafile.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/frame.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_DebugMedia)

MediaIO::FormatDesc MediaIOTask_DebugMedia::formatDesc() {
        MediaIO::FormatDesc desc;
        desc.name            = "PMDF";
        desc.description     = "ProMEKI Debug Frame (.pmdf) — lossless Frame capture for debugging";
        desc.extensions      = {"pmdf"};
        desc.canBeSource     = true;
        desc.canBeSink       = true;
        desc.canBeTransform  = false;
        desc.create          = []() -> MediaIOTask * {
                return new MediaIOTask_DebugMedia();
        };
        desc.configSpecs     = []() -> MediaIO::Config::SpecMap {
                MediaIO::Config::SpecMap specs;
                if(const VariantSpec *gs = MediaConfig::spec(MediaConfig::Filename)) {
                        specs.insert(MediaConfig::Filename, *gs);
                }
                return specs;
        };
        desc.canHandlePath = [](const String &path) -> bool {
                return path.toLower().endsWith(".pmdf");
        };
        return desc;
}

MediaIOTask_DebugMedia::MediaIOTask_DebugMedia() = default;
MediaIOTask_DebugMedia::~MediaIOTask_DebugMedia() = default;

Error MediaIOTask_DebugMedia::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Source && cmd.mode != MediaIO::Sink) {
                promekiErr("MediaIOTask_DebugMedia: only Source/Sink modes are supported");
                return Error::NotSupported;
        }
        _filename = cmd.config.getAs<String>(MediaConfig::Filename);
        if(_filename.isEmpty()) {
                promekiErr("MediaIOTask_DebugMedia: Filename is required");
                return Error::InvalidArgument;
        }

        _file = std::make_unique<DebugMediaFile>();

        if(cmd.mode == MediaIO::Sink) {
                DebugMediaFile::OpenOptions opts;
                opts.sessionInfo = cmd.pendingMetadata;
                Error e = _file->open(_filename, DebugMediaFile::Write, opts);
                if(e.isError()) {
                        _file.reset();
                        return e;
                }
                _mode           = MediaIO::Sink;
                _framesWritten  = 0;
                _framesRead     = 0;

                // Forward the upstream shape unchanged — PMDF captures
                // whatever it's given.
                cmd.mediaDesc            = cmd.pendingMediaDesc;
                cmd.audioDesc            = cmd.pendingAudioDesc;
                cmd.metadata             = cmd.pendingMetadata;
                cmd.frameRate            = cmd.pendingMediaDesc.frameRate();
                cmd.canSeek              = false;
                cmd.frameCount           = MediaIO::FrameCountInfinite;
                cmd.defaultStep          = 1;
                cmd.defaultPrefetchDepth = 0;
                cmd.defaultWriteDepth    = 4;
                return Error::Ok;
        }

        // Source mode.
        Error e = _file->open(_filename, DebugMediaFile::Read);
        if(e.isError()) {
                _file.reset();
                return e;
        }
        _mode           = MediaIO::Source;
        _framesRead     = 0;
        _framesWritten  = 0;

        // We intentionally do not peek the first frame here to populate
        // mediaDesc / frameRate — doing so advances the file cursor and
        // tripped up the test harness.  Callers that need the format
        // before the first read can call MediaIO::readFrame once and
        // inspect the returned Frame.
        cmd.metadata             = _file->sessionInfo();
        cmd.canSeek              = true;
        cmd.frameCount           = _file->frameCount();
        cmd.defaultStep          = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth    = 0;
        return Error::Ok;
}

Error MediaIOTask_DebugMedia::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if(_file) {
                _file->close();
                _file.reset();
        }
        _filename.clear();
        _mode          = MediaIO::NotOpen;
        _framesWritten = 0;
        _framesRead    = 0;
        return Error::Ok;
}

Error MediaIOTask_DebugMedia::executeCmd(MediaIOCommandRead &cmd) {
        if(_mode != MediaIO::Source || !_file) return Error::NotOpen;
        stampWorkBegin();

        Frame::Ptr frame;
        Error e = _file->readFrame(frame);
        if(e == Error::EndOfFile) { stampWorkEnd(); return Error::EndOfFile; }
        if(e.isError())          { stampWorkEnd(); return e; }

        ++_framesRead;
        cmd.frame        = std::move(frame);
        cmd.currentFrame = _framesRead;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_DebugMedia::executeCmd(MediaIOCommandWrite &cmd) {
        if(_mode != MediaIO::Sink || !_file) return Error::NotOpen;
        if(!cmd.frame.isValid()) return Error::InvalidArgument;
        stampWorkBegin();

        Error e = _file->writeFrame(cmd.frame);
        if(e.isError()) { stampWorkEnd(); return e; }

        ++_framesWritten;
        cmd.currentFrame = _framesWritten;
        cmd.frameCount   = _framesWritten;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_DebugMedia::executeCmd(MediaIOCommandSeek &cmd) {
        if(_mode != MediaIO::Source || !_file) return Error::NotOpen;
        Error e = _file->seek(cmd.frameNumber);
        if(e.isError()) return e;
        cmd.currentFrame = cmd.frameNumber;
        _framesRead      = cmd.frameNumber;
        return Error::Ok;
}

Error MediaIOTask_DebugMedia::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesWritten, _framesWritten);
        cmd.stats.set(StatsFramesRead,    _framesRead);
        return Error::Ok;
}

// ---- Introspection / negotiation ----
//
// PMDF is format-agnostic: it captures whatever frame it's given and
// plays it back byte-perfect.  Both proposeInput and proposeOutput
// accept-as-is.

Error MediaIOTask_DebugMedia::proposeInput(const MediaDesc &offered,
                                           MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;
        *preferred = offered;
        return Error::Ok;
}

Error MediaIOTask_DebugMedia::proposeOutput(const MediaDesc &requested,
                                            MediaDesc *achievable) const {
        if(achievable == nullptr) return Error::Invalid;
        *achievable = requested;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
