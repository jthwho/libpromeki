/**
 * @file      transcriptionengine.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Owns the global TranscriptionEngine backend registry and defines the
 * base-class behaviour shared by every concrete backend: the configure
 * stash, the error plumbing, and the Frame-shaped helpers that produce
 * subtitle-bearing output Frames echoing the source's payloads through
 * unchanged.
 */

#include <promeki/transcriptionengine.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/readwritelock.h>
#include <promeki/transcript.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// TranscriptionEngine base — destructor, configure plumbing, error plumbing.
// ---------------------------------------------------------------------------

TranscriptionEngine::~TranscriptionEngine() = default;

void TranscriptionEngine::configure(const MediaConfig &config) {
        if (!_stashedConfig.isValid()) {
                _stashedConfig = UniquePtr<MediaConfig>::create(config);
        } else {
                *_stashedConfig = config;
        }
        onConfigure(config);
}

void TranscriptionEngine::onConfigure(const MediaConfig &config) {
        (void)config;
}

const MediaConfig &TranscriptionEngine::config() const {
        static const MediaConfig empty;
        return _stashedConfig.isValid() ? *_stashedConfig : empty;
}

void TranscriptionEngine::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void TranscriptionEngine::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

// ---------------------------------------------------------------------------
// Frame-shaped helpers — shared algorithms for concrete backends.
// ---------------------------------------------------------------------------

PcmAudioPayload::Ptr TranscriptionEngine::selectInputPayload(const Frame &frame, int streamIndex) {
        if (!frame.isValid()) return PcmAudioPayload::Ptr();
        for (const AudioPayload::Ptr &ap : frame.audioPayloads()) {
                if (!ap.isValid()) continue;
                if (streamIndex >= 0 && ap->streamIndex() != streamIndex) continue;
                PcmAudioPayload::Ptr pp = sharedPointerCast<PcmAudioPayload>(ap);
                if (pp.isNull()) continue;
                return pp;
        }
        return PcmAudioPayload::Ptr();
}

Frame TranscriptionEngine::buildOutputFrame(const Frame &source, const Transcript &transcript) {
        Frame out;
        if (source.isValid()) {
                out.metadata() = source.metadata();
                out.setCaptureTime(source.captureTime());
                // Transcription is additive: echo every source payload
                // through unchanged.  Unlike AudioEncoder, we do not
                // replace the audio we transcribed — downstream stages
                // still need it for playback / further processing.
                for (const VideoPayload::Ptr &vp : source.videoPayloads()) {
                        if (vp.isValid()) out.addPayload(vp);
                }
                for (const AudioPayload::Ptr &ap : source.audioPayloads()) {
                        if (ap.isValid()) out.addPayload(ap);
                }
                for (const AncPayload::Ptr &anc : source.ancPayloads()) {
                        if (anc.isValid()) out.addPayload(anc);
                }
        }
        // Partial vs. finalised lives on the transcript itself
        // (Transcript::partial) so the marker travels with the
        // utterance when it leaves the Frame.
        out.metadata().set(Metadata::Transcript, Variant(transcript));
        return out;
}

