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
 *                          per @ref TranscriptionChannelMode into the
 *                          persistent mono scratch, and push into the
 *                          per-session @ref AudioBuffer accumulator
 *                          (which handles the 16 kHz mono resampling
 *                          and ring storage internally).
 *
 *   2. @ref flush        — lazily initialise the whisper context from
 *                          the resolved model file, drain the
 *                          accumulator into a contiguous float buffer,
 *                          run @c whisper_full over those samples,
 *                          iterate the resulting segments, and emit
 *                          one finalised output @ref Frame per
 *                          segment carrying a @ref Transcript on
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

#include <promeki/audiobuffer.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiodesc.h>
#include <promeki/audiopayload.h>
#include <promeki/backendweight.h>
#include <promeki/basicthread.h>
#include <promeki/bufferview.h>
#include <promeki/deque.h>
#include <promeki/dir.h>
#include <promeki/duration.h>
#include <promeki/env.h>
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

        // Whisper t0/t1 timestamps are expressed in 10 ms units
        // (centiseconds).  Convert to nanoseconds for the absolute
        // TimeStamp we stamp on each TranscriptWord / Transcript.
        constexpr int64_t kCsToNs = 10'000'000LL;

        // ---- Model-path resolution -----------------------------------

        // Subdirectory of Dir::models() that holds whisper.cpp model
        // files.  All bare-name / fallback / symlink lookups happen
        // here.  Kept as a constant so the layout is visible in one
        // place; promeki-fetch-model and the unit tests use the same
        // convention.
        const String kWhisperSubdir = "whisper";

        // Bare name of the pseudo-symlink (or OS symlink) that, when
        // present in the whisper model directory, names which model
        // to load by default.  Created by users / tools that want to
        // pin a particular model without rewriting every config file
        // that asks for the engine.  See FilePath::writePseudoSymlink
        // for the on-disk shape when symlinks are not available.
        const String kDefaultLinkName = "default";

        // Environment variable consulted (only if MediaConfig is not
        // already pinning a model) for an explicit override path.
        // Values may be absolute paths or bare model names; bare
        // names resolve under Dir::models()/whisper/ with the
        // canonical "ggml-<name>.bin" filename.
        const char *kEnvVarModel = "PROMEKI_WHISPER_MODEL";

        // Fallback model order, used when no config / env / default
        // symlink applies.  Tries "small" first (the canonical
        // mid-quality whisper model: ~470 MB, real-time on a desktop
        // CPU, multilingual), then walks outward — preferring the
        // next-better tier ("medium", then the large variants)
        // before degrading to the lighter "base" / "tiny" tiers.
        // English-only variants are interleaved so an .en model can
        // satisfy callers who have only that staged.
        const String kFallbackTiers[] = {
                "small",      "small.en",   "medium",     "medium.en", "large-v3",
                "large-v3-turbo", "large-v2", "large",      "large-v1",  "base",       "base.en",
                "tiny",       "tiny.en",
        };
        constexpr size_t kFallbackTiersCount = sizeof(kFallbackTiers) / sizeof(kFallbackTiers[0]);

        // Returns the on-disk path the canonical whisper.cpp file for
        // a bare model name lives at:
        //   Dir::models()/whisper/ggml-<name>.bin
        FilePath whisperModelFile(const String &bareName) {
                FilePath dir = Dir::models().path() / kWhisperSubdir;
                return dir / (String("ggml-") + bareName + ".bin");
        }

        // True when @p s looks like a filesystem path (contains a
        // path separator) or carries a file extension we recognise.
        // Used to decide whether an override value is a path or a
        // bare model name.  Conservative — a value that simply names
        // a model ("small", "tiny") returns @c false; anything with
        // a slash, a drive letter, or a ".bin" / ".gguf" extension
        // returns @c true.
        bool looksLikePath(const String &s) {
                if (s.isEmpty()) return false;
                if (s.find('/') != String::npos) return true;
                if (s.find('\\') != String::npos) return true;
                if (s.size() >= 2 && s.charAt(1) == Char(':')) return true; // Windows drive
                if (s.endsWith(String(".bin")) || s.endsWith(String(".gguf"))) return true;
                return false;
        }

        // Expands an override value (from config or env) into an
        // absolute filesystem path.  Absolute paths pass through
        // unchanged.  Other path-shaped strings are returned as-is
        // (the caller-supplied form, which may be relative to
        // their CWD).  Bare model names ("small", "tiny.en") are
        // canonicalised to Dir::models()/whisper/ggml-<name>.bin.
        String expandOverride(const String &raw) {
                if (raw.isEmpty()) return raw;
                if (looksLikePath(raw)) return raw;
                return whisperModelFile(raw).toString();
        }

        // Walks the fallback tier list and returns the first model
        // file that exists on disk, or an empty path when none of
        // the tiers are present.  Also reports back via @p outTier
        // which tier matched, for logging.
        String findFallbackModel(String *outTier) {
                for (size_t i = 0; i < kFallbackTiersCount; ++i) {
                        FilePath candidate = whisperModelFile(kFallbackTiers[i]);
                        if (candidate.exists()) {
                                if (outTier) *outTier = kFallbackTiers[i];
                                return candidate.toString();
                        }
                }
                if (outTier) outTier->clear();
                return String();
        }

        // Resolves the model path to load, in priority order:
        //
        //   1. @p hint  — MediaConfig::TranscriptionModelHint as set by
        //                 the caller.  Absolute path, relative path, or
        //                 bare model name; the latter resolves under
        //                 Dir::models()/whisper/.
        //   2. $PROMEKI_WHISPER_MODEL env var.  Same expansion rules
        //                 as the config hint.
        //   3. The "default" link in Dir::models()/whisper/ — an OS
        //                 symlink or a libpromeki pseudo-symlink.
        //   4. Dir::models()/whisper/ggml-small.bin if present (the
        //                 canonical default model).
        //   5. The closest available model from kFallbackTiers,
        //                 preferring next-better over next-smaller.
        //
        // Every successful resolution emits a single promekiInfo
        // line naming the model and the rule that picked it so
        // diagnostic logs make the choice unambiguous.  An empty
        // return means "no model usable" and the caller (ensureContext)
        // surfaces an Error::NotExist.
        String resolveModelPath(const String &hint) {
                // 1. Config hint wins outright.
                if (!hint.isEmpty()) {
                        String resolved = expandOverride(hint);
                        promekiInfo("WhisperEngine: model from MediaConfig::TranscriptionModelHint "
                                    "(hint='%s') -> %s",
                                    hint.cstr(), resolved.cstr());
                        return resolved;
                }

                // 2. Environment override.  Read via the library Env
                // wrapper rather than std::getenv directly so it goes
                // through the same path as other PROMEKI_* knobs
                // (and so tests can stub it later if needed).
                String env = Env::get(kEnvVarModel);
                if (!env.isEmpty()) {
                        String resolved = expandOverride(env);
                        promekiInfo("WhisperEngine: model from %s env (raw='%s') -> %s",
                                    kEnvVarModel, env.cstr(), resolved.cstr());
                        return resolved;
                }

                // 3. "default" link in the whisper model directory.
                FilePath whisperDir = Dir::models().path() / kWhisperSubdir;
                FilePath defaultLink = whisperDir / kDefaultLinkName;
                if (defaultLink.isLink()) {
                        Result<FilePath> r = defaultLink.readLink();
                        if (r.second().isOk()) {
                                FilePath target = r.first();
                                // Mirror OS symlink semantics: a
                                // relative target resolves against the
                                // link's parent (the whisper dir).
                                if (target.isRelative()) target = whisperDir / target;
                                promekiInfo("WhisperEngine: model from '%s' link in %s -> %s",
                                            kDefaultLinkName.cstr(),
                                            whisperDir.toString().cstr(),
                                            target.toString().cstr());
                                return target.toString();
                        }
                        promekiWarn("WhisperEngine: '%s' link present at %s but unreadable; "
                                    "falling through to size-tier fallback",
                                    kDefaultLinkName.cstr(),
                                    defaultLink.toString().cstr());
                }

                // 4 + 5. Walk the fallback tier list (which starts at
                // "small" — covers the user's "use small if it exists"
                // rule — and degrades from there preferring "closest
                // to small, preferring better" semantics).
                String matchedTier;
                String fallback = findFallbackModel(&matchedTier);
                if (!fallback.isEmpty()) {
                        if (matchedTier == "small") {
                                promekiInfo("WhisperEngine: using canonical default model 'small' -> %s",
                                            fallback.cstr());
                        } else {
                                promekiInfo("WhisperEngine: 'small' not staged in %s; "
                                            "fallback tier '%s' -> %s",
                                            (Dir::models().path() / kWhisperSubdir).toString().cstr(),
                                            matchedTier.cstr(), fallback.cstr());
                        }
                        return fallback;
                }

                // No model on disk at all.  Return the canonical
                // "small" path so the caller's not-exist error names
                // a concrete file the user is likely to want to stage.
                FilePath canonical = whisperModelFile("small");
                promekiWarn("WhisperEngine: no whisper model staged under %s; "
                            "expected at %s (use promeki-fetch-model to install)",
                            (Dir::models().path() / kWhisperSubdir).toString().cstr(),
                            canonical.toString().cstr());
                return canonical.toString();
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
                                const size_t available = _accumulator.isValid() ? _accumulator.available() : 0;
                                if (available == 0) {
                                        // Nothing to transcribe — flush is a no-op but not
                                        // an error.  receiveFrame() will return an invalid
                                        // Frame immediately.
                                        return Error::Ok;
                                }
                                if (!ensureContext()) {
                                        return _lastError;
                                }

                                // Drain the accumulator ring into a flat scratch buffer.
                                // whisper_full needs a single contiguous float* spanning
                                // the whole utterance, so the pop into _flushScratch is
                                // unavoidable; the persistent List grows over the session's
                                // peak utterance length and stays there for reuse.
                                _flushScratch.resize(available);
                                auto popRes = _accumulator.pop(_flushScratch.data(), available);
                                const size_t popped = popRes.first();
                                if (popRes.second() != Error::Ok || popped != available) {
                                        promekiWarn("WhisperEngine::flush: AudioBuffer pop returned %zu/%zu (err=%s)",
                                                    popped, available, popRes.second().name().cstr());
                                        setError(popRes.second() != Error::Ok ? popRes.second() : Error::LibraryFailure,
                                                 "AudioBuffer accumulator drain underran");
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
                                // whisper_full_default_params() caps n_threads at
                                // min(4, hardware_concurrency()) — fine for low-end
                                // boxes but pins us to four cores on anything
                                // beefier.  Override with the library's
                                // BasicThread::idealThreadCount() (wraps
                                // std::thread::hardware_concurrency, returns 0 on
                                // failure) so the encoder GEMMs see the whole CPU.
                                {
                                        const unsigned int hwc = BasicThread::idealThreadCount();
                                        params.n_threads = hwc > 0 ? static_cast<int>(hwc) : 4;
                                }

                                int rc = whisper_full(_ctx, params, _flushScratch.data(),
                                                      static_cast<int>(popped));
                                if (rc != 0) {
                                        promekiWarn("WhisperEngine::flush: whisper_full failed (rc=%d, samples=%zu)",
                                                    rc, popped);
                                        setError(Error::DecodeFailed,
                                                 String::sprintf("whisper_full failed (rc=%d)", rc));
                                        return _lastError;
                                }
                                drainSegments();
                                // Reset in-flight state: the batch is complete.  Configuration
                                // and the loaded whisper context are preserved so the next
                                // session can reuse them.  _flushScratch keeps its capacity
                                // so the next flush amortises the allocation.
                                _anchor = MediaTimeStamp();
                                return Error::Ok;
                        }

                        Error reset() override {
                                clearError();
                                if (_accumulator.isValid()) _accumulator.clear();
                                _pending.clear();
                                _anchor = MediaTimeStamp();
                                // Keep _ctx and the accumulator's storage alive — the model
                                // is expensive to load (hundreds of MB) and the ring's
                                // backing buffer is cheap to reuse.  AudioBuffer::clear
                                // resets the ring head/tail and PTS anchors but leaves the
                                // storage capacity in place.
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

                        // Sets up / reconfigures the per-session AudioBuffer accumulator.
                        // The storage format is fixed at 16 kHz mono Float32LE (whisper's
                        // canonical input), so the only thing that varies between sessions
                        // is the input sample rate.  AudioBuffer handles the actual sample
                        // rate conversion + ring storage internally; SincFastest is plenty
                        // since whisper is robust to lower-quality resampling and burning
                        // cycles on a higher tier buys zero accuracy here.
                        bool ensureAccumulator(float inputRate) {
                                if (inputRate <= 0.0f) {
                                        setError(Error::Invalid, "PCM payload has zero sample rate");
                                        return false;
                                }
                                if (_accumulator.isValid() && _inputRate == inputRate) {
                                        return true;
                                }
                                if (!_accumulator.isValid()) {
                                        AudioDesc storage(AudioFormat::PCMI_Float32LE,
                                                          kWhisperSampleRate, 1u);
                                        _accumulator = AudioBuffer(storage);
                                        // ~30 s headroom up front (whisper's encoder window).
                                        // appendPayload grows on demand via reserve() if the
                                        // utterance runs longer than that.
                                        _accumulator.reserve(static_cast<size_t>(kWhisperSampleRate) * 30u);
                                        Error qerr = _accumulator.setResamplerQuality(SrcQuality::SincFastest);
                                        if (qerr != Error::Ok && qerr != Error::NotSupported) {
                                                setError(qerr, "AudioBuffer::setResamplerQuality failed");
                                                return false;
                                        }
                                }
                                AudioDesc input(AudioFormat::PCMI_Float32LE, inputRate, 1u);
                                _accumulator.setInputFormat(input);
                                _inputRate = inputRate;
                                return true;
                        }

                        // Ensures the accumulator has room for at least @p extraSamples
                        // *input* samples worth of output (so an upward resample doesn't
                        // hit NoSpace mid-push).  Cheap when already large enough.
                        bool ensureAccumulatorRoom(size_t extraInputSamples, float inputRate) {
                                const double upRatio = inputRate > 0.0f
                                                               ? std::max(1.0,
                                                                          static_cast<double>(kWhisperSampleRate) /
                                                                                  static_cast<double>(inputRate))
                                                               : 1.0;
                                const size_t needOutputRoom =
                                        static_cast<size_t>(std::ceil(static_cast<double>(extraInputSamples) * upRatio)) +
                                        64u;
                                if (_accumulator.free() >= needOutputRoom) return true;
                                const size_t newCap = _accumulator.available() + needOutputRoom;
                                Error err = _accumulator.reserve(newCap);
                                if (err != Error::Ok) {
                                        setError(err, "AudioBuffer::reserve failed");
                                        return false;
                                }
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
                                // been targeting a different audio stream.  _monoScratch is
                                // a persistent member so the buffer's storage is reused
                                // across submits and only grows on the rare peak.
                                List<int> channelSel = selectChannels(payload, this->config());
                                if (channelSel.isEmpty()) {
                                        return Error::Ok;
                                }
                                _monoScratch.resize(frames);
                                if (channels == 1 && channelSel.size() == 1 && channelSel.at(0) == 0) {
                                        std::memcpy(_monoScratch.data(), interleaved, frames * sizeof(float));
                                } else {
                                        downmixFloat32(interleaved, frames, channels, channelSel,
                                                       _monoScratch.data());
                                }

                                // Step 3: hand the mono float scratch to the AudioBuffer
                                // accumulator.  AudioBuffer transparently resamples to
                                // 16 kHz mono (or short-circuits when sampleRate already
                                // matches the storage rate) and stores the result in its
                                // ring.  Reserve enough output headroom for an
                                // upward-resampling worst case before pushing so the ring
                                // never returns NoSpace mid-utterance.
                                if (!ensureAccumulator(sampleRate)) return _lastError;
                                if (!ensureAccumulatorRoom(frames, sampleRate)) return _lastError;

                                AudioDesc monoInput(AudioFormat::PCMI_Float32LE, sampleRate, 1u);
                                Error err = _accumulator.push(_monoScratch.data(), frames, monoInput);
                                if (err != Error::Ok) {
                                        return err;
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

                        // Per-session 16 kHz mono Float32 accumulator (lazy; lives
                        // for the engine's lifetime once primed so the ring storage
                        // amortises across sessions).  AudioBuffer handles the
                        // sample-rate conversion and ring storage; we only feed it
                        // downmixed mono float scratch.  _inputRate tracks the
                        // currently-installed input rate so we only reconfigure on
                        // an actual change.
                        AudioBuffer  _accumulator;
                        float        _inputRate = 0.0f;
                        List<float>  _monoScratch;   // Persistent downmix scratch.
                        List<float>  _flushScratch;  // Persistent drain buffer fed to whisper_full.

                        // Per-session in-flight state.
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
