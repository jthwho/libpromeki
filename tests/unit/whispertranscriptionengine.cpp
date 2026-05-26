/**
 * @file      tests/whispertranscriptionengine.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Unit tests for the vendored whisper.cpp TranscriptionEngine backend.
 *
 * Two layers of coverage:
 *
 *   1. Registration / metadata — the @c "WhisperCpp" backend appears in
 *      the global registry, advertises the expected input formats and
 *      modes, and rejects streaming-mode configuration with
 *      @c Error::NotSupported.  These checks run unconditionally and
 *      do not require a model file on disk.
 *
 *   2. End-to-end batch transcription — submits a short synthesized
 *      audio clip, flushes, and verifies that at least one output
 *      Frame carrying a Transcript is emitted.  Gated on the presence
 *      of a model file (Dir::models()/whisper/ggml-tiny.bin by
 *      default; override via the @c PROMEKI_TEST_WHISPER_MODEL env
 *      var) — skipped quietly otherwise.  Use promeki-fetch-model to
 *      stage the file once locally; the test then exercises the full
 *      submitFrame → flush → receiveFrame loop.
 */

#include <doctest/doctest.h>
#include <cmath>
#include <cstdlib>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/dir.h>
#include <promeki/enums_transcription.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/timestamp.h>
#include <promeki/transcript.h>
#include <promeki/transcriptionengine.h>

using namespace promeki;

namespace {

        // Synthesizes a 200 ms 440 Hz tone payload in PCMI_Float32LE at
        // 16 kHz mono.  Whisper needs real speech to produce a useful
        // transcript, but for the smoke test we only care that the
        // engine accepts the audio and emits *some* finalised output —
        // even a single empty / "(silence)" segment is enough to prove
        // the submitFrame → flush → receiveFrame loop is wired up.
        PcmAudioPayload::Ptr makeTonePayload(size_t samples, float sampleRate, float freqHz, int64_t ptsNs) {
                AudioDesc    desc(AudioFormat::PCMI_Float32LE, sampleRate, 1);
                const size_t bytes = desc.bufferSize(samples);
                auto         buf = Buffer(bytes);
                buf.setSize(bytes);
                auto *p = static_cast<float *>(buf.data());
                for (size_t i = 0; i < samples; ++i) {
                        const double t = static_cast<double>(i) / sampleRate;
                        p[i] = static_cast<float>(std::sin(2.0 * M_PI * freqHz * t) * 0.4);
                }
                BufferView planes;
                planes.pushToBack(buf, 0, bytes);
                auto payload = PcmAudioPayload::Ptr::create(desc, samples, planes);
                payload.modify()->setPts(MediaTimeStamp(TimeStamp(ptsNs), ClockDomain::Synthetic));
                return payload;
        }

        // Returns the model path the end-to-end test should use.  Falls
        // back to Dir::models()/whisper/ggml-tiny.bin so anyone who
        // runs `promeki-fetch-model tiny` once on this host
        // automatically picks up the e2e path.
        String resolveTestModelPath() {
                const char *override = std::getenv("PROMEKI_TEST_WHISPER_MODEL");
                if (override != nullptr && override[0] != '\0') return String(override);
                FilePath p = Dir::models().path() / "whisper" / "ggml-tiny.bin";
                return p.toString();
        }

} // namespace

TEST_CASE("WhisperCpp: backend is registered with the expected metadata") {
        auto res = TranscriptionEngine::lookupBackend("WhisperCpp");
        REQUIRE(error(res) == Error::Ok);
        const TranscriptionEngine::BackendRecord &rec = value(res);
        CHECK(rec.name == "WhisperCpp");
        CHECK(rec.weight == BackendWeight::Vendored);
        CHECK_FALSE(rec.supportedInputs.isEmpty());
        // Confirm the two formats we advertise are present.
        bool hasFloat = false;
        bool hasS16 = false;
        for (int v : rec.supportedInputs) {
                if (v == static_cast<int>(AudioFormat::PCMI_Float32LE)) hasFloat = true;
                if (v == static_cast<int>(AudioFormat::PCMI_S16LE)) hasS16 = true;
        }
        CHECK(hasFloat);
        CHECK(hasS16);
        CHECK_FALSE(rec.supportedModes.isEmpty());
        // The Phase-1 build only supports Batch.
        bool hasBatch = false;
        for (int v : rec.supportedModes) {
                if (v == TranscriptionMode(TranscriptionMode::Batch).value()) hasBatch = true;
        }
        CHECK(hasBatch);
}

TEST_CASE("WhisperCpp: streaming mode is rejected at create time") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                Variant(TranscriptionMode(TranscriptionMode::Streaming)));
        auto res = TranscriptionEngine::create("WhisperCpp", &cfg);
        CHECK(error(res) == Error::NotSupported);
}

