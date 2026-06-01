/**
 * @file      cases/codec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Codec encode/decode roundtrip tests with no file in between.
 *
 * Each (codec, backend) pair where both an encoder AND decoder
 * implementation are registered becomes one @ref TestCase named
 *
 *   codec.<codec>.<backend>
 *
 * The test pipeline is:
 *
 *   TPG → VideoEncoder(codec, backend) → VideoDecoder(codec, backend) → Inspector
 *
 * No QuickTime / ImageFile container in between, so this isolates the
 * codec's compress→decompress fidelity from any container-mux quirk.
 * That's the same matrix @c scripts/roundtrip-codecs.sh exercises
 * through @c mediaplay, but driven directly through MediaPipeline so
 * the inspector's per-frame stamp validation (LTC, picture data,
 * audio MediaTimeStamp, A/V drift) is available — exit-code
 * classification alone tells you "did it run", not "did the audio
 * and video survive".
 *
 * Skip vs. fail policy mirrors the file-based roundtrip suite:
 *
 *   - Build / open / start failures classify as Skip (the codec is
 *     advertised but the planner couldn't wire it up in this build).
 *   - Mid-stream pipeline errors after a successful start, missing
 *     frames at the inspector, and discontinuity-count overruns are
 *     Fail.
 *   - Watchdog timeouts surface as Timeout.
 *
 * Audio-only tests don't fit here — the encoder/decoder this file
 * configures is the @c VideoEncoder / @c VideoDecoder pair, so audio
 * essence flows around the encoder pair to the inspector via the
 * planner's auto-route.  The inspector still validates the audio
 * timestamps it receives directly from TPG, so an audio-side problem
 * (missed AV anchor, sample-count drift) shows up as a discontinuity
 * even when the codec only touches video.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/framecount.h>
#include <promeki/inspectormediaio.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/string.h>
#include <promeki/videocodec.h>

#include <functional>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // -------------------------------------------------------------------
                // Case description
                // -------------------------------------------------------------------

                struct Case {
                                String              name;       // dotted identifier registered with TestRunner
                                VideoCodec::ID      codecId = VideoCodec::Invalid;
                                VideoCodec::Backend encBackend; // pinned encoder backend handle
                                VideoCodec::Backend decBackend; // pinned decoder backend handle
                };

                // -------------------------------------------------------------------
                // Naming
                // -------------------------------------------------------------------

                // Lowercase identifier-safe slug (same rules as the
                // roundtrip suite — see notes there).
                String slug(const String &s) {
                        String out;
                        for (size_t i = 0; i < s.byteCount(); ++i) {
                                char c = s.cstr()[i];
                                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
                                        out += c;
                                } else if (c == '-' || c == '+' || c == '.') {
                                        out += '_';
                                }
                        }
                        return out;
                }

                // Matched pairs keep the historic single-token name
                // (@c codec.<codec>.<backend>); cross-backend pairs spell
                // out both ends (@c codec.<codec>.<enc>-<dec>) so the
                // intent — e.g. x264-encode decoded by NVDEC — is legible
                // in the test ID.
                String buildCaseName(const VideoCodec &codec, const VideoCodec::Backend &encBackend,
                                     const VideoCodec::Backend &decBackend) {
                        String tail = slug(encBackend.name());
                        if (!(encBackend == decBackend)) tail += String("-") + slug(decBackend.name());
                        return String("codec.") + slug(codec.name()) + String(".") + tail;
                }

                // -------------------------------------------------------------------
                // Matrix builder
                // -------------------------------------------------------------------

                // Walk every registered video codec and emit one Case per
                // (encoder backend × decoder backend) pair.  A codec with
                // one encoder and one decoder yields a single matched pair;
                // a codec with multiple encoder backends (e.g. H.264 with
                // NVENC + x264, both decoded by NVDEC) yields every
                // cross-backend combination — so the matrix deliberately
                // exercises both nvenc→nvdec and x264→nvdec rather than only
                // matched same-backend pairs.  Encode-only / decode-only
                // backends drop out naturally: a codec missing either side
                // contributes an empty cross-product (the @c codec suite
                // advertises only runnable round-trips — audit full codec
                // coverage via @c mediaplay --list-codecs).
                List<Case> buildMatrix() {
                        List<Case>         matrix;
                        VideoCodec::IDList ids = VideoCodec::registeredIDs();
                        for (size_t i = 0; i < ids.size(); ++i) {
                                VideoCodec codec(ids[i]);
                                if (!codec.isValid()) continue;

                                VideoCodec::BackendList encBackends = codec.availableEncoderBackends();
                                VideoCodec::BackendList decBackends = codec.availableDecoderBackends();

                                for (size_t j = 0; j < encBackends.size(); ++j) {
                                        for (size_t k = 0; k < decBackends.size(); ++k) {
                                                Case c;
                                                c.codecId = codec.id();
                                                c.encBackend = encBackends[j];
                                                c.decBackend = decBackends[k];
                                                c.name = buildCaseName(codec, c.encBackend, c.decBackend);
                                                matrix.pushToBack(c);
                                        }
                                }
                        }
                        return matrix;
                }

                // -------------------------------------------------------------------
                // Pipeline assembly
                // -------------------------------------------------------------------

                // TPG → encoder → decoder → Inspector, fully in-memory.
                // The encoder and decoder pin their own backends, which may
                // differ (e.g. an x264 encoder feeding an NVDEC decoder) so
                // cross-backend interop is exercised, not just matched
                // same-backend pairs.  When @p tpgPixelFormat is valid
                // (typically set via the @c Codec.TpgPixelFormat CLI
                // parameter) TPG produces frames in that format directly;
                // the planner adds a CSC stage if the encoder needs a
                // different input.
                MediaPipelineConfig buildPipelineConfig(const Case &c, int frames, uint32_t streamId,
                                                        const PixelFormat &tpgPixelFormat) {
                        VideoCodec encCodec(c.codecId, c.encBackend);
                        VideoCodec decCodec(c.codecId, c.decBackend);

                        MediaPipelineConfig cfg;
                        cfg.addStage(makeTpgStage(streamId, tpgPixelFormat));
                        cfg.addStage(makeEncoderStage(encCodec));
                        cfg.addStage(makeDecoderStage(decCodec));
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(String("tpg"), String("enc"));
                        cfg.addRoute(String("enc"), String("dec"));
                        cfg.addRoute(String("dec"), String("insp"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runCodecCase(const Case &c, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 10000);

                        // Optional TPG pixel format override.  When the
                        // user passes @c -p Codec.TpgPixelFormat=<name>
                        // the value lands here as a raw String; we
                        // resolve it to a @ref PixelFormat by name.
                        // Unknown names are surfaced as a Skip so the
                        // user notices the typo rather than silently
                        // running with the default.
                        PixelFormat tpgPixelFormat;
                        {
                                TestParams::ID pfId("Codec.TpgPixelFormat");
                                String         pfName = ctx.params().getAs<String>(pfId, String());
                                if (!pfName.isEmpty()) {
                                        tpgPixelFormat = PixelFormat::lookup(pfName);
                                        if (!tpgPixelFormat.isValid()) {
                                                ctx.setSkip(String("unknown PixelFormat for "
                                                                   "Codec.TpgPixelFormat: '") +
                                                            pfName + String("'"));
                                                return;
                                        }
                                        if (tpgPixelFormat.isCompressed()) {
                                                ctx.setSkip(
                                                        String("Codec.TpgPixelFormat must name an "
                                                               "uncompressed PixelFormat (got '") +
                                                        pfName + String("')"));
                                                return;
                                        }
                                }
                        }

                        VideoCodec codec(c.codecId);
                        ctx.setDetail(String("codec"), codec.name());
                        ctx.setDetail(String("encBackend"), c.encBackend.name());
                        ctx.setDetail(String("decBackend"), c.decBackend.name());
                        ctx.setDetail(String("frames"), int64_t(frames));
                        if (tpgPixelFormat.isValid()) {
                                ctx.setDetail(String("tpgPixelFormat"), tpgPixelFormat.name());
                        }

                        // Stream IDs are arbitrary but must not collide
                        // across cases that run back-to-back; hashing
                        // the dotted name gives a stable per-case value.
                        uint32_t streamId =
                                0xC0DE0000u ^ static_cast<uint32_t>(c.name.hash());

                        // The inspector is constructed and injected so we
                        // can pull a snapshot once the close cascade
                        // completes — same trick the file-based roundtrip
                        // suite uses.
                        InspectorMediaIO *insp = new InspectorMediaIO();
                        {
                                MediaIO::Config inspCfg = MediaIOFactory::defaultConfig("Inspector");
                                inspCfg.set(MediaConfig::Type, String("Inspector"));
                                insp->setConfig(inspCfg);
                        }

                        int64_t framesProcessed = 0;
                        int64_t framesWithPictureData = 0;
                        int64_t framesWithAudioTimestamp = 0;
                        int64_t totalDiscontinuities = 0;
                        {
                                MediaPipelineConfig cfg = buildPipelineConfig(c, frames, streamId, tpgPixelFormat);
                                MediaPipeline       pipe;
                                insp->setName(String("insp"));
                                Error ie = pipe.injectStage(insp);
                                if (ie.isError()) {
                                        ctx.setFail(String("injectStage: ") + ie.desc());
                                        delete insp;
                                        return;
                                }

                                PhaseOutcome p = runPhase(pipe, cfg, loop, (unsigned int)timeoutMs);

                                // Persist the resolved pipeline graph
                                // BEFORE we evaluate pass/fail, so even
                                // a build/open failure that produced a
                                // graph at all still records what we
                                // tried.  An empty resolvedConfig (build
                                // bailed out early) just doesn't show
                                // up in the JSON.
                                if (p.resolvedConfig.size() > 0) {
                                        ctx.setPipelineConfig(p.resolvedConfig);
                                }

                                InspectorSnapshot snap = insp->snapshot();
                                framesProcessed = snap.framesProcessed.value();
                                framesWithPictureData = snap.framesWithPictureData.value();
                                framesWithAudioTimestamp = snap.framesWithAudioTimestamp.value();
                                totalDiscontinuities = snap.totalDiscontinuities;
                                ctx.setDetail(String("framesProcessed"), framesProcessed);
                                ctx.setDetail(String("framesWithPictureData"), framesWithPictureData);
                                ctx.setDetail(String("framesWithAudioTimestamp"), framesWithAudioTimestamp);
                                ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);

                                delete insp;

                                auto isPlannerGap = [](const Error &e) { return e == Error::NotSupported; };
                                if (!p.built) {
                                        if (isPlannerGap(p.buildError))
                                                ctx.setSkip(String("build failed: ") + p.buildError.desc());
                                        else
                                                ctx.setFail(String("build failed: ") + p.buildError.desc());
                                        return;
                                }
                                if (!p.opened) {
                                        if (isPlannerGap(p.openError))
                                                ctx.setSkip(String("open failed: ") + p.openError.desc());
                                        else
                                                ctx.setFail(String("open failed: ") + p.openError.desc());
                                        return;
                                }
                                if (!p.started) {
                                        if (isPlannerGap(p.startError))
                                                ctx.setSkip(String("start failed: ") + p.startError.desc());
                                        else
                                                ctx.setFail(String("start failed: ") + p.startError.desc());
                                        return;
                                }
                                if (p.timedOut) {
                                        ctx.setTimeout(String("phase deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        return;
                                }
                                if (p.sawError) {
                                        // A mid-stream @ref Error::NotSupported
                                        // means the codec was advertised but
                                        // the runtime can't actually wire it
                                        // up (e.g. NVENC's CUDA context
                                        // failed to retain on a host with a
                                        // driver mismatch).  That's the same
                                        // bucket as the build/open/start
                                        // planner-gap path above — Skip,
                                        // not Fail.
                                        if (p.firstError == Error::NotSupported) {
                                                ctx.setSkip(String("pipeline error: ") + p.errorDetail);
                                        } else {
                                                ctx.setFail(String("pipeline error: ") + p.errorDetail);
                                        }
                                        return;
                                }
                        }

                        if (framesProcessed <= 0) {
                                ctx.setFail(String("inspector saw no frames"));
                                return;
                        }
                        if (totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities detected in codec round-trip"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerCodecCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case c = matrix[i];
                        VideoCodec codec(c.codecId);
                        String     pair = c.encBackend == c.decBackend
                                                  ? c.encBackend.name()
                                                  : c.encBackend.name() + String(" → ") + c.decBackend.name();
                        String     desc = String("Codec roundtrip: ") + codec.name() + String(" / ") + pair +
                                      String(" (TPG → encoder → decoder → Inspector)");
                        TestRunner::registerCase(TestCase(c.name, desc,
                                                          [c](TestContext &ctx) { runCodecCase(c, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
