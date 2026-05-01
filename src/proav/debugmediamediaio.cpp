/**
 * @file      debugmediamediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/debugmediamediaio.h>

#include <promeki/debugmediafile.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/url.h>
#include <promeki/variantspec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(DebugMediaFactory)

// ============================================================================
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
// returns.
// ============================================================================

Error DebugMediaFactory::urlToConfig(const Url &url, Config *outConfig) const {
        if (!url.host().isEmpty()) {
                promekiErr("pmdf URL rejects a non-empty host: '%s' "
                           "(use pmdf:/path or pmdf:///path)",
                           url.toString().cstr());
                return Error::InvalidArgument;
        }
        const String path = url.path();
        if (path.isEmpty()) {
                promekiErr("pmdf URL requires a file path: '%s'", url.toString().cstr());
                return Error::InvalidArgument;
        }
        outConfig->set(MediaConfig::Filename, path);
        return Error::Ok;
}

MediaIOFactory::Config::SpecMap DebugMediaFactory::configSpecs() const {
        Config::SpecMap specs;
        if (const VariantSpec *gs = MediaConfig::spec(MediaConfig::Filename)) {
                specs.insert(MediaConfig::Filename, *gs);
        }
        return specs;
}

MediaIO *DebugMediaFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new DebugMediaMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ============================================================================
// DebugMediaMediaIO
// ============================================================================

DebugMediaMediaIO::DebugMediaMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

DebugMediaMediaIO::~DebugMediaMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error DebugMediaMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        _filename = cmd.config.getAs<String>(MediaConfig::Filename);
        if (_filename.isEmpty()) {
                promekiErr("DebugMediaMediaIO: Filename is required");
                return Error::InvalidArgument;
        }

        // Direction is config-driven via MediaConfig::OpenMode.  Default
        // (Read) means open as a source; Write opens as a sink.
        Enum       modeEnum = cmd.config.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();

        _file = DebugMediaFile::UPtr::create();

        MediaDesc resolvedDesc;
        FrameRate resolvedFps;
        if (isWrite) {
                DebugMediaFile::OpenOptions opts;
                opts.sessionInfo = cmd.pendingMetadata;
                Error e = _file->open(_filename, DebugMediaFile::Write, opts);
                if (e.isError()) {
                        _file.reset();
                        return e;
                }
                _isOpen = true;
                _isWrite = true;
                _framesWritten = 0;
                _framesRead = 0;

                // Forward the upstream shape unchanged — PMDF captures
                // whatever it's given.
                resolvedDesc = cmd.pendingMediaDesc;
                resolvedFps = cmd.pendingMediaDesc.frameRate();
        } else {
                Error e = _file->open(_filename, DebugMediaFile::Read);
                if (e.isError()) {
                        _file.reset();
                        return e;
                }
                _isOpen = true;
                _isWrite = false;
                _framesRead = 0;
                _framesWritten = 0;

                // Peek frame 0 so the MediaIO layer can report mediaDesc /
                // audioDesc / frameRate without the caller having to read
                // the first frame first.  The pipeline planner relies on
                // this to discover the produced MediaDesc of a PMDF source
                // stage before the pipeline is opened.  We rewind the file
                // cursor to frame 0 so the caller's first readFrame() still
                // sees the peeked frame.
                FrameCount fc = _file->frameCount();
                if (fc.isFinite() && fc.value() > 0) {
                        Frame::Ptr firstFrame;
                        Error      pe = _file->readFrameAt(FrameNumber(0), firstFrame);
                        if (pe.isOk() && firstFrame.isValid()) {
                                resolvedDesc = firstFrame->mediaDesc();
                                resolvedFps = resolvedDesc.frameRate();
                                // Rewind — readFrame() advanced past the peek;
                                // seek back so the first real read returns frame 0.
                                (void)_file->seek(FrameNumber(0));
                        } else if (pe.isError()) {
                                promekiWarn("DebugMediaMediaIO: peek of frame 0 "
                                            "failed on '%s' (%s); mediaDesc will be "
                                            "empty until the first readFrame()",
                                            _filename.cstr(), pe.name().cstr());
                        }
                }
        }

        MediaIOPortGroup *group = addPortGroup("debugmedia");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(resolvedFps);
        group->setCanSeek(!isWrite);
        group->setFrameCount(isWrite ? MediaIO::FrameCountInfinite : _file->frameCount());
        if (isWrite) {
                if (addSink(group, resolvedDesc) == nullptr) return Error::Invalid;
        } else {
                if (addSource(group, resolvedDesc) == nullptr) return Error::Invalid;
        }
        return Error::Ok;
}

Error DebugMediaMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_file) {
                _file->close();
                _file.reset();
        }
        _filename.clear();
        _isOpen = false;
        _isWrite = false;
        _framesWritten = 0;
        _framesRead = 0;
        return Error::Ok;
}

Error DebugMediaMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_isOpen || _isWrite || !_file) return Error::NotOpen;

        Frame::Ptr frame;
        Error      e = _file->readFrame(frame);
        if (e == Error::EndOfFile) {
                return Error::EndOfFile;
        }
        if (e.isError()) {
                return e;
        }

        ++_framesRead;
        cmd.frame = std::move(frame);
        cmd.currentFrame = toFrameNumber(_framesRead);
        return Error::Ok;
}

Error DebugMediaMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!_isOpen || !_isWrite || !_file) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;

        Error e = _file->writeFrame(cmd.frame);
        if (e.isError()) {
                return e;
        }

        ++_framesWritten;
        cmd.currentFrame = toFrameNumber(_framesWritten);
        cmd.frameCount = _framesWritten;
        return Error::Ok;
}

Error DebugMediaMediaIO::executeCmd(MediaIOCommandSeek &cmd) {
        if (!_isOpen || _isWrite || !_file) return Error::NotOpen;
        FrameNumber target = cmd.frameNumber.isValid() ? cmd.frameNumber : FrameNumber(0);
        Error       e = _file->seek(target);
        if (e.isError()) return e;
        cmd.currentFrame = cmd.frameNumber;
        _framesRead = toFrameCount(target);
        return Error::Ok;
}

Error DebugMediaMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesWritten, _framesWritten);
        cmd.stats.set(StatsFramesRead, _framesRead);
        return Error::Ok;
}

// ---- Introspection / negotiation ----
//
// PMDF is format-agnostic: it captures whatever frame it's given and
// plays it back byte-perfect.  Both proposeInput and proposeOutput
// accept-as-is.

Error DebugMediaMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        *preferred = offered;
        return Error::Ok;
}

Error DebugMediaMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const {
        if (achievable == nullptr) return Error::Invalid;
        *achievable = requested;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
