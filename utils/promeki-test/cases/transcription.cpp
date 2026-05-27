/**
 * @file      cases/transcription.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Speech-to-text round-trip tests driven by the @c testmedia/ corpus.
 *
 * For every entry in the testmedia index whose @c useCases list
 * contains @c "speech-to-text", this suite registers one case named
 * @c "transcription.<slug>".  The case body:
 *
 *   1. Resolves the audio asset and the expected transcript path
 *      against the testmedia root.  Skips cleanly if the audio blob
 *      is missing (lazy LFS pointer not fetched) or the expected
 *      transcript text is unavailable.
 *   2. Defers to the engine's own model resolver (config hint >
 *      @c PROMEKI_WHISPER_MODEL > @c default link >
 *      @c ggml-small.bin > size-tier fallback).  When the engine
 *      surfaces @c Error::NotExist at flush time we convert that to
 *      Skip — no point failing an STT case for a missing model on
 *      a fresh host.  See the WhisperCpp engine for the precedence
 *      details.
 *   3. Opens the asset via @ref AudioFile, reads it in PCM chunks,
 *      submits each chunk to a @c "WhisperCpp" batch
 *      @ref TranscriptionEngine, flushes, and drains every emitted
 *      Frame.
 *   4. Records the decoded transcript, the expected transcript, and
 *      a normalized word-error-rate against the per-test
 *      @c result.json so the corpus's diff over time is easy to
 *      inspect.  The pass/fail bar is intentionally loose: any
 *      non-empty finalised transcript passes, regardless of the
 *      decoder's wording fidelity.  Tighter WER assertions can be
 *      added later as the engine and prompts stabilise.
 *
 * Suite is registered unconditionally — when the corpus is absent the
 * suite no-ops without complaint, which keeps developers who do not
 * have the testmedia repo on disk from being forced to set anything
 * up just to run the rest of promeki-test.
 */

#include "cases.h"
#include "../testcontext.h"
#include "../testmedia.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/audiodesc.h>
#include <promeki/audiofile.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/dir.h>
#include <promeki/enums_transcription.h>
#include <promeki/error.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/timestamp.h>
#include <promeki/transcript.h>
#include <promeki/transcriptionengine.h>

