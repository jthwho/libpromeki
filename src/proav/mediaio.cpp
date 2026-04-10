/**
 * @file      mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaio.h>
#include <promeki/mediaiotask.h>
#include <promeki/threadpool.h>
#include <promeki/file.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIO)

// ============================================================================
// Format registry
// ============================================================================

static MediaIO::FormatDescList &formatRegistry() {
        static MediaIO::FormatDescList list;
        return list;
}

ThreadPool &MediaIO::pool() {
        static ThreadPool p;
        return p;
}

int MediaIO::registerFormat(const FormatDesc &desc) {
        FormatDescList &list = formatRegistry();
        int ret = list.size();
        list.pushToBack(desc);
        promekiDebug("Registered MediaIO '%s'", desc.name.cstr());
        return ret;
}

const MediaIO::FormatDescList &MediaIO::registeredFormats() {
        return formatRegistry();
}

static const MediaIO::FormatDesc *findFormatByName(const String &name) {
        const MediaIO::FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                if(desc.name == name) return &desc;
        }
        return nullptr;
}

static String extractExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos || dot + 1 >= filename.size()) return String();
        return filename.mid(dot + 1).toLower();
}

static const MediaIO::FormatDesc *findFormatByExtension(const String &filename) {
        String ext = extractExtension(filename);
        if(ext.isEmpty()) return nullptr;
        const MediaIO::FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                for(const auto &e : desc.extensions) {
                        if(ext == e) return &desc;
                }
        }
        return nullptr;
}

static const MediaIO::FormatDesc *findFormatForFileRead(const String &filename) {
        String ext = extractExtension(filename);
        const MediaIO::FormatDescList &list = formatRegistry();

        // Pass 1: extension match (fast path)
        if(!ext.isEmpty()) {
                for(const auto &desc : list) {
                        if(!desc.canRead) continue;
                        for(const auto &e : desc.extensions) {
                                if(ext == e) return &desc;
                        }
                }
        }

        // Pass 2: content-based probe
        File probeFile(filename);
        if(probeFile.open(IODevice::ReadOnly).isError()) return nullptr;
        const MediaIO::FormatDesc *result = nullptr;
        for(const auto &desc : list) {
                if(!desc.canRead) continue;
                if(!desc.canHandleDevice) continue;
                probeFile.seek(0);
                if(desc.canHandleDevice(&probeFile)) {
                        result = &desc;
                        break;
                }
        }
        probeFile.close();
        return result;
}

// ============================================================================
// Factory
// ============================================================================

MediaIO::Config MediaIO::defaultConfig(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->configSpecs) return Config();
        Config cfg;
        cfg.setValidation(SpecValidation::None);
        Config::SpecMap specs = desc->configSpecs();
        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                const Variant &def = it->second.defaultValue();
                if(def.isValid()) cfg.set(it->first, def);
        }
        cfg.setValidation(SpecValidation::Warn);
        cfg.set(MediaConfig::Type, typeName);
        return cfg;
}

MediaIO::Config::SpecMap MediaIO::configSpecs(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->configSpecs) return Config::SpecMap();
        return desc->configSpecs();
}

Metadata MediaIO::defaultMetadata(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->defaultMetadata) return Metadata();
        return desc->defaultMetadata();
}

MediaIO *MediaIO::create(const Config &config, ObjectBase *parent) {
        const FormatDesc *desc = nullptr;

        if(config.contains(MediaConfig::Type)) {
                String typeName = config.getAs<String>(MediaConfig::Type);
                desc = findFormatByName(typeName);
                if(desc == nullptr) {
                        promekiWarn("MediaIO::create: unknown type '%s'", typeName.cstr());
                        return nullptr;
                }
        }

        if(desc == nullptr && config.contains(MediaConfig::Filename)) {
                String filename = config.getAs<String>(MediaConfig::Filename);
                desc = findFormatByExtension(filename);
                if(desc == nullptr) {
                        promekiWarn("MediaIO::create: no backend for '%s'", filename.cstr());
                        return nullptr;
                }
        }

        if(desc == nullptr) {
                promekiWarn("MediaIO::create: config has neither Type nor Filename");
                return nullptr;
        }

        MediaIOTask *task = desc->create();
        if(task == nullptr) {
                promekiWarn("MediaIO::create: factory for '%s' returned null", desc->name.cstr());
                return nullptr;
        }
        MediaIO *io = new MediaIO(parent);
        io->_task = task;
        io->_config = config;
        return io;
}

MediaIO *MediaIO::createForFileRead(const String &filename, ObjectBase *parent) {
        const FormatDesc *desc = findFormatForFileRead(filename);
        if(desc == nullptr) {
                promekiWarn("MediaIO::createForFileRead: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if(!desc->canRead) {
                promekiWarn("MediaIO::createForFileRead: '%s' does not support reading", desc->name.cstr());
                return nullptr;
        }
        MediaIOTask *task = desc->create();
        if(task == nullptr) return nullptr;
        MediaIO *io = new MediaIO(parent);
        io->_task = task;
        // Seed the live config with the resolved backend name + the
        // file the caller passed in, so downstream consumers that
        // need to know "which backend is this?" can read it back
        // from io->config() without a second registry walk.
        io->_config = desc->configSpecs ? defaultConfig(desc->name) : Config();
        io->_config.set(MediaConfig::Type, desc->name);
        io->_config.set(MediaConfig::Filename, filename);
        return io;
}

MediaIO *MediaIO::createForFileWrite(const String &filename, ObjectBase *parent) {
        const FormatDesc *desc = findFormatByExtension(filename);
        if(desc == nullptr) {
                promekiWarn("MediaIO::createForFileWrite: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if(!desc->canWrite) {
                promekiWarn("MediaIO::createForFileWrite: '%s' does not support writing", desc->name.cstr());
                return nullptr;
        }
        MediaIOTask *task = desc->create();
        if(task == nullptr) return nullptr;
        MediaIO *io = new MediaIO(parent);
        io->_task = task;
        // Same rationale as createForFileRead: seed the live config
        // with the backend's full default schema plus the type and
        // filename so callers that read io->config() back out see a
        // complete, discoverable picture.
        io->_config = desc->configSpecs ? defaultConfig(desc->name) : Config();
        io->_config.set(MediaConfig::Type, desc->name);
        io->_config.set(MediaConfig::Filename, filename);
        return io;
}

StringList MediaIO::enumerate(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->enumerate) return StringList();
        return desc->enumerate();
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIO::MediaIO(ObjectBase *parent) : ObjectBase(parent) {}

Error MediaIO::adoptTask(MediaIOTask *task) {
        if(isOpen()) return Error::AlreadyOpen;
        if(task == nullptr) return Error::Invalid;
        if(_task != nullptr) return Error::Invalid;
        _task = task;
        return Error::Ok;
}

MediaIO::~MediaIO() {
        if(isOpen()) close();
        // Wait for any in-flight strand task to complete before deleting
        // the task.  The Strand destructor would also wait, but doing it
        // here makes the order explicit: drain the strand first, then
        // delete the task.
        _strand.waitForIdle();
        delete _task;
}

// ============================================================================
// Command dispatch
// ============================================================================

Error MediaIO::dispatchCommand(MediaIOCommand::Ptr cmd) {
        MediaIOCommand *raw = cmd.modify();
        switch(raw->type()) {
                case MediaIOCommand::Open:
                        return _task->executeCmd(*static_cast<MediaIOCommandOpen *>(raw));
                case MediaIOCommand::Close:
                        return _task->executeCmd(*static_cast<MediaIOCommandClose *>(raw));
                case MediaIOCommand::Read:
                        return _task->executeCmd(*static_cast<MediaIOCommandRead *>(raw));
                case MediaIOCommand::Write:
                        return _task->executeCmd(*static_cast<MediaIOCommandWrite *>(raw));
                case MediaIOCommand::Seek:
                        return _task->executeCmd(*static_cast<MediaIOCommandSeek *>(raw));
                case MediaIOCommand::Params:
                        return _task->executeCmd(*static_cast<MediaIOCommandParams *>(raw));
                case MediaIOCommand::Stats:
                        return _task->executeCmd(*static_cast<MediaIOCommandStats *>(raw));
        }
        return Error::NotSupported;
}

Error MediaIO::submitAndWait(MediaIOCommand::Ptr cmd) {
        // Submit to the strand for serialized execution and wait for the
        // result.  The strand returns a Future<Error> for the dispatched
        // call's return value.
        Future<Error> future = _strand.submit([this, cmd]() mutable {
                return dispatchCommand(cmd);
        });
        auto r = future.result();
        if(r.second().isError()) return r.second();
        return r.first();
}

void MediaIO::submitReadCommand() {
        // Atomically claim a prefetch slot.  fetchAndAdd is the
        // single-step counterpart to the loop check in readFrame() —
        // by the time we own the slot, no other code path can race
        // past the depth limit.
        _pendingReadCount.fetchAndAdd(1);

        auto *cmdRead = new MediaIOCommandRead();
        cmdRead->step = _step;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdRead);

        // Fire-and-forget: dispatch on the strand, push the result onto the
        // read result queue when done.  readFrame() consumes from there.
        _strand.submit(
                [this, cmd]() mutable {
                        MediaIOCommand *raw = cmd.modify();
                        auto *cr = static_cast<MediaIOCommandRead *>(raw);
                        cr->result = _task->executeCmd(*cr);
                        _readResultQueue.push(cmd);
                        _pendingReadCount.fetchAndSub(1);
                        // Fire on every completion — success, EOF, or
                        // error.  Signal-driven consumers need the
                        // signal for terminal results too so they can
                        // observe EOF/errors via a subsequent
                        // readFrame(..., false) call that pops the
                        // queued result.  The signal is "a read
                        // finished", not "a frame is available".
                        frameReadySignal.emit();
                },
                [this]() {
                        // Cancellation cleanup: release the slot we
                        // claimed above so the in-flight count stays
                        // accurate.
                        _pendingReadCount.fetchAndSub(1);
                });
}

// ============================================================================
// Open / Close
// ============================================================================

Error MediaIO::open(Mode mode) {
        if(isOpen()) return Error::AlreadyOpen;
        if(mode == NotOpen) return Error::InvalidArgument;
        if(_task == nullptr) return Error::Invalid;

        // Fill in the standard libpromeki write defaults (Date,
        // OriginationDateTime, Software, Originator, OriginatorReference,
        // UMID) when opening a writer.  Values already set by the caller
        // are preserved because applyMediaIOWriteDefaults() uses
        // setIfMissing internally.  The defaults are merged into both
        // the free-standing pending metadata and the media descriptor's
        // own metadata, so writer backends see the same information
        // regardless of which path they read from.
        if(mode == Writer || mode == ReadWrite) {
                _pendingMetadata.applyMediaIOWriteDefaults();
                _pendingMediaDesc.metadata().applyMediaIOWriteDefaults();
        }

        auto *cmdOpen = new MediaIOCommandOpen();
        cmdOpen->mode = mode;
        cmdOpen->config = _config;
        cmdOpen->pendingMediaDesc = _pendingMediaDesc;
        cmdOpen->pendingMetadata = _pendingMetadata;
        cmdOpen->pendingAudioDesc = _pendingAudioDesc;
        cmdOpen->videoTracks = _pendingVideoTracks;
        cmdOpen->audioTracks = _pendingAudioTracks;

        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdOpen);
        Error err = submitAndWait(cmd);
        if(err.isOk()) {
                _mode = mode;
                _mediaDesc = cmdOpen->mediaDesc;
                _audioDesc = cmdOpen->audioDesc;
                _metadata = cmdOpen->metadata;
                _frameRate = cmdOpen->frameRate;
                _canSeek = cmdOpen->canSeek;
                _frameCount = cmdOpen->frameCount;
                _currentFrame = 0;
                _step = cmdOpen->defaultStep;
                _defaultSeekMode = cmdOpen->defaultSeekMode;
                if(!_prefetchDepthExplicit) {
                        _prefetchDepth = cmdOpen->defaultPrefetchDepth;
                        if(_prefetchDepth < 1) _prefetchDepth = 1;
                }
        } else {
                // Open failed — give the task a chance to clean up any
                // partially-allocated resources via its Close handler.
                // Backends must tolerate Close from a failed-open state.
                auto *cmdClose = new MediaIOCommandClose();
                MediaIOCommand::Ptr closeCmd = MediaIOCommand::Ptr::takeOwnership(cmdClose);
                submitAndWait(closeCmd);  // ignore close error
        }
        return err;
}

Error MediaIO::close() {
        if(!isOpen()) return Error::NotOpen;

        auto *cmdClose = new MediaIOCommandClose();
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdClose);
        Error err = submitAndWait(cmd);

        // Wait for any in-flight strand tasks (e.g. trailing reads) to drain
        // before resetting state.
        _strand.waitForIdle();

        // Drain any unconsumed read results
        _readResultQueue.clear();
        _pendingReadCount.setValue(0);
        _pendingWriteCount.setValue(0);

        // Reset cache regardless of close result
        _mode = NotOpen;
        _mediaDesc = MediaDesc();
        _audioDesc = AudioDesc();
        _metadata = Metadata();
        _frameRate = FrameRate();
        _canSeek = false;
        _frameCount = 0;
        _currentFrame = 0;
        _defaultSeekMode = SeekExact;
        _prefetchDepth = 1;
        _prefetchDepthExplicit = false;
        _atEnd = false;
        return err;
}

// ============================================================================
// Pre-open setters
// ============================================================================

Error MediaIO::setMediaDesc(const MediaDesc &desc) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingMediaDesc = desc;
        return Error::Ok;
}

Error MediaIO::setAudioDesc(const AudioDesc &desc) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingAudioDesc = desc;
        return Error::Ok;
}

Error MediaIO::setMetadata(const Metadata &meta) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingMetadata = meta;
        return Error::Ok;
}

Error MediaIO::setVideoTracks(const List<int> &tracks) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingVideoTracks = tracks;
        return Error::Ok;
}

Error MediaIO::setAudioTracks(const List<int> &tracks) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingAudioTracks = tracks;
        return Error::Ok;
}

void MediaIO::setPrefetchDepth(int n) {
        if(n < 1) n = 1;
        _prefetchDepth = n;
        _prefetchDepthExplicit = true;
}

// ============================================================================
// Frame I/O
// ============================================================================

bool MediaIO::frameAvailable() const {
        // True when there's a result waiting to be consumed.
        return !_readResultQueue.isEmpty();
}

int MediaIO::readyReads() const {
        return static_cast<int>(_readResultQueue.size());
}

int MediaIO::pendingReads() const {
        return _pendingReadCount.value();
}

int MediaIO::pendingWrites() const {
        return _pendingWriteCount.value();
}

Error MediaIO::readFrame(Frame::Ptr &frame, bool block) {
        if(!isOpen()) return Error::NotOpen;
        if(_mode != Reader && _mode != ReadWrite) return Error::NotSupported;

        // Once EOF has been hit, every subsequent read returns EOF
        // without going down to the backend.  Cleared on seek/close.
        if(_atEnd) return Error::EndOfFile;

        MediaIOCommand::Ptr resultCmd;
        bool gotResult = _readResultQueue.popOrFail(resultCmd);
        if(!gotResult) {
                // Top up the in-flight read queue to the desired depth.
                while(_pendingReadCount.value() < _prefetchDepth) {
                        submitReadCommand();
                }
                if(!block) return Error::TryAgain;
                // Block on the result queue until something arrives.
                auto [popped, popErr] = _readResultQueue.pop();
                if(popErr.isError()) return popErr;
                resultCmd = popped;
        } else {
                // We just consumed a prefetched result; top up again so
                // the next call has work waiting.
                while(_pendingReadCount.value() < _prefetchDepth) {
                        submitReadCommand();
                }
        }

        auto *cmdRead = static_cast<MediaIOCommandRead *>(resultCmd.modify());

        // If the backend pushed a mid-stream descriptor change, update
        // our cache and notify listeners.  We do this BEFORE handing the
        // frame back so the user sees the new descriptors immediately.
        if(cmdRead->mediaDescChanged) {
                _mediaDesc = cmdRead->updatedMediaDesc;
                _frameRate = _mediaDesc.frameRate();
                if(!_mediaDesc.audioList().isEmpty()) {
                        _audioDesc = _mediaDesc.audioList()[0];
                }
                _metadata = _mediaDesc.metadata();
                descriptorChangedSignal.emit();
        }

        if(cmdRead->result.isOk()) {
                frame = std::move(cmdRead->frame);
                _currentFrame = cmdRead->currentFrame;
                // Stamp the current frame number into the frame's metadata
                // so downstream consumers know which frame it is.
                if(frame.isValid()) {
                        frame.modify()->metadata().set(
                                Metadata::FrameNumber, _currentFrame);
                        if(cmdRead->mediaDescChanged) {
                                frame.modify()->metadata().set(
                                        Metadata::MediaDescChanged, true);
                        }
                }
        } else if(cmdRead->result == Error::EndOfFile) {
                // Latch EOF — stop submitting prefetches.  Drain any
                // already-queued results so we don't return them after
                // signalling EOF (the backend has said it's done).
                _atEnd = true;
                _strand.cancelPending();
                MediaIOCommand::Ptr drop;
                while(_readResultQueue.popOrFail(drop)) {}
        }
        return cmdRead->result;
}

bool MediaIO::isIdle() const {
        return !_strand.isBusy();
}

size_t MediaIO::cancelPending() {
        if(!isOpen()) return 0;
        // Cancel anything queued in the strand (the Strand's per-task
        // cancel callbacks balance any reference counts on our side,
        // such as _pendingReadCount).  Any prefetched read results that
        // the worker already pushed are also discarded so the next
        // readFrame() submits fresh work.
        size_t cancelled = _strand.cancelPending();
        size_t dropped = 0;
        MediaIOCommand::Ptr drop;
        while(_readResultQueue.popOrFail(drop)) dropped++;
        return cancelled + dropped;
}

Error MediaIO::writeFrame(const Frame::Ptr &frame, bool block) {
        if(!isOpen()) return Error::NotOpen;
        if(_mode != Writer && _mode != ReadWrite) return Error::NotSupported;

        auto *cmdWrite = new MediaIOCommandWrite();
        cmdWrite->frame = frame;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdWrite);

        // Claim a pending-write slot before submit so pendingWrites()
        // reflects the new command immediately.  The strand task
        // releases the slot on completion, and the cancellation
        // callback releases it if the command is cancelled before it
        // runs.
        _pendingWriteCount.fetchAndAdd(1);

        Future<Error> future = _strand.submit(
                [this, cmd]() mutable {
                        MediaIOCommand *raw = cmd.modify();
                        auto *cw = static_cast<MediaIOCommandWrite *>(raw);
                        Error err = _task->executeCmd(*cw);
                        if(err.isOk()) frameWantedSignal.emit();
                        else            writeErrorSignal.emit(err);
                        _pendingWriteCount.fetchAndSub(1);
                        return err;
                },
                [this]() {
                        // Cancellation cleanup: balance the slot we
                        // claimed above so pendingWrites() stays
                        // accurate if the command is dropped before
                        // running.
                        _pendingWriteCount.fetchAndSub(1);
                });

        if(!block) return Error::TryAgain;

        auto r = future.result();
        if(r.second().isError()) return r.second();
        Error err = r.first();
        if(err.isOk()) {
                _currentFrame = cmdWrite->currentFrame;
                _frameCount = cmdWrite->frameCount;
        }
        return err;
}

Error MediaIO::sendParams(const String &name, const MediaIOParams &params, MediaIOParams *result) {
        if(!isOpen()) return Error::NotOpen;

        auto *cmdParams = new MediaIOCommandParams();
        cmdParams->name = name;
        cmdParams->params = params;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdParams);
        Error err = submitAndWait(cmd);
        if(result != nullptr) {
                *result = std::move(cmdParams->result);
        }
        return err;
}

MediaIOStats MediaIO::stats() {
        if(!isOpen()) return MediaIOStats();
        auto *cmdStats = new MediaIOCommandStats();
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdStats);
        Error err = submitAndWait(cmd);
        if(err.isError()) return MediaIOStats();
        return std::move(cmdStats->stats);
}

// ============================================================================
// Navigation
// ============================================================================

void MediaIO::setStep(int val) {
        if(val == _step) return;
        // Outstanding prefetched reads were submitted with the old step;
        // they're stale relative to the new direction/speed.  Cancel them
        // and discard any results that already came back.  Also clear
        // the EOF latch — the new direction may make more frames available
        // (e.g. flipping from forward-EOF to reverse).
        if(isOpen()) {
                _strand.cancelPending();
                MediaIOCommand::Ptr drop;
                while(_readResultQueue.popOrFail(drop)) {}
                _atEnd = false;
        }
        _step = val;
}

Error MediaIO::seekToFrame(int64_t frameNumber, SeekMode mode) {
        if(!isOpen()) return Error::NotOpen;
        if(!_canSeek) return Error::IllegalSeek;

        // Cancel any prefetched reads from the old position before
        // submitting the seek.  Otherwise the next read would return
        // a stale frame from the pre-seek queue.
        _strand.cancelPending();
        MediaIOCommand::Ptr drop;
        while(_readResultQueue.popOrFail(drop)) {}
        // Seeking past EOF clears the EOF latch — the new position may
        // be re-readable.
        _atEnd = false;

        // Resolve Default to the task's preferred mode so the backend
        // always sees a concrete mode.
        if(mode == SeekDefault) mode = _defaultSeekMode;

        auto *cmdSeek = new MediaIOCommandSeek();
        cmdSeek->frameNumber = frameNumber;
        cmdSeek->mode = mode;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdSeek);
        Error err = submitAndWait(cmd);
        if(err.isOk()) {
                _currentFrame = cmdSeek->currentFrame;
        }
        return err;
}

PROMEKI_NAMESPACE_END