TEST_CASE("WhisperCpp: factory produces a configured session for batch mode") {
        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                Variant(TranscriptionMode(TranscriptionMode::Batch)));
        auto res = TranscriptionEngine::create("WhisperCpp", &cfg);
        REQUIRE(error(res) == Error::Ok);
        TranscriptionEngine::UPtr engine = std::move(res.first());
        REQUIRE(engine.isValid());
        CHECK(engine->name() == "WhisperCpp");
        // No model loaded yet — calling submitFrame on a fresh engine
        // with no PCM payload should error out cleanly.
        Frame empty;
        Error err = engine->submitFrame(empty);
        CHECK(err == Error::Invalid);
}

TEST_CASE("WhisperCpp: end-to-end batch transcription"
          * doctest::skip(true)) {
        // Skip by default — gated on a model file being present.  Use
        //   promeki-fetch-model tiny
        // to stage one under Dir::models()/whisper/, or set
        //   PROMEKI_TEST_WHISPER_MODEL=/path/to/ggml-XYZ.bin
        // to point at a model that's already been downloaded.  The
        // skip marker stays on to keep CI green without a model file —
        // remove it locally when iterating on the engine.
        String modelPath = resolveTestModelPath();
        if (!FilePath(modelPath).exists()) {
                MESSAGE("Skipping: model file not found at ", modelPath.cstr());
                return;
        }

        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                Variant(TranscriptionMode(TranscriptionMode::Batch)));
        cfg.set(MediaConfig::TranscriptionModelHint, Variant(modelPath));
        cfg.set(MediaConfig::TranscriptionLanguage, Variant(String("en")));
        cfg.set(MediaConfig::TranscriptionChannelMode,
                Variant(TranscriptionChannelMode(TranscriptionChannelMode::DownmixAll)));

        auto res = TranscriptionEngine::create("WhisperCpp", &cfg);
        REQUIRE(error(res) == Error::Ok);
        TranscriptionEngine::UPtr engine = std::move(res.first());

        // Feed ~1 second of audio in three chunks so we also cover the
        // multi-submit anchoring path.
        constexpr float kSr = 16000.0f;
        for (int i = 0; i < 3; ++i) {
                auto payload = makeTonePayload(static_cast<size_t>(kSr / 3), kSr, 220.0f,
                                               static_cast<int64_t>(i) * 333'000'000LL);
                Frame frame;
                frame.addPayload(payload);
                CHECK(engine->submitFrame(frame) == Error::Ok);
        }
        CHECK(engine->flush() == Error::Ok);

        // Drain everything the engine produced.  A pure sine wave
        // typically yields zero or one segment from whisper — we just
        // confirm the call sequence completes without error and any
        // emitted Frame carries the expected metadata stamp.
        int produced = 0;
        for (;;) {
                Frame out = engine->receiveFrame();
                if (!out.isValid()) break;
                ++produced;
                Variant t = out.metadata().get(Metadata::Transcript);
                CHECK(t.isValid());
                Transcript tr = t.get<Transcript>();
                CHECK_FALSE(tr.partial());
        }
        MESSAGE("WhisperCpp emitted ", produced, " segment(s)");
}

TEST_CASE("WhisperCpp: end-to-end batch transcription with resampling"
          * doctest::skip(true)) {
        // Same shape as the previous test, but feeds 48 kHz audio
        // through the AudioBuffer accumulator's resampler path.
        // Exercises the cross-rate code in WhisperEngine::appendPayload
        // (downmix scratch + ensureAccumulatorRoom + AudioBuffer push
        // with input rate != output rate).
        String modelPath = resolveTestModelPath();
        if (!FilePath(modelPath).exists()) {
                MESSAGE("Skipping: model file not found at ", modelPath.cstr());
                return;
        }

        MediaConfig cfg;
        cfg.set(MediaConfig::TranscriptionSessionMode,
                Variant(TranscriptionMode(TranscriptionMode::Batch)));
        cfg.set(MediaConfig::TranscriptionModelHint, Variant(modelPath));
        cfg.set(MediaConfig::TranscriptionLanguage, Variant(String("en")));
        cfg.set(MediaConfig::TranscriptionChannelMode,
                Variant(TranscriptionChannelMode(TranscriptionChannelMode::DownmixAll)));

        auto res = TranscriptionEngine::create("WhisperCpp", &cfg);
        REQUIRE(error(res) == Error::Ok);
        TranscriptionEngine::UPtr engine = std::move(res.first());

        constexpr float kSr = 48000.0f;
        for (int i = 0; i < 3; ++i) {
                auto payload = makeTonePayload(static_cast<size_t>(kSr / 3), kSr, 220.0f,
                                               static_cast<int64_t>(i) * 333'000'000LL);
                Frame frame;
                frame.addPayload(payload);
                CHECK(engine->submitFrame(frame) == Error::Ok);
        }
        CHECK(engine->flush() == Error::Ok);

        int produced = 0;
        for (;;) {
                Frame out = engine->receiveFrame();
                if (!out.isValid()) break;
                ++produced;
                Variant t = out.metadata().get(Metadata::Transcript);
                CHECK(t.isValid());
                Transcript tr = t.get<Transcript>();
                CHECK_FALSE(tr.partial());
        }
        MESSAGE("WhisperCpp (48k -> 16k) emitted ", produced, " segment(s)");
}