TimeStamp TranscriptionEngine::wordTimestamp(const PcmAudioPayload &payload, size_t sampleOffset) {
        const MediaTimeStamp &pts = payload.pts();
        if (!pts.timeStamp().isValid()) return TimeStamp();
        const float sampleRate = payload.desc().sampleRate();
        if (sampleRate <= 0.0f) return TimeStamp();
        // Compute the offset in nanoseconds with 64-bit math; multiply
        // before divide so 1 sample at 48 kHz lands on an exact 20833 ns
        // increment rather than rounding through float.
        const int64_t offsetNs = static_cast<int64_t>(
                (static_cast<double>(sampleOffset) * 1'000'000'000.0) / static_cast<double>(sampleRate));
        return TimeStamp(pts.timeStamp().nanoseconds() + offsetNs);
}

// ---------------------------------------------------------------------------
// Backend-record registry — private to this TU.
// ---------------------------------------------------------------------------

namespace {

        struct EngineRegistry {
                        ReadWriteLock                            mutex;
                        List<TranscriptionEngine::BackendRecord> records;
        };

        EngineRegistry &registry() {
                static EngineRegistry inst;
                return inst;
        }

        // Sort by descending weight so the first match wins.  Stable
        // relative to insertion order for equal-weight entries.
        void sortByWeight(List<TranscriptionEngine::BackendRecord> &list) {
                list.sortInPlace(
                        [](const TranscriptionEngine::BackendRecord &a,
                           const TranscriptionEngine::BackendRecord &b) {
                                return a.weight > b.weight;
                        });
        }

} // namespace

// ---------------------------------------------------------------------------
// Public registration + query surface.
// ---------------------------------------------------------------------------

Error TranscriptionEngine::registerBackend(BackendRecord record) {
        if (record.name.isEmpty()) {
                promekiWarn("TranscriptionEngine::registerBackend: empty backend name");
                return Error::Invalid;
        }
        if (!record.factory) {
                promekiWarn("TranscriptionEngine::registerBackend: null factory for backend '%s'",
                            record.name.cstr());
                return Error::Invalid;
        }
        auto                      &reg = registry();
        ReadWriteLock::WriteLocker lock(reg.mutex);
        // Replace existing entry with the same name (idempotent re-registration).
        for (auto &existing : reg.records) {
                if (existing.name == record.name) {
                        existing = std::move(record);
                        sortByWeight(reg.records);
                        return Error::Ok;
                }
        }
        reg.records.pushToBack(std::move(record));
        sortByWeight(reg.records);
        return Error::Ok;
}

List<TranscriptionEngine::BackendRecord> TranscriptionEngine::registeredBackends() {
        auto                     &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        return reg.records;
}

Result<TranscriptionEngine::BackendRecord> TranscriptionEngine::lookupBackend(const String &name) {
        auto                     &reg = registry();
        ReadWriteLock::ReadLocker lock(reg.mutex);
        for (const auto &r : reg.records) {
                if (r.name == name) return makeResult(r);
        }
        return makeError<BackendRecord>(Error::IdNotFound);
}

Result<TranscriptionEngine::UPtr> TranscriptionEngine::create(const String &name,
                                                              const MediaConfig *config) {
        Factory factory;
        String  chosenName;
        List<int> supportedModes;
        {
                auto                     &reg = registry();
                ReadWriteLock::ReadLocker lock(reg.mutex);
                const BackendRecord      *chosen = nullptr;
                for (const auto &r : reg.records) {
                        if (r.name == name) {
                                chosen = &r;
                                break;
                        }
                }
                if (chosen == nullptr) {
                        promekiWarn("TranscriptionEngine::create: no backend registered as '%s'", name.cstr());
                        return makeError<UPtr>(Error::IdNotFound);
                }
                factory = chosen->factory;
                chosenName = chosen->name;
                supportedModes = chosen->supportedModes;
        }
        if (!factory) {
                promekiWarn("TranscriptionEngine::create: null factory for backend '%s'", name.cstr());
                return makeError<UPtr>(Error::LibraryFailure);
        }

        // Reject configs that ask for a mode the backend doesn't
        // support before we pay the construction cost.  An empty
        // supportedModes list means "the backend supports every mode".
        if (config != nullptr && !supportedModes.isEmpty()) {
                // TypedEnum subclasses slice to base Enum when stored as
                // Variants, so the canonical read path goes through
                // Variant::asEnum rather than getAs<TranscriptionMode>.
                Enum requested = config->get(MediaConfig::TranscriptionSessionMode)
                                         .asEnum(TranscriptionMode::Type);
                bool ok = false;
                for (int m : supportedModes) {
                        if (m == requested.value()) {
                                ok = true;
                                break;
                        }
                }
                if (!ok) {
                        promekiWarn("TranscriptionEngine::create: backend '%s' does not support requested "
                                    "TranscriptionMode '%s'",
                                    name.cstr(), requested.valueName().cstr());
                        return makeError<UPtr>(Error::NotSupported);
                }
        }

        UPtr engine = factory();
        if (!engine.isValid()) {
                promekiWarn("TranscriptionEngine::create: factory returned null for backend '%s'",
                            name.cstr());
                return makeError<UPtr>(Error::LibraryFailure);
        }
        engine->setName(std::move(chosenName));
        if (config != nullptr) engine->configure(*config);
        return makeResult(std::move(engine));
}

PROMEKI_NAMESPACE_END
