/**
 * @file      audioencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Owns the (codec-id → backend-record list) registry that backs every
 * audio encoder factory in the process, defines the public
 * @ref AudioEncoder::registerBackend / @ref AudioEncoder::create
 * surface that backend source files call, and defines the
 * @ref AudioCodec bridge methods so those accessors resolve to the
 * same registry.
 */

#include <algorithm>
#include <promeki/audioencoder.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/mediaconfig.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/readwritelock.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// AudioEncoder base — destructor, configure default, error plumbing.
// ---------------------------------------------------------------------------

AudioEncoder::~AudioEncoder() = default;

void AudioEncoder::configure(const MediaConfig &config) {
        (void)config;
}

void AudioEncoder::requestKeyframe() {
        // Default: no-op — most audio codecs are packet-independent.
        // Codecs with internal state override to force a fresh run.
}

void AudioEncoder::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void AudioEncoder::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

// ---------------------------------------------------------------------------
// Backend-record registry — private to this TU.
// ---------------------------------------------------------------------------

namespace {

        struct EncoderRegistry {
                        ReadWriteLock                                          mutex;
                        Map<AudioCodec::ID, List<AudioEncoder::BackendRecord>> entries;
        };

        EncoderRegistry &registry() {
                static EncoderRegistry inst;
                return inst;
        }

        void sortByWeight(List<AudioEncoder::BackendRecord> &list) {
                std::sort(list.begin(), list.end(),
                          [](const AudioEncoder::BackendRecord &a, const AudioEncoder::BackendRecord &b) {
                                  return a.weight > b.weight;
                          });
        }

} // namespace

// ---------------------------------------------------------------------------
// Public registration + query surface.
// ---------------------------------------------------------------------------

Error AudioEncoder::registerBackend(BackendRecord record) {
        if (!record.factory) return Error::Invalid;
        if (!record.backend.isValid()) return Error::Invalid;
        if (record.codecId == AudioCodec::Invalid) return Error::Invalid;

        auto                      &reg = registry();
        ReadWriteLock::WriteLocker lock(reg.mutex);
        auto                      &list = reg.entries[record.codecId];
        for (auto &existing : list) {
                if (existing.backend == record.backend) {
                        existing = std::move(record);
                        sortByWeight(list);
                        return Error::Ok;
                }
        }
        list.pushToBack(std::move(record));
        sortByWeight(list);
        return Error::Ok;
}

AudioCodec::BackendList AudioEncoder::availableBackends(AudioCodec::ID codecId) {
        AudioCodec::BackendList   out;
        auto                     &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto                      it = reg.entries.find(codecId);
        if (it == reg.entries.end()) return out;
        for (const auto &r : it->second) out.pushToBack(r.backend);
        return out;
}

List<int> AudioEncoder::supportedInputsFor(AudioCodec::ID codecId, AudioCodec::Backend pinned) {
        auto                     &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        auto                      it = reg.entries.find(codecId);
        if (it == reg.entries.end()) return {};
        if (pinned.isValid()) {
                for (const auto &r : it->second) {
                        if (r.backend == pinned) return r.supportedInputs;
                }
                return {};
        }
        Set<int> acc;
        for (const auto &r : it->second) {
                for (int fmt : r.supportedInputs) acc.insert(fmt);
        }
        List<int> out;
        out.reserve(acc.size());
        for (int fmt : acc) out.pushToBack(fmt);
        return out;
}

Result<AudioEncoder *> AudioEncoder::create(AudioCodec::ID codecId, AudioCodec::Backend pinned,
                                            const MediaConfig *config) {
        auto               &reg = registry();
        AudioCodec::Backend chosenTag;
        Factory             factory;
        {
                ReadWriteLock::ReadLocker lock(reg.mutex);
                auto                      it = reg.entries.find(codecId);
                if (it == reg.entries.end() || it->second.isEmpty()) {
                        return makeError<AudioEncoder *>(Error::IdNotFound);
                }
                const auto &list = it->second;

                const BackendRecord *chosen = nullptr;
                if (pinned.isValid()) {
                        for (const auto &r : list) {
                                if (r.backend == pinned) {
                                        chosen = &r;
                                        break;
                                }
                        }
                        if (chosen == nullptr) {
                                return makeError<AudioEncoder *>(Error::IdNotFound);
                        }
                } else {
                        if (config != nullptr) {
                                String backendName = config->getAs<String>(MediaConfig::CodecBackend);
                                if (!backendName.isEmpty()) {
                                        auto bk = AudioCodec::lookupBackend(backendName);
                                        if (!error(bk).isError()) {
                                                auto backend = value(bk);
                                                for (const auto &r : list) {
                                                        if (r.backend == backend) {
                                                                chosen = &r;
                                                                break;
                                                        }
                                                }
                                                if (chosen == nullptr) {
                                                        return makeError<AudioEncoder *>(Error::IdNotFound);
                                                }
                                        }
                                }
                        }
                        if (chosen == nullptr) chosen = &list.front();
                }
                chosenTag = chosen->backend;
                factory = chosen->factory;
        }
        if (!factory) return makeError<AudioEncoder *>(Error::IdNotFound);

        AudioEncoder *enc = factory();
        if (enc == nullptr) return makeError<AudioEncoder *>(Error::LibraryFailure);
        enc->setCodec(AudioCodec(codecId, chosenTag));
        if (config != nullptr) enc->configure(*config);
        return makeResult(enc);
}

// ---------------------------------------------------------------------------
// AudioCodec bridge — encoder-side accessors + registeredBackends().
// ---------------------------------------------------------------------------

bool AudioCodec::canEncode() const {
        if (!isValid()) return false;
        auto list = AudioEncoder::availableBackends(id());
        if (!_backend.isValid()) return !list.isEmpty();
        for (auto b : list)
                if (b == _backend) return true;
        return false;
}

AudioCodec::BackendList AudioCodec::availableEncoderBackends() const {
        if (!isValid()) return {};
        return AudioEncoder::availableBackends(id());
}

List<AudioFormat> AudioCodec::encoderSupportedInputs() const {
        List<AudioFormat> out;
        if (!isValid()) return out;
        for (int rawId : AudioEncoder::supportedInputsFor(id(), _backend)) {
                out.pushToBack(AudioFormat(static_cast<AudioFormat::ID>(rawId)));
        }
        return out;
}

List<AudioFormat> AudioCodec::supportedSampleFormats() const {
        List<AudioFormat> out;
        if (!isValid()) return out;
        for (int rawId : d->supportedSampleFormats) {
                out.pushToBack(AudioFormat(static_cast<AudioFormat::ID>(rawId)));
        }
        return out;
}

Result<AudioEncoder *> AudioCodec::createEncoder(const MediaConfig *config) const {
        if (!isValid()) return makeError<AudioEncoder *>(Error::Invalid);
        return AudioEncoder::create(id(), _backend, config);
}

AudioCodec::BackendList AudioCodec::registeredBackends() {
        Set<uint64_t> seen;
        BackendList   out;
        for (auto cid : registeredIDs()) {
                AudioCodec ac(cid);
                for (auto b : ac.availableEncoderBackends()) {
                        if (seen.insert(b.id()).second()) out.pushToBack(b);
                }
                for (auto b : ac.availableDecoderBackends()) {
                        if (seen.insert(b.id()).second()) out.pushToBack(b);
                }
        }
        return out;
}

PROMEKI_NAMESPACE_END
