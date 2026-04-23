/**
 * @file      audiodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Symmetric counterpart to @ref audioencoder.cpp — owns the audio
 * decoder registry and defines the @ref AudioCodec decoder-side
 * bridge methods.
 */

#include <algorithm>
#include <promeki/audiodecoder.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/mediaconfig.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/readwritelock.h>

PROMEKI_NAMESPACE_BEGIN

AudioDecoder::~AudioDecoder() = default;

void AudioDecoder::configure(const MediaConfig &config) {
        (void)config;
}

void AudioDecoder::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void AudioDecoder::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

namespace {

struct DecoderRegistry {
        ReadWriteLock                                           mutex;
        Map<AudioCodec::ID, List<AudioDecoder::BackendRecord>>  entries;
};

DecoderRegistry &registry() {
        static DecoderRegistry inst;
        return inst;
}

void sortByWeight(List<AudioDecoder::BackendRecord> &list) {
        std::sort(list.begin(), list.end(),
                  [](const AudioDecoder::BackendRecord &a,
                     const AudioDecoder::BackendRecord &b) {
                          return a.weight > b.weight;
                  });
}

} // namespace

Error AudioDecoder::registerBackend(BackendRecord record) {
        if(!record.factory) return Error::Invalid;
        if(!record.backend.isValid()) return Error::Invalid;
        if(record.codecId == AudioCodec::Invalid) return Error::Invalid;

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

AudioCodec::BackendList AudioDecoder::availableBackends(AudioCodec::ID codecId) {
        AudioCodec::BackendList out;
        auto &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto it = reg.entries.find(codecId);
        if(it == reg.entries.end()) return out;
        for(const auto &r : it->second) out.pushToBack(r.backend);
        return out;
}

List<int> AudioDecoder::supportedOutputsFor(AudioCodec::ID codecId,
                                             AudioCodec::Backend pinned) {
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
                for(int fmt : r.supportedOutputs) acc.insert(fmt);
        }
        List<int> out;
        out.reserve(acc.size());
        for(int fmt : acc) out.pushToBack(fmt);
        return out;
}

Result<AudioDecoder *> AudioDecoder::create(AudioCodec::ID codecId,
                                             AudioCodec::Backend pinned,
                                             const MediaConfig *config) {
        auto &reg = registry();
        AudioCodec::Backend chosenTag;
        Factory factory;
        {
                ReadWriteLock::ReadLocker lock(reg.mutex);
                auto it = reg.entries.find(codecId);
                if(it == reg.entries.end() || it->second.isEmpty()) {
                        return makeError<AudioDecoder *>(Error::IdNotFound);
                }
                const auto &list = it->second;

                const BackendRecord *chosen = nullptr;
                if(pinned.isValid()) {
                        for(const auto &r : list) {
                                if(r.backend == pinned) { chosen = &r; break; }
                        }
                        if(chosen == nullptr) {
                                return makeError<AudioDecoder *>(Error::IdNotFound);
                        }
                } else {
                        if(config != nullptr) {
                                String backendName = config->getAs<String>(MediaConfig::CodecBackend);
                                if(!backendName.isEmpty()) {
                                        auto bk = AudioCodec::lookupBackend(backendName);
                                        if(!error(bk).isError()) {
                                                auto backend = value(bk);
                                                for(const auto &r : list) {
                                                        if(r.backend == backend) {
                                                                chosen = &r;
                                                                break;
                                                        }
                                                }
                                                if(chosen == nullptr) {
                                                        return makeError<AudioDecoder *>(Error::IdNotFound);
                                                }
                                        }
                                }
                        }
                        if(chosen == nullptr) chosen = &list.front();
                }
                chosenTag = chosen->backend;
                factory   = chosen->factory;
        }
        if(!factory) return makeError<AudioDecoder *>(Error::IdNotFound);

        AudioDecoder *dec = factory();
        if(dec == nullptr) return makeError<AudioDecoder *>(Error::LibraryFailure);
        dec->setCodec(AudioCodec(codecId, chosenTag));
        if(config != nullptr) dec->configure(*config);
        return makeResult(dec);
}

// ---------------------------------------------------------------------------
// AudioCodec bridge — decoder-side accessors.
// ---------------------------------------------------------------------------

bool AudioCodec::canDecode() const {
        if(!isValid()) return false;
        auto list = AudioDecoder::availableBackends(id());
        if(!_backend.isValid()) return !list.isEmpty();
        for(auto b : list) if(b == _backend) return true;
        return false;
}

AudioCodec::BackendList AudioCodec::availableDecoderBackends() const {
        if(!isValid()) return {};
        return AudioDecoder::availableBackends(id());
}

List<AudioFormat> AudioCodec::decoderSupportedOutputs() const {
        List<AudioFormat> out;
        if(!isValid()) return out;
        for(int rawId : AudioDecoder::supportedOutputsFor(id(), _backend)) {
                out.pushToBack(AudioFormat(static_cast<AudioFormat::ID>(rawId)));
        }
        return out;
}

Result<AudioDecoder *> AudioCodec::createDecoder(const MediaConfig *config) const {
        if(!isValid()) return makeError<AudioDecoder *>(Error::Invalid);
        return AudioDecoder::create(id(), _backend, config);
}

PROMEKI_NAMESPACE_END
