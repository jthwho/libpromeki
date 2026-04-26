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
#include <promeki/url.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_DebugMedia)

// ---------------------------------------------------------------------------
// pmdf:// URL → Config translator.
//
// Canonical forms:
//   pmdf:/abs/path.pmdf              (opaque, absolute path)
//   pmdf:rel/path.pmdf               (opaque, relative path)
//   pmdf:///abs/path.pmdf            (authority form, empty host)
//
// The URL's path component becomes @ref MediaConfig::Filename;
// authority must be empty (pmdf:// files are always local, so a host
// component would be nonsense).  Query parameters are handled
// generically by MediaIO::applyQueryToConfig after this callback
// returns — today PMDF has no tunables beyond Filename but anything
// added later picks up URL support for free.
// ---------------------------------------------------------------------------
static Error debugMediaUrlToConfig(const Url &url, MediaIO::Config *outConfig) {
        if(!url.host().isEmpty()) {
                promekiErr("pmdf URL rejects a non-empty host: '%s' "
                           "(use pmdf:/path or pmdf:///path)",
                           url.toString().cstr());
                return Error::InvalidArgument;
        }
        const String path = url.path();
        if(path.isEmpty()) {
                promekiErr("pmdf URL requires a file path: '%s'",
                           url.toString().cstr());
                return Error::InvalidArgument;
        }
        outConfig->set(MediaConfig::Filename, path);
        return Error::Ok;
}

MediaIO::FormatDesc MediaIOTask_DebugMedia::formatDesc() {
        MediaIO::FormatDesc desc;
        desc.name            = "PMDF";
        desc.displayName     = "Debug Media (.pmdf)";
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
        desc.schemes       = { "pmdf" };
        desc.urlToConfig   = debugMediaUrlToConfig;
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

        _file = DebugMediaFile::UPtr::create();

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

        cmd.metadata             = _file->sessionInfo();
        cmd.canSeek              = true;
        cmd.frameCount           = _file->frameCount();
        cmd.defaultStep          = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth    = 0;

        // Peek frame 0 so the MediaIO layer can report mediaDesc /
        // audioDesc / frameRate without the caller having to read
        // the first frame first.  The pipeline planner relies on
        // this to discover the produced MediaDesc of a PMDF source
        // stage before the pipeline is opened.  We rewind the file
        // cursor to frame 0 so the caller's first readFrame() still
        // sees the peeked frame.
        if(cmd.frameCount.isFinite() && cmd.frameCount.value() > 0) {
                Frame::Ptr firstFrame;
                Error pe = _file->readFrameAt(FrameNumber(0), firstFrame);
                if(pe.isOk() && firstFrame.isValid()) {
                        MediaDesc md = firstFrame->mediaDesc();
                        cmd.mediaDesc = md;
                        cmd.frameRate = md.frameRate();
                        if(!md.audioList().isEmpty()) {
                                cmd.audioDesc = md.audioList()[0];
                        }
                        // Rewind — readFrame() advanced past the peek;
                        // seek back so the first real read returns frame 0.
                        (void)_file->seek(FrameNumber(0));
                } else if(pe.isError()) {
                        promekiWarn("MediaIOTask_DebugMedia: peek of frame 0 "
                                    "failed on '%s' (%s); mediaDesc will be "
                                    "empty until the first readFrame()",
                                    _filename.cstr(), pe.name().cstr());
                }
        }
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
        cmd.currentFrame = toFrameNumber(_framesRead);
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
        cmd.currentFrame = toFrameNumber(_framesWritten);
        cmd.frameCount   = _framesWritten;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_DebugMedia::executeCmd(MediaIOCommandSeek &cmd) {
        if(_mode != MediaIO::Source || !_file) return Error::NotOpen;
        FrameNumber target = cmd.frameNumber.isValid() ? cmd.frameNumber : FrameNumber(0);
        Error e = _file->seek(target);
        if(e.isError()) return e;
        cmd.currentFrame = cmd.frameNumber;
        _framesRead      = toFrameCount(target);
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
