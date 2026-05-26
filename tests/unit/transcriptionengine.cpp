/**
 * @file      tests/transcriptionengine.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the TranscriptionEngine session contract end-to-end with a
 * synthetic "Echo" engine that turns PCM submits into canned Subtitle
 * cues.  The intent is to prove the registry plumbing (registration,
 * lookup, weighted ordering, lookupBackend, NotSupported gating), the
 * configure → submit/receive → flush lifecycle, Frame echo + cue
 * stamping with @ref Subtitle::partial riding on the cue, PTS-anchored
 * cue timestamps, and channel-selection plumbing (ChannelMap /
 * ChannelIndex / DownmixAll), all before any real speech-to-text
 * backend is plugged in.
 */

#include <doctest/doctest.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiopayload.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/clockdomain.h>
#include <promeki/deque.h>
#include <promeki/duration.h>
#include <promeki/frame.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/timestamp.h>
#include <promeki/transcript.h>
#include <promeki/transcriptionengine.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

        // Synthetic engine: each submitted PCM payload produces one
        // canned Subtitle cue whose start/end mirror the payload's PTS
        // window.  Streaming mode emits a "partial …" cue at submit
        // time and a "final" cue at flush.  Batch mode emits nothing
        // until flush, then drains every accumulated cue.
        class EchoTranscriptionEngine : public TranscriptionEngine {
                public:
                        void onConfigure(const MediaConfig &cfg) override {
                                Enum modeEnum = cfg.get(MediaConfig::TranscriptionSessionMode)
                                                        .asEnum(TranscriptionMode::Type);
                                _mode = TranscriptionMode(modeEnum.value());
                                Enum chEnum = cfg.get(MediaConfig::TranscriptionChannelMode)
                                                      .asEnum(TranscriptionChannelMode::Type);
                                _channelMode = TranscriptionChannelMode(chEnum.value());
                                _channelMap = cfg.getAs<AudioChannelMap>(MediaConfig::TranscriptionChannelMap);
                                _channelIndex = cfg.getAs<int32_t>(MediaConfig::TranscriptionChannelIndex);
                                _streamIndex = cfg.getAs<int32_t>(MediaConfig::TranscriptionStreamIndex);
                                _language = cfg.getAs<String>(MediaConfig::TranscriptionLanguage);
                                _configured = true;
                        }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                PcmAudioPayload::Ptr pcm = selectInputPayload(frame, _streamIndex);
                                if (!pcm.isValid() || !pcm->isValid()) {
                                        setError(Error::Invalid, "no PCM audio payload on frame");
                                        return _lastError;
                                }
                                // Build a single-word transcript whose
                                // start/end are anchored to the payload's
                                // pts via the shared helper.  Echo
                                // engines stuff the language hint into
                                // the word text so the lifecycle (partial
                                // → final) and PTS-anchoring assertions
                                // have something deterministic to check.
                                TimeStamp wordStart = wordTimestamp(*pcm, 0);
                                TimeStamp wordEnd = wordTimestamp(*pcm, pcm->sampleCount());
                                TranscriptWord       w(String("partial:") + _language, wordStart, wordEnd, 0.5f);
                                TranscriptWord::List words;
                                words.pushToBack(w);
                                Transcript partialUtterance(std::move(words), String(), _language, 0.5f,
                                                            /*partial=*/true);
                                if (_mode == TranscriptionMode::Streaming) {
                                        _queue.pushToBack(buildOutputFrame(frame, partialUtterance));
                                }
                                _pending.pushToBack(PendingEntry(frame, partialUtterance));
                                ++_submitCount;
                                return Error::Ok;
                        }

                        Frame receiveFrame() override {
                                if (_queue.isEmpty()) return Frame();
                                return _queue.popFromFront();
                        }

                        Error flush() override {
                                // Promote every accumulated submit to a
                                // finalised utterance.  Streaming engines
                                // emit a finalised version of the
                                // transcript they already emitted as
                                // partial; batch engines emit the
                                // utterance for the first time here.
                                while (!_pending.isEmpty()) {
                                        PendingEntry entry = _pending.popFromFront();
                                        const Transcript &orig = entry.second();
                                        TranscriptWord    fw(String("final:") + _language,
                                                          orig.start(), orig.end(), 0.95f);
                                        TranscriptWord::List fwords;
                                        fwords.pushToBack(fw);
                                        Transcript finalUtterance(std::move(fwords), String(), _language, 0.95f,
                                                                  /*partial=*/false);
                                        _queue.pushToBack(buildOutputFrame(entry.first(), finalUtterance));
                                }
                                _flushed = true;
                                return Error::Ok;
                        }

                        Error reset() override {
                                _queue.clear();
                                _pending.clear();
                                _submitCount = 0;
                                _flushed = false;
                                return Error::Ok;
                        }

                        bool                     configured() const { return _configured; }
                        TranscriptionMode        configuredMode() const { return _mode; }
                        TranscriptionChannelMode configuredChannelMode() const { return _channelMode; }
                        const AudioChannelMap   &configuredChannelMap() const { return _channelMap; }
                        int32_t                  configuredChannelIndex() const { return _channelIndex; }
                        const String            &configuredLanguage() const { return _language; }
                        int                      submitCount() const { return _submitCount; }

                private:
                        using PendingEntry = Pair<Frame, Transcript>;

                        Deque<Frame>             _queue;
                        Deque<PendingEntry>      _pending;
                        TranscriptionMode        _mode{TranscriptionMode::Streaming};
                        TranscriptionChannelMode _channelMode{TranscriptionChannelMode::ChannelMap};
                        AudioChannelMap          _channelMap;
                        int32_t                  _channelIndex = 0;
                        int32_t                  _streamIndex = -1;
                        String                   _language;
                        int                      _submitCount = 0;
                        bool                     _configured = false;
                        bool                     _flushed = false;
        };

        // Registers the test backends exactly once per process so each
        // TEST_CASE starts from a known registry state.  Two backends are
        // wired up: a "regular" Echo with both modes supported, and a
        // "batch-only" sibling for proving the NotSupported gate at create
        // time.
        struct EchoRegistrar {
                        EchoRegistrar() {
                                TranscriptionEngine::registerBackend({
                                        .name = "EchoTranscription",
                                        .description = "Echo (test) transcription engine",
                                        .weight = BackendWeight::Vendored,
                                        .supportedInputs = {static_cast<int>(AudioFormat::PCMI_S16LE),
                                                            static_cast<int>(AudioFormat::PCMI_Float32LE)},
                                        .supportedModes = {TranscriptionMode::Streaming.value(),
                                                           TranscriptionMode::Batch.value()},
                                        .factory = []() -> TranscriptionEngine::UPtr {
                                                return UniquePtr<TranscriptionEngine>::takeOwnership(
                                                        new EchoTranscriptionEngine());
                                        },
                                });
                                TranscriptionEngine::registerBackend({
                                        .name = "EchoTranscriptionBatchOnly",
                                        .description = "Echo (test) batch-only transcription engine",
                                        .weight = BackendWeight::User,
                                        .supportedInputs = {static_cast<int>(AudioFormat::PCMI_S16LE)},
                                        .supportedModes = {TranscriptionMode::Batch.value()},
                                        .factory = []() -> TranscriptionEngine::UPtr {
                                                return UniquePtr<TranscriptionEngine>::takeOwnership(
                                                        new EchoTranscriptionEngine());
                                        },
                                });
                        }
        };

        static EchoRegistrar gEchoRegistrar;

        // Builds a PCM Frame whose payload reports `samples` frames at
        // 48 kHz stereo and the supplied PTS.  Content is zero-filled —
        // the synthetic engine never reads the samples themselves.
        Frame makePcmFrame(size_t samples, const TimeStamp &pts) {
                AudioDesc  desc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
                Buffer     buf(desc.bufferSize(samples));
                BufferView view(buf, 0, buf.size());
                auto       payload = PcmAudioPayload::Ptr::create(desc, samples, view);
                payload.modify()->setPts(MediaTimeStamp(pts, ClockDomain(ClockDomain::SystemMonotonic)));
                Frame f;
                f.addPayload(payload);
                f.setCaptureTime(MediaTimeStamp(pts, ClockDomain(ClockDomain::SystemMonotonic)));
                return f;
        }

        EchoTranscriptionEngine *asEcho(const TranscriptionEngine::UPtr &eng) {
                // dynamic_cast is intentional — the test backend is the
                // only one registered so this always succeeds, but
                // a missed cast must fail the test loudly rather than
                // segfault.
                return dynamic_cast<EchoTranscriptionEngine *>(eng.get());
        }

} // namespace

