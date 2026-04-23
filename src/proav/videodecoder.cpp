/**
 * @file      videodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Symmetric counterpart to @ref videoencoder.cpp — owns the decoder
 * registry and defines @ref VideoCodec::canDecode /
 * @ref VideoCodec::createDecoder / @ref VideoCodec::decoderSupportedOutputs /
 * @ref VideoCodec::availableDecoderBackends against it.
 */

#include <algorithm>
#include <promeki/videodecoder.h>
#include <promeki/videocodec.h>
#include <promeki/pixelformat.h>
#include <promeki/mediaconfig.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/readwritelock.h>

PROMEKI_NAMESPACE_BEGIN

VideoDecoder::~VideoDecoder() = default;

void VideoDecoder::configure(const MediaConfig &config) {
        (void)config;
}

void VideoDecoder::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void VideoDecoder::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

namespace {

struct DecoderRegistry {
        ReadWriteLock                                   mutex;
        Map<VideoCodec::ID, List<VideoDecoder::BackendRecord>> entries;
};

DecoderRegistry &registry() {
        static DecoderRegistry inst;
        return inst;
}

void sortByWeight(List<VideoDecoder::BackendRecord> &list) {
        std::sort(list.begin(), list.end(),
                  [](const VideoDecoder::BackendRecord &a,
                     const VideoDecoder::BackendRecord &b) {
                          return a.weight > b.weight;
                  });
}

} // namespace

Error VideoDecoder::registerBackend(BackendRecord record) {
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

VideoCodec::BackendList VideoDecoder::availableBackends(VideoCodec::ID codecId) {
        VideoCodec::BackendList out;
        auto &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto it = reg.entries.find(codecId);
        if(it == reg.entries.end()) return out;
        for(const auto &r : it->second) out.pushToBack(r.backend);
        return out;
}

List<int> VideoDecoder::supportedOutputsFor(VideoCodec::ID codecId,
                                             VideoCodec::Backend pinned) {
        auto &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto it = reg.entries.find(codecId);
        if(it == reg.entries.end()) return {};
        if(pinned.isValid()) {
                for(const auto &r : it->second) {
                        if(r.backend == pinned) return r.supportedOutputs;
                }
                return {};
        }
        Set<int> acc;
        for(const auto &r : it->second) {
                for(int pd : r.supportedOutputs) acc.insert(pd);
        }
        List<int> out;
        out.reserve(acc.size());
        for(int pd : acc) out.pushToBack(pd);
        return out;
}

Result<VideoDecoder *> VideoDecoder::create(VideoCodec::ID codecId,
                                             VideoCodec::Backend pinned,
                                             const MediaConfig *config) {
        auto &reg = registry();
        VideoCodec::Backend chosenTag;
        Factory factory;
        {
                ReadWriteLock::ReadLocker lock(reg.mutex);
                auto it = reg.entries.find(codecId);
                if(it == reg.entries.end() || it->second.isEmpty()) {
                        return makeError<VideoDecoder *>(Error::IdNotFound);
                }
                const auto &list = it->second;

                const BackendRecord *chosen = nullptr;
                if(pinned.isValid()) {
                        for(const auto &r : list) {
                                if(r.backend == pinned) { chosen = &r; break; }
                        }
                        if(chosen == nullptr) {
                                return makeError<VideoDecoder *>(Error::IdNotFound);
                        }
                } else {
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
                                                        return makeError<VideoDecoder *>(Error::IdNotFound);
                                                }
                                        }
                                }
                        }
                        if(chosen == nullptr) chosen = &list.front();
                }
                chosenTag = chosen->backend;
                factory   = chosen->factory;
        }
        if(!factory) return makeError<VideoDecoder *>(Error::IdNotFound);

        VideoDecoder *dec = factory();
        if(dec == nullptr) return makeError<VideoDecoder *>(Error::LibraryFailure);
        dec->setCodec(VideoCodec(codecId, chosenTag));
        if(config != nullptr) dec->configure(*config);
        return makeResult(dec);
}

// ---------------------------------------------------------------------------
// VideoCodec bridge — decoder-side accessors.
// ---------------------------------------------------------------------------

bool VideoCodec::canDecode() const {
        if(!isValid()) return false;
        auto list = VideoDecoder::availableBackends(id());
        if(!_backend.isValid()) return !list.isEmpty();
        for(auto b : list) if(b == _backend) return true;
        return false;
}

VideoCodec::BackendList VideoCodec::availableDecoderBackends() const {
        if(!isValid()) return {};
        return VideoDecoder::availableBackends(id());
}

List<PixelFormat> VideoCodec::decoderSupportedOutputs() const {
        List<PixelFormat> out;
        if(!isValid()) return out;
        for(int rawId : VideoDecoder::supportedOutputsFor(id(), _backend)) {
                out.pushToBack(PixelFormat(static_cast<PixelFormat::ID>(rawId)));
        }
        return out;
}

Result<VideoDecoder *> VideoCodec::createDecoder(const MediaConfig *config) const {
        if(!isValid()) return makeError<VideoDecoder *>(Error::Invalid);
        return VideoDecoder::create(id(), _backend, config);
}

PROMEKI_NAMESPACE_END
