/**
 * @file      videoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Owns the (codec-id → backend-record list) registry that backs every
 * encoder factory in the process, defines the public
 * @ref VideoEncoder::registerBackend / @ref VideoEncoder::create
 * surface that backend source files call, and defines the
 * @ref VideoCodec bridge methods (@c canEncode, @c createEncoder,
 * @c encoderSupportedInputs, @c availableEncoderBackends,
 * @c registeredBackends) so those accessors resolve to the same registry.
 *
 * Pairing the implementation with @ref videodecoder.cpp keeps the
 * symmetric knowledge of "which backends are registered for which
 * codec" inside the proav TU without leaking it to the core
 * videocodec.cpp — when the proav feature flag is off, no encoder or
 * decoder exists, and the bridge methods simply don't link.
 */

#include <algorithm>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/videocodec.h>
#include <promeki/pixelformat.h>
#include <promeki/mediaconfig.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/readwritelock.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// VideoEncoder base — destructor, configure default, error plumbing.
// ---------------------------------------------------------------------------

VideoEncoder::~VideoEncoder() = default;

void VideoEncoder::configure(const MediaConfig &config) {
        (void)config;
}

void VideoEncoder::requestKeyframe() {
        // Default: no-op — intra-only codecs don't need it, and pipeline
        // glue should be able to call this unconditionally.
}

void VideoEncoder::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void VideoEncoder::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

// ---------------------------------------------------------------------------
// Backend-record registry — private to this TU.
// ---------------------------------------------------------------------------

namespace {

struct EncoderRegistry {
        ReadWriteLock                                   mutex;
        Map<VideoCodec::ID, List<VideoEncoder::BackendRecord>> entries;
};

EncoderRegistry &registry() {
        static EncoderRegistry inst;
        return inst;
}

void sortByWeight(List<VideoEncoder::BackendRecord> &list) {
        std::sort(list.begin(), list.end(),
                  [](const VideoEncoder::BackendRecord &a,
                     const VideoEncoder::BackendRecord &b) {
                          return a.weight > b.weight;
                  });
}

} // namespace

// ---------------------------------------------------------------------------
// Public registration surface.
// ---------------------------------------------------------------------------

Error VideoEncoder::registerBackend(BackendRecord record) {
        if(!record.factory) return Error::Invalid;
        if(!record.backend.isValid()) return Error::Invalid;
        if(record.codecId == VideoCodec::Invalid) return Error::Invalid;

        auto &reg = registry();
        ReadWriteLock::WriteLocker lock(reg.mutex);
        auto &list = reg.entries[record.codecId];
        for(auto &existing : list) {
                if(existing.backend == record.backend) {
                        existing = std::move(record);
                        sortByWeight(list);
                        return Error::Ok;
                }
        }
        list.pushToBack(std::move(record));
        sortByWeight(list);
        return Error::Ok;
}

VideoCodec::BackendList VideoEncoder::availableBackends(VideoCodec::ID codecId) {
        VideoCodec::BackendList out;
        auto &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto it = reg.entries.find(codecId);
        if(it == reg.entries.end()) return out;
        for(const auto &r : it->second) out.pushToBack(r.backend);
        return out;
}

List<int> VideoEncoder::supportedInputsFor(VideoCodec::ID codecId,
                                            VideoCodec::Backend pinned) {
        auto &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto it = reg.entries.find(codecId);
        if(it == reg.entries.end()) return {};
        if(pinned.isValid()) {
                for(const auto &r : it->second) {
                        if(r.backend == pinned) return r.supportedInputs;
                }
                return {};
        }
        // Union across every registered backend for this codec.
        Set<int> acc;
        for(const auto &r : it->second) {
                for(int pd : r.supportedInputs) acc.insert(pd);
        }
        List<int> out;
        out.reserve(acc.size());
        for(int pd : acc) out.pushToBack(pd);
        return out;
}