TEST_CASE("TranscriptionEngine: registerBackend rejects malformed records") {
        TranscriptionEngine::BackendRecord empty;
        CHECK(TranscriptionEngine::registerBackend(empty) == Error::Invalid);

        TranscriptionEngine::BackendRecord noFactory;
        noFactory.name = "NeverRegisters";
        CHECK(TranscriptionEngine::registerBackend(noFactory) == Error::Invalid);

        // Static-init Registrar made sure neither slot pollutes the registry.
        CHECK(error(TranscriptionEngine::lookupBackend("NeverRegisters")) == Error::IdNotFound);
}

TEST_CASE("TranscriptionEngine: registeredBackends sorts by descending weight") {
        auto list = TranscriptionEngine::registeredBackends();
        REQUIRE(list.size() >= 2);
        // EchoTranscriptionBatchOnly is registered at User weight (higher than Vendored),
        // so it must come first.
        bool sawBatchOnly = false;
        bool sawEcho = false;
        int  weightSeenBefore = INT32_MAX;
        for (const auto &r : list) {
                CHECK(r.weight <= weightSeenBefore);
                weightSeenBefore = r.weight;
                if (r.name == "EchoTranscription") sawEcho = true;
                if (r.name == "EchoTranscriptionBatchOnly") sawBatchOnly = true;
        }
        CHECK(sawEcho);
        CHECK(sawBatchOnly);
}

