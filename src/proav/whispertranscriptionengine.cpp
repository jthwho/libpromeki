/**
 * @file      whispertranscriptionengine.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Vendored whisper.cpp speech-to-text backend.  Registers the
 * @c "WhisperCpp" @ref TranscriptionEngine at static-init time with
 * @c BackendWeight::Vendored.  Batch mode only on this first cut —
 * streaming will land in a follow-up once the sliding-window /
 * partial-emission story is proven against end-to-end pipeline use.
 *
 * Pipeline shape:
 *
 *   1. @ref submitFrame  — extract the selected @ref PcmAudioPayload,
 *                          convert to float32 if necessary, downmix
 *                          per @ref TranscriptionChannelMode, resample
 *                          to 16 kHz mono via libsamplerate, append
 *                          to the in-flight float buffer.
 *
 *   2. @ref flush        — lazily initialise the whisper context from
 *                          the resolved model file, run
 *                          @c whisper_full over the accumulated
 *                          samples, iterate the resulting segments,
 *                          and emit one finalised output @ref Frame
 *                          per segment carrying a @ref Transcript on
 *                          @c Metadata::Transcript.
 *
 *   3. @ref receiveFrame — dequeues one ready output Frame per call,
 *                          or an invalid Frame when none are pending.
 *
 *   4. @ref reset        — drops in-flight audio and any pending
 *                          output Frames; configuration is preserved.
 *
 * Streaming mode (TranscriptionMode::Streaming) is rejected at
 * @ref onConfigure with @c Error::NotSupported.  Engines on Phase-1
 * still register Batch as their only supported mode in
 * @c registeredBackends.supportedModes, so the factory pre-check in
 * @ref TranscriptionEngine::create catches it before construction.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_WHISPER

#include <whisper.h>
#include <ggml.h>

#include <cstring>
#include <cmath>

#include <promeki/audiochannelmap.h>
#include <promeki/audiodesc.h>
#include <promeki/audiopayload.h>
#include <promeki/audioresampler.h>
#include <promeki/backendweight.h>
#include <promeki/bufferview.h>
#include <promeki/deque.h>
#include <promeki/dir.h>
#include <promeki/duration.h>
#include <promeki/enum.h>
#include <promeki/enums.h>
#include <promeki/char.h>
#include <promeki/error.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/once.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/transcript.h>
#include <promeki/transcriptionengine.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(WhisperInternal);

namespace {

        // ---- whisper.cpp / ggml log routing --------------------------

        // whisper.cpp's own logger and the underlying ggml runtime each
        // expose a single set-callback hook (whisper_log_set /
        // ggml_log_set).  Both are process-global, so the bridge is
        // installed exactly once for the life of the process — the
        // first WhisperEngine to come up wins.
        //
        // GGML_LOG_LEVEL_INFO covers both genuinely interesting
        // initialisation banners ("loading model X", "compute buffer
        // = Y MB") and routine per-call chatter; routing the whole
        // band straight at Info would flood logs whenever a session
        // does any work.  So:
        //
        //   - ERROR  → promekiWarn (always on — operator must see)
        //   - WARN   → promekiWarn (always on)
        //   - INFO   → promekiDebug, gated by WhisperInternal
        //   - DEBUG  → promekiDebug, gated by WhisperInternal
        //   - CONT   → appended to the previous record's level
        //
        // Enable with:
        //
        //   PROMEKI_DEBUG=WhisperInternal <command>
        //
        // file/line aren't part of the ggml callback signature, so the
        // record carries "whisper" as the source — the prefix on the
        // message body identifies the originating subsystem.
        OnceFlag g_whisperLogInstalled;

        static void ggmlPromekiLog(ggml_log_level level, const char *text, void * /*user_data*/) {
                if (text == nullptr || *text == '\0') return;
                String s(text);
                while (!s.isEmpty()) {
                        char c = s[s.size() - 1];
                        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
                        s.resize(s.size() - 1);
                }
                if (s.isEmpty()) return;
                switch (level) {
                        case GGML_LOG_LEVEL_ERROR:
                        case GGML_LOG_LEVEL_WARN:
                                Logger::defaultLogger().log(Logger::LogLevel::Warn, "whisper", 0,
                                                            String::sprintf("whisper: %s", s.cstr()));
                                break;
                        case GGML_LOG_LEVEL_INFO:
                        case GGML_LOG_LEVEL_DEBUG:
                        case GGML_LOG_LEVEL_CONT:
                        case GGML_LOG_LEVEL_NONE:
                        default:
                                if (_promeki_debug_enabled) {
                                        Logger::defaultLogger().log(
                                                Logger::LogLevel::Debug, "whisper", 0,
                                                String::sprintf("whisper[%d]: %s", static_cast<int>(level), s.cstr()));
                                }
                                break;
                }
        }

        static void ensureWhisperLogInstalled() {
                callOnce(g_whisperLogInstalled, []() {
                        whisper_log_set(&ggmlPromekiLog, nullptr);
                        ggml_log_set(&ggmlPromekiLog, nullptr);
                });
        }

        // ---- Constants -----------------------------------------------

        // whisper.cpp expects 16 kHz mono float32 in [-1, 1].  Every
        // path through the engine resamples + downmixes onto this
        // canonical format before invoking whisper_full.
        constexpr float kWhisperSampleRate = 16000.0f;

        // Default model name when MediaConfig::TranscriptionModelHint
        // is empty.  Multilingual, ~470 MB on disk, real-time on a
        // modern desktop CPU.  Bare-name resolution puts this at
        // Dir::models()/whisper/ggml-small.bin (see resolveModelPath).
        const String kDefaultModelName = "small";

        // Whisper t0/t1 timestamps are expressed in 10 ms units
        // (centiseconds).  Convert to nanoseconds for the absolute
        // TimeStamp we stamp on each TranscriptWord / Transcript.
        constexpr int64_t kCsToNs = 10'000'000LL;

        // ---- Model-path resolution -----------------------------------

        // Resolves the engine-specific model identifier into an absolute
        // filesystem path.  Empty / unset hint maps to the kDefault model
        // name.  Absolute paths are honoured verbatim so users can stage
        // models outside Dir::models() (network share, read-only mount,
        // etc.).  Bare names resolve under Dir::models()/whisper/ with
        // whisper.cpp's canonical "ggml-<name>.bin" filename.
        String resolveModelPath(const String &hint) {
                String name = hint.isEmpty() ? kDefaultModelName : hint;
                // Crude absolute-path detection: POSIX paths begin with
                // '/', Windows paths begin with a drive letter + ':'.
                // FilePath itself can answer the same question but it's
                // overkill here.
                if (name.startsWith("/") || (name.size() >= 2 && name.charAt(1) == Char(':'))) {
                        return name;
                }
                FilePath dir = Dir::models().path();
                String filename = String("ggml-") + name + ".bin";
                FilePath model = dir / "whisper" / filename;
                return model.toString();
        }

        // ---- BCP 47 → whisper language code --------------------------

        // Whisper accepts ISO 639-1 (two-letter) codes plus "auto" for
        // built-in language identification.  Strip any region suffix
        // off a BCP 47 tag so "en-US" / "fr-CA" still match.  Empty
        // hint maps to "auto" (whisper's auto-detect mode).
        String whisperLanguageCode(const String &bcp47) {
                if (bcp47.isEmpty()) return "auto";
                size_t dash = bcp47.find('-');
                if (dash == String::npos) return bcp47.toLower();
                return bcp47.left(dash).toLower();
        }

        // ---- Downmix helpers -----------------------------------------

        // Computes the per-output-frame downmix coefficients for a
        // single PCM payload.  Returns the list of source-channel
        // indices to sum (the inverse of the channel-selection rule).
        // Empty result means "no channels selected — drop silently".
        List<int> selectChannels(const PcmAudioPayload &payload, const MediaConfig &config) {
                const AudioDesc &desc = payload.desc();
                const unsigned int channels = desc.channels();
                if (channels == 0) return {};

                Enum mode = config.get(MediaConfig::TranscriptionChannelMode)
                                    .asEnum(TranscriptionChannelMode::Type);

                if (mode == TranscriptionChannelMode::DownmixAll) {
                        List<int> all;
                        all.reserve(channels);
                        for (unsigned int i = 0; i < channels; ++i) all.pushToBack(static_cast<int>(i));
                        return all;
                }

                if (mode == TranscriptionChannelMode::ChannelIndex) {
                        int32_t idx = config.getAs<int32_t>(MediaConfig::TranscriptionChannelIndex);
                        if (idx < 0 || static_cast<unsigned int>(idx) >= channels) {
                                promekiWarn("WhisperEngine: TranscriptionChannelIndex %d out of range (channels=%u); "
                                            "downmixing all channels instead",
                                            int(idx), channels);
                                List<int> all;
                                all.reserve(channels);
                                for (unsigned int i = 0; i < channels; ++i) all.pushToBack(static_cast<int>(i));
                                return all;
                        }
                        return {idx};
                }

                // mode == ChannelMap: consult the descriptor's role map.
                const AudioChannelMap roleMap =
                        config.getAs<AudioChannelMap>(MediaConfig::TranscriptionChannelMap);
                if (!roleMap.isValid()) {
                        // Empty map (the default): prefer the FrontCenter dialog
                        // stem when present, otherwise downmix everything.
                        const AudioChannelMap &incoming = desc.channelMap();
                        int center = incoming.indexOf(ChannelRole::FrontCenter);
                        if (center >= 0) return {center};
                        List<int> all;
                        all.reserve(channels);
                        for (unsigned int i = 0; i < channels; ++i) all.pushToBack(static_cast<int>(i));
                        return all;
                }

                // Pick every channel of `payload` whose role appears in the
                // requested role map.
                const AudioChannelMap &incoming = desc.channelMap();
                List<int> picked;
                for (size_t i = 0; i < incoming.channels() && i < channels; ++i) {
                        ChannelRole role = incoming.role(i);
                        if (roleMap.contains(role)) picked.pushToBack(static_cast<int>(i));
                }
                return picked;
        }

        // Downmixes interleaved float32 samples `src` (channels = `srcChannels`,
        // frames = `frames`) into a single mono float32 buffer `dst` (length
        // = `frames`) by averaging the source channels listed in `channels`.
        // `channels` must be non-empty and every entry must be a valid index
        // into `src`.
        void downmixFloat32(const float *src, size_t frames, unsigned int srcChannels,
                            const List<int> &channels, float *dst) {
                const float inv = 1.0f / static_cast<float>(channels.size());
                for (size_t f = 0; f < frames; ++f) {
                        float sum = 0.0f;
                        const float *base = src + f * srcChannels;
                        for (int c : channels) {
                                sum += base[c];
                        }
                        dst[f] = sum * inv;
                }
        }

        // ---- Engine --------------------------------------------------

        class WhisperEngine : public TranscriptionEngine {
                public:
                        WhisperEngine() = default;

                        ~WhisperEngine() override {
                                if (_ctx != nullptr) {
                                        whisper_free(_ctx);
                                        _ctx = nullptr;
                                }
                        }

                protected:
                        void onConfigure(const MediaConfig &cfg) override {
                                // Reject streaming-mode early — Phase 1 is batch only.
                                Enum mode = cfg.get(MediaConfig::TranscriptionSessionMode)
                                                    .asEnum(TranscriptionMode::Type);
                                if (mode != TranscriptionMode::Batch) {
                                        promekiWarn("WhisperEngine::configure: streaming mode is not yet "
                                                    "implemented; configure with TranscriptionSessionMode = Batch");
                                        setError(Error::NotSupported,
                                                 "WhisperCpp backend supports only TranscriptionMode::Batch in this build");
                                        return;
                                }
                                // Stash a couple of frequently-consulted values from the
                                // freshly-supplied config so the per-submit hot path
                                // doesn't keep calling getAs.
                                _streamIndex = cfg.getAs<int32_t>(MediaConfig::TranscriptionStreamIndex);
                                _wantWordTimestamps = cfg.getAs<bool>(MediaConfig::TranscriptionWordTimestamps);
                                _language = whisperLanguageCode(cfg.getAs<String>(MediaConfig::TranscriptionLanguage));
                                _modelHint = cfg.getAs<String>(MediaConfig::TranscriptionModelHint);
                                clearError();
                        }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                PcmAudioPayload::Ptr payload = selectInputPayload(frame, _streamIndex);
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        promekiWarnThrottled(
                                                1000,
                                                "WhisperEngine::submitFrame: no PCM audio payload on frame (streamIndex=%d)",
                                                _streamIndex);
                                        setError(Error::Invalid, "no PCM audio payload on frame");
                                        return _lastError;
                                }

                                // Anchor the absolute media-time of the buffered audio to
                                // the *first* submitted payload's PTS.  Later submits add
                                // samples at sample-rate offsets from there; per-word
                                // timestamps reconstruct absolute TimeStamps via
                                // wordTimestamp().
                                if (!_anchor.isValid()) {
                                        _anchor = payload->pts();
                                }

                                Error err = appendPayload(*payload);
                                if (err != Error::Ok) {
                                        setError(err, "failed to append PCM payload to whisper buffer");
                                }
                                return _lastError;
                        }

                        Frame receiveFrame() override {
                                if (_pending.isEmpty()) return Frame();
                                return _pending.popFromFront();
                        }

                        Error flush() override {
                                clearError();
                                if (_buffer.isEmpty()) {
                                        // Nothing to transcribe — flush is a no-op but not
                                        // an error.  receiveFrame() will return an invalid
                                        // Frame immediately.
                                        return Error::Ok;
                                }
                                if (!ensureContext()) {
                                        return _lastError;
                                }

                                whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
                                params.print_progress = false;
                                params.print_realtime = false;
                                params.print_timestamps = false;
                                params.print_special = false;
                                params.language = _language.cstr();
                                params.translate = false;
                                params.no_context = true;
                                params.single_segment = false;
                                params.token_timestamps = _wantWordTimestamps;
                                params.suppress_blank = true;
                                params.thold_pt = 0.01f;
                                // n_threads defaults to std::thread::hardware_concurrency()
                                // inside whisper.cpp — fine for batch transcription.

                                int rc = whisper_full(_ctx, params, _buffer.data(),
                                                      static_cast<int>(_buffer.size()));
                                if (rc != 0) {
                                        promekiWarn("WhisperEngine::flush: whisper_full failed (rc=%d, samples=%zu)",
                                                    rc, _buffer.size());
                                        setError(Error::DecodeFailed,
                                                 String::sprintf("whisper_full failed (rc=%d)", rc));
                                        return _lastError;
                                }
                                drainSegments();
                                // Reset in-flight state: the batch is complete.  Configuration
                                // and the loaded whisper context are preserved so the next
                                // session can reuse them.
                                _buffer.clear();
                                _anchor = MediaTimeStamp();
                                return Error::Ok;
                        }

                        Error reset() override {
                                clearError();
                                _buffer.clear();
                                _pending.clear();
                                _anchor = MediaTimeStamp();
                                // Keep _ctx and _resampler alive — the model is expensive
                                // to load (hundreds of MB) and the resampler's filter state
                                // is cheap to throw away via AudioResampler::reset(), which
                                // ensureResampler() will call when channel count changes.
                                if (_resampler.isValid() && _resampler->isValid()) {
                                        _resampler->reset();
                                }
                                return Error::Ok;
                        }

                private:
                        // Lazily loads the whisper context from the configured model
                        // path.  Returns false (with _lastError populated) on any
                        // failure; subsequent submits/flushes will keep returning
                        // the error until the engine is reset and reconfigured.
                        bool ensureContext() {
                                if (_ctx != nullptr) return true;

                                // Route whisper / ggml internal logs into the
                                // promeki logger before whisper_init_* runs;
                                // the init path itself emits banner / model
                                // metadata via this callback.
                                ensureWhisperLogInstalled();

                                String path = resolveModelPath(_modelHint);
                                if (!FilePath(path).exists()) {
                                        promekiWarn("WhisperEngine::ensureContext: model file not found: %s "
                                                    "(use promeki-fetch-model to download)",
                                                    path.cstr());
                                        setError(Error::NotExist,
                                                 String::sprintf("whisper model file not found: %s", path.cstr()));
                                        return false;
                                }

                                whisper_context_params cparams = whisper_context_default_params();
                                // CPU-only on this first cut — explicit GPU off so that
                                // a future PROMEKI_ENABLE_CUDA pass-through doesn't quietly
                                // flip the meaning here.
                                cparams.use_gpu = false;

                                _ctx = whisper_init_from_file_with_params(path.cstr(), cparams);
                                if (_ctx == nullptr) {
                                        promekiWarn("WhisperEngine::ensureContext: whisper_init_from_file_with_params "
                                                    "failed for '%s'",
                                                    path.cstr());
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("whisper init failed for %s", path.cstr()));
                                        return false;
                                }
                                _modelPath = path;
                                promekiInfo("WhisperEngine: loaded model '%s' (language=%s, word_ts=%s)",
                                            path.cstr(), _language.cstr(),
                                            _wantWordTimestamps ? "yes" : "no");
                                return true;
                        }

                        // Sets up / re-sets up the resampler when the input format
                        // changes.  Whisper takes 16 kHz mono so the *output* of
                        // libsamplerate is always 1 channel; we resample the
                        // already-downmixed mono buffer.  Returns false on setup
                        // failure.
                        bool ensureResampler(float inputRate) {
                                if (inputRate <= 0.0f) {
                                        setError(Error::Invalid, "PCM payload has zero sample rate");
                                        return false;
                                }
                                if (_resampler.isValid() && _resampler->isValid() && _inputRate == inputRate) {
                                        return true;
                                }
                                if (!_resampler.isValid()) {
                                        _resampler = UniquePtr<AudioResampler>::create();
                                }
                                Error err = _resampler->setup(1u, SrcQuality::SincMedium);
                                if (err != Error::Ok) {
                                        setError(err, "AudioResampler::setup failed");
                                        return false;
                                }
                                err = _resampler->setRatio(inputRate, kWhisperSampleRate);
                                if (err != Error::Ok) {
                                        setError(err, "AudioResampler::setRatio failed");
                                        return false;
                                }
                                _inputRate = inputRate;
                                return true;
                        }

                        Error appendPayload(const PcmAudioPayload &payload) {
                                const AudioDesc &desc = payload.desc();
                                const float sampleRate = desc.sampleRate();
                                const unsigned int channels = desc.channels();
                                const size_t frames = payload.sampleCount();
                                if (frames == 0 || channels == 0 || sampleRate <= 0.0f) {
                                        // Empty payload — nothing to do but also not an error.
                                        return Error::Ok;
                                }

                                // Step 1: get a float32-interleaved view of the input.  When
                                // the payload is already PCMI_Float32LE we read its bytes
                                // directly; otherwise PcmAudioPayload::convert does the
                                // format coercion for us.
                                PcmAudioPayload::Ptr floatPayload;
                                const float *interleaved = nullptr;
                                if (desc.format() == AudioFormat::PCMI_Float32LE) {
                                        const BufferView &data = payload.data();
                                        if (data.count() == 0) return Error::Invalid;
                                        BufferView::Entry plane0 = data[0];
                                        interleaved = reinterpret_cast<const float *>(plane0.data());
                                } else {
                                        floatPayload = payload.convert(AudioFormat(AudioFormat::PCMI_Float32LE));
                                        if (!floatPayload.isValid() || !floatPayload->isValid()
                                            || floatPayload->planeCount() == 0) {
                                                promekiWarn(
                                                        "WhisperEngine::appendPayload: convert(PCMI_Float32LE) failed "
                                                        "from format %d",
                                                        int(desc.format().id()));
                                                return Error::ConversionFailed;
                                        }
                                        const BufferView &data = floatPayload->data();
                                        BufferView::Entry plane0 = data[0];
                                        interleaved = reinterpret_cast<const float *>(plane0.data());
                                }

                                // Step 2: downmix to a mono float buffer.  Selecting zero
                                // channels means "drop this payload silently" — keep going
                                // rather than fail, since the caller's MediaConfig may have
                                // been targeting a different audio stream.
                                List<int> channelSel = selectChannels(payload, this->config());
                                if (channelSel.isEmpty()) {
                                        return Error::Ok;
                                }
                                List<float> mono;
                                mono.resize(frames);
                                if (channels == 1 && channelSel.size() == 1 && channelSel.at(0) == 0) {
                                        std::memcpy(mono.data(), interleaved, frames * sizeof(float));
                                } else {
                                        downmixFloat32(interleaved, frames, channels, channelSel, mono.data());
                                }

                                // Step 3: resample to 16 kHz mono unless the input is
                                // already at the target rate, in which case pass through
                                // verbatim (saves the filter delay).
                                if (sampleRate == kWhisperSampleRate) {
                                        const size_t base = _buffer.size();
                                        _buffer.resize(base + frames);
                                        std::memcpy(_buffer.data() + base, mono.data(), frames * sizeof(float));
                                        return Error::Ok;
                                }

                                if (!ensureResampler(sampleRate)) return _lastError;

                                // Allocate a generous output buffer — libsamplerate may
                                // produce up to ratio*frames + (filter-internal) extra
                                // samples per process() call.  +64 absorbs the rounding
                                // / filter overhang without forcing a second loop pass.
                                const double ratio = static_cast<double>(kWhisperSampleRate) / sampleRate;
                                const size_t outCap = static_cast<size_t>(
                                                              std::ceil(static_cast<double>(frames) * ratio))
                                                      + 64;
                                List<float> out;
                                out.resize(outCap);

                                long used = 0;
                                long gen = 0;
                                Error err = _resampler->process(mono.data(), static_cast<long>(frames),
                                                                out.data(), static_cast<long>(outCap),
                                                                used, gen, /*endOfInput=*/false);
                                if (err != Error::Ok) {
                                        return err;
                                }
                                if (gen > 0) {
                                        const size_t base = _buffer.size();
                                        _buffer.resize(base + static_cast<size_t>(gen));
                                        std::memcpy(_buffer.data() + base, out.data(),
                                                    static_cast<size_t>(gen) * sizeof(float));
                                }
                                return Error::Ok;
                        }

                        // Iterate the segments produced by the most-recent whisper_full
                        // call and emit one finalised output Frame per segment.  Word
                        // timestamps (when configured) are derived from per-token data.
                        void drainSegments() {
                                const int nSegments = whisper_full_n_segments(_ctx);
                                for (int s = 0; s < nSegments; ++s) {
                                        const int64_t segT0 = whisper_full_get_segment_t0(_ctx, s);
                                        const int64_t segT1 = whisper_full_get_segment_t1(_ctx, s);
                                        const char *text = whisper_full_get_segment_text(_ctx, s);
                                        if (text == nullptr) text = "";

                                        TranscriptWord::List words;
                                        if (_wantWordTimestamps) {
                                                const int nTok = whisper_full_n_tokens(_ctx, s);
                                                words.reserve(static_cast<size_t>(nTok));
                                                for (int t = 0; t < nTok; ++t) {
                                                        whisper_token_data td =
                                                                whisper_full_get_token_data(_ctx, s, t);
                                                        const char *tt = whisper_full_get_token_text(_ctx, s, t);
                                                        if (tt == nullptr) continue;
                                                        // Skip special / unspoken tokens — whisper emits
                                                        // <|startoftranscript|>, <|en|>, <|notimestamps|>,
                                                        // etc.; their text starts with '<' and they have
                                                        // no useful timing.
                                                        if (tt[0] == '<') continue;
                                                        TranscriptWord w(String(tt),
                                                                         segmentTimeToAbsolute(td.t0),
                                                                         segmentTimeToAbsolute(td.t1),
                                                                         td.p);
                                                        words.pushToBack(std::move(w));
                                                }
                                        } else {
                                                // Single synthetic "word" carrying the full segment
                                                // text — downstream consumers can still derive a cue
                                                // from this, just without intra-segment timing.
                                                TranscriptWord w(String(text),
                                                                 segmentTimeToAbsolute(segT0),
                                                                 segmentTimeToAbsolute(segT1),
                                                                 1.0f);
                                                words.pushToBack(std::move(w));
                                        }

                                        Transcript t(std::move(words));
                                        t.setLanguage(_language == "auto" ? String() : _language);
                                        t.setPartial(false); // batch mode emits only finalised cues
                                        t.setConfidence(1.0f);

                                        // We have no source Frame in batch mode (the audio was
                                        // accumulated across many submits), so synthesise a
                                        // minimal output Frame carrying the Transcript on its
                                        // metadata.  Capture-time is the segment's absolute
                                        // start so downstream subtitle / log consumers can
                                        // line up output cues with the original timeline.
                                        Frame out;
                                        // Echo the source's ClockDomain through; we have no
                                        // source Frame in batch mode, but the engine's anchor
                                        // MediaTimeStamp carries the right domain.
                                        out.setCaptureTime(MediaTimeStamp(segmentTimeToAbsolute(segT0),
                                                                          _anchor.domain()));
                                        out.metadata().set(Metadata::Transcript, Variant(t));
                                        _pending.pushToBack(std::move(out));
                                }
                        }

                        // Converts a whisper-relative 10ms-tick timestamp into the
                        // absolute media-time domain by anchoring at the first
                        // submitted payload's PTS.
                        TimeStamp segmentTimeToAbsolute(int64_t cs) const {
                                if (!_anchor.timeStamp().isValid()) {
                                        return TimeStamp(cs * kCsToNs);
                                }
                                return TimeStamp(_anchor.timeStamp().nanoseconds() + cs * kCsToNs);
                        }

                        // Whisper context — lazy-loaded on first flush, freed on dtor.
                        whisper_context             *_ctx = nullptr;
                        String                       _modelPath;
                        String                       _modelHint;

                        // Configuration cache (refreshed in onConfigure).
                        String  _language = "auto";
                        bool    _wantWordTimestamps = false;
                        int32_t _streamIndex = -1;

                        // Resampler (lazy; recreated when input format changes).
                        UniquePtr<AudioResampler> _resampler;
                        float                     _inputRate = 0.0f;

                        // Per-session in-flight state.
                        List<float>      _buffer;   // 16 kHz mono float32, accumulated across submits.
                        MediaTimeStamp   _anchor;   // PTS of the first submitted payload.
                        Deque<Frame>     _pending;  // Output Frames waiting on receiveFrame().
        };

        // ---- Static registration -------------------------------------

        struct WhisperRegistrar {
                        WhisperRegistrar() {
                                TranscriptionEngine::BackendRecord rec;
                                rec.name = "WhisperCpp";
                                rec.description = "Vendored whisper.cpp speech-to-text engine (batch mode, CPU).";
                                rec.weight = BackendWeight::Vendored;
                                rec.supportedInputs = {
                                        static_cast<int>(AudioFormat::PCMI_Float32LE),
                                        static_cast<int>(AudioFormat::PCMI_S16LE),
                                };
                                rec.supportedModes.pushToBack(TranscriptionMode(TranscriptionMode::Batch).value());
                                rec.factory = []() -> TranscriptionEngine::UPtr {
                                        return TranscriptionEngine::UPtr::takeOwnership(new WhisperEngine);
                                };
                                Error err = TranscriptionEngine::registerBackend(std::move(rec));
                                if (err != Error::Ok) {
                                        promekiWarn("WhisperRegistrar: failed to register 'WhisperCpp' backend: %s",
                                                    err.name().cstr());
                                }
                        }
        };

        static WhisperRegistrar _whisperRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_WHISPER