#include <cctype>
#include <cstdlib>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                /// @brief Maximum PCM chunk size submitted per
                ///        @c submitFrame call.  Whisper accumulates the
                ///        full stream internally, so this is purely a
                ///        memory-pressure knob for the test reader —
                ///        a few seconds of audio at a time keeps the
                ///        AudioFile buffer small without forcing
                ///        thousands of submitFrame calls.
                inline constexpr size_t kSubmitChunkSamples = 16000;

                /// @brief Lowercase, ASCII-strip-punctuation, collapse
                ///        whitespace.
                ///
                /// Used by the WER computation so cosmetic differences
                /// (casing, trailing punctuation) don't blow up the
                /// metric.  Anything non-alphanumeric / non-ASCII-space
                /// becomes a space; runs of spaces collapse to one.
                /// Unicode characters above the ASCII range pass
                /// through untouched, so accented letters in the
                /// reference stay intact.
                String normalizeForWer(const String &in) {
                        String      lc = in.toLower();
                        const char *src = lc.cstr();
                        size_t      n = lc.byteCount();
                        String      out;
                        out.reserve(n);
                        bool inSpace = true; // collapse leading whitespace
                        for (size_t i = 0; i < n; ++i) {
                                unsigned char c = static_cast<unsigned char>(src[i]);
                                bool          isAscii = c < 0x80;
                                bool          isAlnum = isAscii && (std::isalnum(c) || c == '\'');
                                bool          isSpace = isAscii && std::isspace(c);
                                bool          isPunct = isAscii && !isAlnum && !isSpace;
                                if (isPunct) {
                                        if (!inSpace) {
                                                out += String(" ");
                                                inSpace = true;
                                        }
                                        continue;
                                }
                                if (isSpace) {
                                        if (!inSpace) {
                                                out += String(" ");
                                                inSpace = true;
                                        }
                                        continue;
                                }
                                out += String(reinterpret_cast<const char *>(&c), 1);
                                inSpace = false;
                        }
                        // Trim trailing space.
                        if (!out.isEmpty() && out.cstr()[out.byteCount() - 1] == ' ') {
                                out = String(out.cstr(), out.byteCount() - 1);
                        }
                        return out;
                }

                /// @brief Tokenises @p s on ASCII whitespace.
                StringList tokens(const String &s) {
                        StringList out;
                        const char *p = s.cstr();
                        size_t      n = s.byteCount();
                        size_t      start = 0;
                        bool        inTok = false;
                        for (size_t i = 0; i < n; ++i) {
                                bool ws = static_cast<unsigned char>(p[i]) < 0x80 && std::isspace(p[i]);
                                if (ws) {
                                        if (inTok) {
                                                out.pushToBack(String(p + start, i - start));
                                                inTok = false;
                                        }
                                } else {
                                        if (!inTok) {
                                                start = i;
                                                inTok = true;
                                        }
                                }
                        }
                        if (inTok) out.pushToBack(String(p + start, n - start));
                        return out;
                }

                /// @brief Levenshtein word edit distance between two
                ///        token lists, scaled to the reference length
                ///        to produce a Word Error Rate in @c [0, ...).
                ///
                /// Reference (expected) tokens are @p ref; hypothesis
                /// (engine output) tokens are @p hyp.  WER above 1.0
                /// is possible (more insertions than reference words);
                /// callers can clip if a UI cap is desired.  Returns
                /// 0.0 when @p ref is empty (no signal to score).
                double wordErrorRate(const StringList &ref, const StringList &hyp) {
                        const size_t m = ref.size();
                        const size_t n = hyp.size();
                        if (m == 0) return 0.0;
                        // Two-row DP — full matrix isn't needed for the
                        // scalar score.
                        List<size_t> prev;
                        List<size_t> cur;
                        prev.reserve(n + 1);
                        cur.reserve(n + 1);
                        for (size_t j = 0; j <= n; ++j) prev.pushToBack(j);
                        for (size_t i = 1; i <= m; ++i) {
                                cur.clear();
                                cur.pushToBack(i);
                                for (size_t j = 1; j <= n; ++j) {
                                        size_t cost = (ref[i - 1] == hyp[j - 1]) ? 0u : 1u;
                                        size_t del = prev[j] + 1;
                                        size_t ins = cur[j - 1] + 1;
                                        size_t sub = prev[j - 1] + cost;
                                        size_t v = del < ins ? del : ins;
                                        if (sub < v) v = sub;
                                        cur.pushToBack(v);
                                }
                                prev = cur;
                        }
                        return static_cast<double>(prev[n]) / static_cast<double>(m);
                }

                /// @brief Joins a list of @ref Transcript entries into
                ///        a single space-separated string.
                ///
                /// Each transcript carries its own @ref Transcript::text
                /// (space-joined word texts); we concatenate those with
                /// a single space between utterances so multi-segment
                /// emissions read like one continuous transcript.
                String joinTranscripts(const List<Transcript> &items) {
                        String out;
                        for (size_t i = 0; i < items.size(); ++i) {
                                const String &t = items[i].text();
                                if (t.isEmpty()) continue;
                                if (!out.isEmpty()) out += String(" ");
                                out += t;
                        }
                        return out;
                }

                /// @brief Reads the full body of @p path as a UTF-8
                ///        String.
                ///
                /// Returns an empty String + sets @p outErr on any
                /// failure.  Used to slurp the small expected-output
                /// text files that ride next to each audio asset.
                String readWholeTextFile(const String &path, String *outErr) {
                        File f(path);
                        if (f.open(IODevice::ReadOnly).isError()) {
                                if (outErr) *outErr = String("cannot open '") + path + String("'");
                                return String();
                        }
                        Result<int64_t> szR = f.size();
                        if (szR.second().isError()) {
                                f.close();
                                if (outErr) *outErr = String("cannot stat '") + path + String("'");
                                return String();
                        }
                        const int64_t sz = szR.first();
                        if (sz <= 0) {
                                f.close();
                                return String();
                        }
                        Buffer buf(static_cast<size_t>(sz));
                        buf.setSize(static_cast<size_t>(sz));
                        f.read(buf.data(), sz);
                        f.close();
                        return String::fromUtf8(static_cast<const char *>(buf.data()),
                                                static_cast<size_t>(sz));
                }

                /// @brief Builds the dotted case name from a
                ///        speech-to-text asset path.
                ///
                /// Strips the @c "audio/speech-to-text/" prefix when
                /// present (so the language sub-folder becomes a
                /// dotted segment), drops the file extension, and
                /// replaces every path separator with a dot.  Falls
                /// back to the bare stem if the asset is somewhere
                /// else in the tree.  The leading @c "transcription."
                /// pins the case under that suite for the runner's
                /// regex filter.
                String dottedCaseName(const FilePath &relPath) {
                        String s = relPath.toString();
                        const String prefix("audio/speech-to-text/");
                        if (s.startsWith(prefix)) {
                                s = String(s.cstr() + prefix.byteCount(),
                                           s.byteCount() - prefix.byteCount());
                        }
                        // Strip the trailing extension when one is
                        // present after the last slash (so the test
                        // name doesn't carry the @c .mp3 / @c .flac
                        // suffix).  String::rfind returns
                        // @c String::npos when not found, which
                        // compares safely against the slash position.
                        const size_t lastDot = s.rfind(String("."));
                        const size_t lastSlash = s.rfind(String("/"));
                        const bool   haveDot = (lastDot != String::npos);
                        const bool   haveSlash = (lastSlash != String::npos);
                        if (haveDot && (!haveSlash || lastDot > lastSlash) && lastDot > 0) {
                                s = String(s.cstr(), lastDot);
                        }
                        // Slashes become dots so the case name is
                        // single-token from the runner's perspective.
                        s = s.replace(String("/"), String("."));
                        return String("transcription.") + s;
                }

                // -------------------------------------------------------------
                // Per-case body
                // -------------------------------------------------------------

                /// @brief Builds a @ref MediaConfig configured for the
                ///        WhisperCpp batch backend.
                ///
                /// Intentionally omits @c TranscriptionModelHint —
                /// the engine's own resolver handles fallback through
                /// @c PROMEKI_WHISPER_MODEL, the @c default symlink in
                /// @c Dir::models()/whisper/, and the canonical
                /// fallback tier list.  Tests that want to pin a
                /// specific model can still set the hint via
                /// @c -p TranscriptionModelHint=... once that wiring
                /// lands; the suite logs what the engine actually
                /// chose at @c promekiInfo level either way.
                MediaConfig buildEngineConfig(const String &languageHint) {
                        MediaConfig cfg;
                        cfg.set(MediaConfig::TranscriptionSessionMode,
                                Variant(TranscriptionMode(TranscriptionMode::Batch)));
                        if (!languageHint.isEmpty()) {
                                cfg.set(MediaConfig::TranscriptionLanguage, Variant(languageHint));
                        }
                        cfg.set(MediaConfig::TranscriptionChannelMode,
                                Variant(TranscriptionChannelMode(TranscriptionChannelMode::DownmixAll)));
                        return cfg;
                }

                /// @brief Pulls a language hint out of an asset's tags.
                ///
                /// The testmedia sidecars carry BCP-47 / language slugs
                /// like @c "english", @c "en-US", @c "en", etc. in their
                /// @c tags array.  We surface a short hint (@c "en",
                /// @c "es" …) to Whisper when one is obvious; otherwise
                /// we leave the field empty and let the engine
                /// language-detect.  The mapping is intentionally
                /// conservative — better to under-specify than to send
                /// the engine a wrong hint.
                String languageHintForEntry(const TestMediaEntry &entry) {
                        for (size_t i = 0; i < entry.tags.size(); ++i) {
                                const String &t = entry.tags[i].toLower();
                                if (t == "english" || t == "en" || t.startsWith("en-")) return String("en");
                                if (t == "spanish" || t == "es" || t.startsWith("es-")) return String("es");
                                if (t == "french" || t == "fr" || t.startsWith("fr-")) return String("fr");
                                if (t == "german" || t == "de" || t.startsWith("de-")) return String("de");
                                if (t == "italian" || t == "it" || t.startsWith("it-")) return String("it");
                                if (t == "portuguese" || t == "pt" || t.startsWith("pt-")) return String("pt");
                                if (t == "japanese" || t == "ja" || t.startsWith("ja-")) return String("ja");
                                if (t == "korean" || t == "ko" || t.startsWith("ko-")) return String("ko");
                                if (t == "chinese" || t == "zh" || t.startsWith("zh-")) return String("zh");
                        }
                        return String();
                }

                /// @brief Per-asset test body.
                void runTranscriptionCase(const TestMediaEntry &entry, TestContext &ctx) {
                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 10000);
                        (void)timeoutMs; // currently advisory — whisper runs synchronously here

                        ctx.setDetail(String("assetPath"), entry.path.toString());
                        ctx.setDetail(String("relPath"), entry.relPath.toString());
                        if (!entry.title.isEmpty()) ctx.setDetail(String("title"), entry.title);

                        // -- 1. Pre-flight: backend + audio asset ----------
                        // Model resolution is the engine's job (config
                        // hint > PROMEKI_WHISPER_MODEL > default link
                        // > size-tier fallback); a missing model
                        // surfaces below as an ensureContext failure
                        // on flush() and we convert that to Skip there.
                        auto backend = TranscriptionEngine::lookupBackend(String("WhisperCpp"));
                        if (error(backend).isError()) {
                                ctx.setSkip(String("WhisperCpp backend not registered"));
                                return;
                        }

                        if (!entry.path.exists()) {
                                ctx.setSkip(String("audio blob not on disk: ") + entry.path.toString() +
                                            String(" (LFS pointer? run testmedia/media.sh fetch ...)"));
                                return;
                        }

                        // Expected transcript — without one we can still
                        // run the engine, but a Pass / Fail comparison
                        // becomes meaningless.  Skip rather than Fail
                        // when the corpus omits a reference.
                        const TestMediaExpectedOutput exp = findExpectedOutput(entry, String("transcript"));
                        if (exp.path.toString().isEmpty() || !exp.path.exists()) {
                                ctx.setSkip(String("expected transcript not available for ") +
                                            entry.relPath.toString());
                                return;
                        }

                        String expectedErr;
                        const String expectedText = readWholeTextFile(exp.path.toString(), &expectedErr);
                        if (expectedText.isEmpty()) {
                                ctx.setSkip(String("expected transcript empty / unreadable: ") +
                                            (expectedErr.isEmpty() ? exp.path.toString() : expectedErr));
                                return;
                        }
                        ctx.setDetail(String("expectedTranscriptPath"), exp.path.toString());
                        ctx.setDetail(String("expectedTranscript"), expectedText);

                        // -- 2. Open the audio file ------------------------
                        AudioFile reader = AudioFile::createReader(entry.path.toString());
                        if (!reader.isValid()) {
                                ctx.setSkip(String("no AudioFile backend for ") + entry.path.toString());
                                return;
                        }
                        Error oe = reader.open();
                        if (oe.isError()) {
                                if (oe == Error::NotSupported) {
                                        ctx.setSkip(String("AudioFile open NotSupported (codec gap?): ") +
                                                    oe.desc());
                                        return;
                                }
                                ctx.setFail(String("AudioFile open failed: ") + oe.desc());
                                return;
                        }
                        const AudioDesc desc = reader.desc();
                        const float     sampleRate = desc.sampleRate();
                        const uint32_t  channels = desc.channels();
                        ctx.setDetail(String("sampleRate"), int64_t(sampleRate));
                        ctx.setDetail(String("channels"), int64_t(channels));
                        ctx.setDetail(String("audioFormat"), desc.format().name());
                        ctx.setDetail(String("totalSamples"), int64_t(reader.sampleCount()));

                        const String languageHint = languageHintForEntry(entry);
                        if (!languageHint.isEmpty())
                                ctx.setDetail(String("languageHint"), languageHint);

                        // -- 3. Build the engine ---------------------------
                        MediaConfig engineCfg = buildEngineConfig(languageHint);
                        auto        engineRes = TranscriptionEngine::create(String("WhisperCpp"), &engineCfg);
                        if (error(engineRes).isError()) {
                                reader.close();
                                const Error e = error(engineRes);
                                if (e == Error::NotSupported) {
                                        ctx.setSkip(String("WhisperCpp create returned NotSupported: ") +
                                                    e.desc());
                                } else {
                                        ctx.setFail(String("WhisperCpp create failed: ") + e.desc());
                                }
                                return;
                        }
                        TranscriptionEngine::UPtr engine = std::move(engineRes.first());

                        // -- 4. Feed PCM in chunks -------------------------
                        size_t  totalSamplesFed = 0;
                        int64_t chunkPtsNs = 0;
                        for (;;) {
                                PcmAudioPayload::Ptr chunk;
                                Error                re = reader.read(chunk, kSubmitChunkSamples);
                                if (re == Error::EndOfFile) break;
                                if (re.isError()) {
                                        reader.close();
                                        ctx.setFail(String("AudioFile read failed at sample ") +
                                                    String::number(int64_t(totalSamplesFed)) + String(": ") +
                                                    re.desc());
                                        return;
                                }
                                if (!chunk.isValid() || chunk->sampleCount() == 0) break;

                                chunk.modify()->setPts(MediaTimeStamp(TimeStamp(chunkPtsNs),
                                                                     ClockDomain::Synthetic));
                                Frame f;
                                f.addPayload(chunk);
                                Error se = engine->submitFrame(f);
                                if (se.isError()) {
                                        reader.close();
                                        ctx.setFail(String("submitFrame failed at sample ") +
                                                    String::number(int64_t(totalSamplesFed)) + String(": ") +
                                                    se.desc());
                                        return;
                                }

                                const size_t samplesThisChunk = chunk->sampleCount();
                                totalSamplesFed += samplesThisChunk;
                                if (sampleRate > 0.0f) {
                                        chunkPtsNs += static_cast<int64_t>(
                                                (static_cast<double>(samplesThisChunk) /
                                                 static_cast<double>(sampleRate)) *
                                                1'000'000'000.0);
                                }
                        }
                        reader.close();
                        ctx.setDetail(String("samplesSubmitted"), int64_t(totalSamplesFed));

                        // -- 5. Flush + drain ------------------------------
                        // The engine loads its model lazily in flush()
                        // (the only point at which we know we have
                        // audio for it to chew on), so a missing
                        // model file surfaces here as Error::NotExist.
                        // Treat that as a clean Skip so a host that
                        // simply hasn't run promeki-fetch-model yet
                        // doesn't fail the whole STT suite.
                        Error fe = engine->flush();
                        if (fe == Error::NotExist) {
                                ctx.setSkip(String("no whisper model staged: ") +
                                            engine->lastErrorMessage());
                                return;
                        }
                        if (fe.isError()) {
                                ctx.setFail(String("flush failed: ") + fe.desc());
                                return;
                        }
                        List<Transcript> received;
                        for (;;) {
                                Frame out = engine->receiveFrame();
                                if (!out.isValid()) break;
                                Variant v = out.metadata().get(Metadata::Transcript);
                                if (!v.isValid()) continue;
                                Transcript t = v.get<Transcript>();
                                if (t.partial()) continue; // skip interim hypotheses
                                received.pushToBack(t);
                        }

                        // -- 6. Compare + report ---------------------------
                        const String hypothesis = joinTranscripts(received);
                        ctx.setDetail(String("transcriptUtterances"), int64_t(received.size()));
                        ctx.setDetail(String("hypothesisTranscript"), hypothesis);

                        const String normExp = normalizeForWer(expectedText);
                        const String normHyp = normalizeForWer(hypothesis);
                        ctx.setDetail(String("normalizedExpected"), normExp);
                        ctx.setDetail(String("normalizedHypothesis"), normHyp);

                        const double wer = wordErrorRate(tokens(normExp), tokens(normHyp));
                        ctx.setDetail(String("wer"), wer);

                        if (received.isEmpty() || hypothesis.isEmpty()) {
                                ctx.setFail(String("engine emitted no finalised transcript"));
                                return;
                        }

                        // We deliberately don't fail on WER threshold —
                        // see the file header.  The metric is recorded
                        // for diffing across runs; a clearly broken
                        // engine still trips the "no transcript at all"
                        // arm above.
                        ctx.setPass();
                }

        } // namespace

        void registerTranscriptionCases(const FilePath &testmediaRoot) {
                // The CLI / env / default lookup happened in main.cpp
                // before suite registration; here we just consume the
                // resolved root.  Empty means the corpus was not found
                // — emit a one-liner explaining and bail.
                if (testmediaRoot.toString().isEmpty()) {
                        promekiInfo("transcription: skipping suite — no testmedia root resolved "
                                    "(pass --testmedia or set PROMEKI_TESTMEDIA)");
                        return;
                }
                const FilePath root = testmediaRoot;

                List<TestMediaEntry> entries;
                String               loadErr;
                if (!loadTestMediaIndex(root, entries, &loadErr)) {
                        promekiWarn("transcription: testmedia index load failed: %s",
                                    loadErr.cstr());
                        return;
                }

                List<TestMediaEntry> stt = filterByUseCase(entries, String("speech-to-text"));
                if (stt.isEmpty()) {
                        promekiInfo("transcription: no speech-to-text entries in testmedia index "
                                    "(corpus has %zu total)",
                                    static_cast<size_t>(entries.size()));
                        return;
                }

                for (size_t i = 0; i < stt.size(); ++i) {
                        const TestMediaEntry &e = stt[i];
                        const String          name = dottedCaseName(e.relPath);
                        String desc = String("STT: ") + (e.title.isEmpty()
                                                                 ? e.relPath.toString()
                                                                 : e.title) +
                                      String(" → WhisperCpp");
                        TestRunner::registerCase(TestCase(name, desc, [e](TestContext &ctx) {
                                runTranscriptionCase(e, ctx);
                        }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