TEST_CASE("TranscriptionEngine: lookupBackend round-trips") {
        auto r = TranscriptionEngine::lookupBackend("EchoTranscription");
        REQUIRE(isOk(r));
        CHECK(value(r).description == "Echo (test) transcription engine");
        CHECK(value(r).supportedModes.size() == 2);

        CHECK(error(TranscriptionEngine::lookupBackend("NoSuchEngine")) == Error::IdNotFound);
}

TEST_CASE("TranscriptionEngine: create() resolves the named backend and stashes config") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionLanguage, String("en-US"));
        cfg.set(MediaConfig::TranscriptionSessionMode,
                TranscriptionMode(TranscriptionMode::Streaming));

        auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
        REQUIRE(isOk(res));
        TranscriptionEngine::UPtr engine = std::move(res.first());
        REQUIRE(engine.isValid());
        CHECK(engine->name() == "EchoTranscription");

        EchoTranscriptionEngine *echo = asEcho(engine);
        REQUIRE(echo != nullptr);
        CHECK(echo->configured());
        CHECK(echo->configuredLanguage() == "en-US");
        CHECK(echo->configuredMode() == TranscriptionMode::Streaming);
}

TEST_CASE("TranscriptionEngine: create() reports IdNotFound for unknown names") {
        auto res = TranscriptionEngine::create("DefinitelyMissing");
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("TranscriptionEngine: create() rejects unsupported TranscriptionMode") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                TranscriptionMode(TranscriptionMode::Streaming));
        auto res = TranscriptionEngine::create("EchoTranscriptionBatchOnly", &cfg);
        CHECK(error(res) == Error::NotSupported);

        // Same backend with the supported (Batch) mode resolves fine.
        cfg.set(MediaConfig::TranscriptionSessionMode,
                TranscriptionMode(TranscriptionMode::Batch));
        auto res2 = TranscriptionEngine::create("EchoTranscriptionBatchOnly", &cfg);
        REQUIRE(isOk(res2));
}