Result<VideoEncoder *> VideoEncoder::create(VideoCodec::ID codecId,
                                             VideoCodec::Backend pinned,
                                             const MediaConfig *config) {
        auto &reg = registry();
        const BackendRecord *chosen = nullptr;
        VideoCodec::Backend chosenTag;
        {
                ReadWriteLock::ReadLocker lock(reg.mutex);
                auto it = reg.entries.find(codecId);
                if(it == reg.entries.end() || it->second.isEmpty()) {
                        return makeError<VideoEncoder *>(Error::IdNotFound);
                }
                const auto &list = it->second;

                if(pinned.isValid()) {
                        for(const auto &r : list) {
                                if(r.backend == pinned) { chosen = &r; break; }
                        }
                        if(chosen == nullptr) {
                                return makeError<VideoEncoder *>(Error::IdNotFound);
                        }
                } else {
                        // Consult MediaConfig::CodecBackend as a soft pin.
                        if(config != nullptr) {
                                String backendName = config->getAs<String>(MediaConfig::CodecBackend);
                                if(!backendName.isEmpty()) {
                                        auto bk = VideoCodec::lookupBackend(backendName);
                                        if(!error(bk).isError()) {
                                                auto backend = value(bk);
                                                for(const auto &r : list) {
                                                        if(r.backend == backend) {
                                                                chosen = &r;
                                                                break;
                                                        }
                                                }
                                                if(chosen == nullptr) {
                                                        return makeError<VideoEncoder *>(Error::IdNotFound);
                                                }
                                        }
                                }
                        }
                        if(chosen == nullptr) chosen = &list.front();
                }
                chosenTag = chosen->backend;
                // Copy the factory out of the record so we can release
                // the lock before invoking it (which may do arbitrary
                // work, including further registry walks).
        }

        // Re-lock briefly to pull a stable copy of the factory.
        Factory factory;
        {
                ReadWriteLock::ReadLocker lock(reg.mutex);
                auto it = reg.entries.find(codecId);
                if(it == reg.entries.end()) {
                        return makeError<VideoEncoder *>(Error::IdNotFound);
                }
                for(const auto &r : it->second) {
                        if(r.backend == chosenTag) { factory = r.factory; break; }
                }
        }
        if(!factory) return makeError<VideoEncoder *>(Error::IdNotFound);

        VideoEncoder *enc = factory();
        if(enc == nullptr) return makeError<VideoEncoder *>(Error::LibraryFailure);
        enc->setCodec(VideoCodec(codecId, chosenTag));
        if(config != nullptr) enc->configure(*config);
        return makeResult(enc);
}

// ---------------------------------------------------------------------------
// VideoCodec bridge — encoder-side accessors.
// ---------------------------------------------------------------------------

bool VideoCodec::canEncode() const {
        if(!isValid()) return false;
        auto list = VideoEncoder::availableBackends(id());
        if(!_backend.isValid()) return !list.isEmpty();
        for(auto b : list) if(b == _backend) return true;
        return false;
}

VideoCodec::BackendList VideoCodec::availableEncoderBackends() const {
        if(!isValid()) return {};
        return VideoEncoder::availableBackends(id());
}

List<PixelFormat> VideoCodec::encoderSupportedInputs() const {
        List<PixelFormat> out;
        if(!isValid()) return out;
        for(int rawId : VideoEncoder::supportedInputsFor(id(), _backend)) {
                out.pushToBack(PixelFormat(static_cast<PixelFormat::ID>(rawId)));
        }
        return out;
}

List<PixelFormat> VideoCodec::compressedPixelFormats() const {
        List<PixelFormat> out;
        if(!isValid()) return out;
        for(int rawId : d->compressedPixelFormats) {
                out.pushToBack(PixelFormat(static_cast<PixelFormat::ID>(rawId)));
        }
        return out;
}

Result<VideoEncoder *> VideoCodec::createEncoder(const MediaConfig *config) const {
        if(!isValid()) return makeError<VideoEncoder *>(Error::Invalid);
        return VideoEncoder::create(id(), _backend, config);
}

// ---------------------------------------------------------------------------
// Union over both encoder and decoder registries.
// ---------------------------------------------------------------------------

VideoCodec::BackendList VideoCodec::registeredBackends() {
        Set<uint64_t> seen;
        BackendList out;
        for(auto cid : registeredIDs()) {
                VideoCodec vc(cid);
                for(auto b : vc.availableEncoderBackends()) {
                        if(seen.insert(b.id()).second()) out.pushToBack(b);
                }
                for(auto b : vc.availableDecoderBackends()) {
                        if(seen.insert(b.id()).second()) out.pushToBack(b);
                }
        }
        return out;
}

PROMEKI_NAMESPACE_END