TEST_CASE("TranscriptionEngine: streaming mode emits PTS-anchored partial transcripts") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                TranscriptionMode(TranscriptionMode::Streaming));
        cfg.set(MediaConfig::TranscriptionLanguage, String("en"));

        auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
        REQUIRE(isOk(res));
        TranscriptionEngine::UPtr engine = std::move(res.first());

        TimeStamp pts(1'000'000'000); // 1.0 s
        Frame     in = makePcmFrame(/*samples=*/48000, pts);
        REQUIRE(engine->submitFrame(in) == Error::Ok);

        Frame out = engine->receiveFrame();
        REQUIRE(out.isValid());

        // Output Frame must echo the source's audio payload through.
        REQUIRE(out.audioPayloads().size() == 1);

        // Transcript metadata must be present with the PTS-anchored
        // utterance.  Word start/end are derived via wordTimestamp from
        // the payload's pts.
        REQUIRE(out.metadata().contains(Metadata::Transcript));
        Transcript tr = out.metadata().get(Metadata::Transcript).get<Transcript>();
        REQUIRE(tr.words().size() == 1);
        const TranscriptWord &w = tr.words()[0];
        CHECK(w.start() == pts);
        CHECK(w.end() == TimeStamp(pts.nanoseconds() + 1'000'000'000)); // 48 000 samples @ 48 kHz = 1 s
        CHECK(w.text() == "partial:en");
        CHECK(tr.language() == "en");

        // Partial flag rides on the transcript itself, not on Frame metadata.
        CHECK(tr.partial());

        // No more pending output until flush.
        CHECK_FALSE(engine->receiveFrame().isValid());
}

TEST_CASE("TranscriptionEngine: flush() emits finalised transcripts without the partial flag") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                TranscriptionMode(TranscriptionMode::Streaming));
        cfg.set(MediaConfig::TranscriptionLanguage, String("en"));

        auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
        REQUIRE(isOk(res));
        TranscriptionEngine::UPtr engine = std::move(res.first());

        TimeStamp pts(2'000'000'000);
        REQUIRE(engine->submitFrame(makePcmFrame(/*samples=*/24000, pts)) == Error::Ok);
        // Drain the partial.
        (void)engine->receiveFrame();

        REQUIRE(engine->flush() == Error::Ok);
        Frame finalFrame = engine->receiveFrame();
        REQUIRE(finalFrame.isValid());

        REQUIRE(finalFrame.metadata().contains(Metadata::Transcript));
        Transcript tr = finalFrame.metadata().get(Metadata::Transcript).get<Transcript>();
        REQUIRE(tr.words().size() == 1);
        CHECK(tr.words()[0].text() == "final:en");
        CHECK(tr.confidence() == doctest::Approx(0.95f));
        // Finalised transcripts leave Transcript::partial at its default false.
        CHECK_FALSE(tr.partial());
}

TEST_CASE("TranscriptionEngine: batch mode holds transcripts until flush") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                TranscriptionMode(TranscriptionMode::Batch));
        cfg.set(MediaConfig::TranscriptionLanguage, String("fr"));

        auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
        REQUIRE(isOk(res));
        TranscriptionEngine::UPtr engine = std::move(res.first());

        // Submit several frames; receive must return invalid (nothing
        // gets emitted in Batch mode until flush).
        REQUIRE(engine->submitFrame(makePcmFrame(48000, TimeStamp(0))) == Error::Ok);
        REQUIRE(engine->submitFrame(makePcmFrame(48000, TimeStamp(1'000'000'000))) == Error::Ok);
        CHECK_FALSE(engine->receiveFrame().isValid());

        REQUIRE(engine->flush() == Error::Ok);

        Frame f1 = engine->receiveFrame();
        Frame f2 = engine->receiveFrame();
        REQUIRE(f1.isValid());
        REQUIRE(f2.isValid());
        Transcript tr1 = f1.metadata().get(Metadata::Transcript).get<Transcript>();
        Transcript tr2 = f2.metadata().get(Metadata::Transcript).get<Transcript>();
        REQUIRE(tr1.words().size() == 1);
        REQUIRE(tr2.words().size() == 1);
        CHECK(tr1.words()[0].text() == "final:fr");
        CHECK(tr2.words()[0].text() == "final:fr");
        // Batch outputs are always finalised (Transcript::partial == false).
        CHECK_FALSE(tr1.partial());
        CHECK_FALSE(tr2.partial());

        CHECK_FALSE(engine->receiveFrame().isValid());
}

TEST_CASE("TranscriptionEngine: channel-selection plumbing flows from MediaConfig to onConfigure") {
        // ChannelMap path.
        {
                MediaConfig cfg;
                AudioChannelMap roles({ChannelRole::FrontCenter});
                cfg.set(MediaConfig::TranscriptionChannelMode,
                        TranscriptionChannelMode(TranscriptionChannelMode::ChannelMap));
                cfg.set(MediaConfig::TranscriptionChannelMap, roles);

                auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
                REQUIRE(isOk(res));
                EchoTranscriptionEngine *echo = asEcho(value(res));
                REQUIRE(echo != nullptr);
                CHECK(echo->configuredChannelMode() == TranscriptionChannelMode::ChannelMap);
                CHECK(echo->configuredChannelMap() == roles);
        }

        // ChannelIndex path.
        {
                MediaConfig cfg;
                cfg.set(MediaConfig::TranscriptionChannelMode,
                        TranscriptionChannelMode(TranscriptionChannelMode::ChannelIndex));
                cfg.set(MediaConfig::TranscriptionChannelIndex, int32_t(3));

                auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
                REQUIRE(isOk(res));
                EchoTranscriptionEngine *echo = asEcho(value(res));
                REQUIRE(echo != nullptr);
                CHECK(echo->configuredChannelMode() == TranscriptionChannelMode::ChannelIndex);
                CHECK(echo->configuredChannelIndex() == 3);
        }

        // DownmixAll path.
        {
                MediaConfig cfg;
                cfg.set(MediaConfig::TranscriptionChannelMode,
                        TranscriptionChannelMode(TranscriptionChannelMode::DownmixAll));

                auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
                REQUIRE(isOk(res));
                EchoTranscriptionEngine *echo = asEcho(value(res));
                REQUIRE(echo != nullptr);
                CHECK(echo->configuredChannelMode() == TranscriptionChannelMode::DownmixAll);
        }
}

TEST_CASE("TranscriptionEngine: selectInputPayload honours streamIndex") {
        // Build a Frame with two PCM payloads on different streams.
        AudioDesc  desc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        Buffer     buf(desc.bufferSize(1024));
        BufferView view(buf, 0, buf.size());
        auto       p0 = PcmAudioPayload::Ptr::create(desc, 1024, view);
        p0.modify()->setStreamIndex(0);
        auto       p7 = PcmAudioPayload::Ptr::create(desc, 1024, view);
        p7.modify()->setStreamIndex(7);
        Frame frame;
        frame.addPayload(p0);
        frame.addPayload(p7);

        // Default -1: first payload found, regardless of index.
        auto any = TranscriptionEngine::selectInputPayload(frame);
        REQUIRE(any.isValid());
        CHECK(any->streamIndex() == 0);

        // Explicit 7: only the matching stream.
        auto only7 = TranscriptionEngine::selectInputPayload(frame, 7);
        REQUIRE(only7.isValid());
        CHECK(only7->streamIndex() == 7);

        // Missing index: null pointer.
        auto miss = TranscriptionEngine::selectInputPayload(frame, 42);
        CHECK_FALSE(miss.isValid());
}

TEST_CASE("TranscriptionEngine: reset() clears in-flight state without forgetting configuration") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                TranscriptionMode(TranscriptionMode::Streaming));
        cfg.set(MediaConfig::TranscriptionLanguage, String("de"));

        auto res = TranscriptionEngine::create("EchoTranscription", &cfg);
        REQUIRE(isOk(res));
        TranscriptionEngine::UPtr engine = std::move(res.first());
        EchoTranscriptionEngine  *echo = asEcho(engine);
        REQUIRE(echo != nullptr);

        REQUIRE(engine->submitFrame(makePcmFrame(48000, TimeStamp(0))) == Error::Ok);
        CHECK(echo->submitCount() == 1);
        REQUIRE(engine->reset() == Error::Ok);
        CHECK(echo->submitCount() == 0);
        // Configuration survives reset.
        CHECK(echo->configuredLanguage() == "de");
        CHECK(echo->configuredMode() == TranscriptionMode::Streaming);
}
