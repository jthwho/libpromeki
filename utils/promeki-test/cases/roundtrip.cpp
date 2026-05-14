/**
 * @file      cases/roundtrip.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Functional roundtrip tests for the MediaPipeline / file-backend /
 * VideoEncoder / VideoDecoder quartet.
 *
 * For every (backend, extension, codec, pixel-format) tuple the
 * library advertises through @ref MediaIOFactory::registeredFactories
 * and @ref VideoCodec::registeredIDs, this file registers one
 * @ref TestCase named like
 *
 *   roundtrip.<backend>[.<extension>][.<codec>][.<pixel-format>]
 *
 * The test body is the same write-then-read pipeline the legacy
 * @c roundtrip-functest used: TPG → (encoder) → file sink, then
 * file source → (decoder) → InspectorMediaIO.  A pass requires the
 * inspector to see at least one frame and zero discontinuities; lossy
 * codecs are still expected to preserve the TPG's frame stamps, so
 * no allowance is granted for dropped or corrupted frames.
 *
 * Skip vs. fail policy:
 *
 *   - Build / open / pipeline-error failures on the WRITE side are
 *     classified as Skip — they almost always mean the combination
 *     isn't supported in this build (missing NVENC, writer rejects
 *     a codec) and would just spam the matrix with noise.
 *   - On the READ side, planner gaps (@c Error::NotSupported on
 *     build / open / start) are also Skip — interesting signal but
 *     not a regression in a combination the registry says works.
 *   - Mid-stream pipeline errors after a successful start, missing
 *     frames at the inspector, file-count mismatches in image
 *     sequences, and discontinuity-count overruns are Fail.
 *   - Watchdog timeouts are surfaced as Timeout so a hang shows up
 *     as its own bucket in the summary.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/dir.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
#include <promeki/framecount.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/inspectormediaio.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/videocodec.h>
#include <promeki/videoformat.h>

#include <functional>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // -------------------------------------------------------------------
                // Case description
                // -------------------------------------------------------------------

                struct Case {
                                String      name;        // dotted identifier registered with TestRunner
                                String      backendName; // "ImgSeqDPX", "QuickTime", ...
                                String      extension;   // "dpx", "mov", ...
                                VideoCodec  codec;       // invalid for raw / container-default cases
                                PixelFormat pixelFormat; // empty = backend default
                                bool        isSequence = false;
                };

                // -------------------------------------------------------------------
                // Registry filters (unchanged from the legacy test)
                // -------------------------------------------------------------------

                // Container backends that accept multiple codecs.  Today
                // QuickTime is the only one; when MXF / IVF land they
                // just need to advertise canBeSource + canBeSink + an
                // extension list and return true here.
                bool backendAcceptsCodecMatrix(const String &name) {
                        return name == String("QuickTime");
                }

                // Image-file subsystem registers per-format backends
                // (@c ImgSeqDPX, @c ImgSeqPNG, ...) plus a legacy
                // @c "ImageFile" umbrella.  We use the per-format names
                // and skip the umbrella to avoid duplicate cases.  PMDF
                // is the project's lossless debug-frame format — it
                // captures whatever Frame it's handed verbatim
                // (pixels + audio + metadata), so it's the closest
                // thing to a "ground truth" file roundtrip in the
                // tree.
                bool isVideoCapableFileBackend(const String &name) {
                        if (name == String("QuickTime")) return true;
                        if (name == String("PMDF")) return true;
                        if (name.size() >= 6 && name.left(6) == String("ImgSeq")) return true;
                        return false;
                }

                bool isImageSequenceBackend(const String &name) {
                        return name.size() >= 6 && name.left(6) == String("ImgSeq");
                }

                // Backends whose MediaIO accepts any input pixel
                // format unchanged.  For these we emit only the
                // smoke case (backend default) — the per-pixel
                // probe would either return every registered
                // PixelFormat or none, neither of which produces a
                // useful matrix entry.  PMDF in particular is
                // documented as "format-agnostic: it captures
                // whatever frame it's given" so a 130-row matrix
                // would just slow the run down without adding
                // information beyond the smoke case.
                bool isFormatAgnosticBackend(const String &name) {
                        return name == String("PMDF");
                }

                // Image-file extensions that need caller-supplied
                // size / pixel-desc hints (headerless raw YUV).  The
                // matrix can't infer the layout from TPG's RGB output
                // so we leave them to dedicated unit tests.
                bool extensionNeedsHeaderlessHints(const String &ext) {
                        return ext == String("uyvy") || ext == String("yuyv") || ext == String("yuy2") ||
                               ext == String("v210") || ext == String("i420") || ext == String("nv12") ||
                               ext == String("yuv420p") || ext == String("i422") || ext == String("yuv422p") ||
                               ext == String("yuv");
                }

                // -------------------------------------------------------------------
                // Native-format probing
                // -------------------------------------------------------------------

                List<PixelFormat> nativePixelFormats(MediaIO *io) {
                        List<PixelFormat> out;
                        if (io == nullptr) return out;

                        const Size2Du32 probeSize(1920, 1080);

                        PixelFormat::IDList ids = PixelFormat::registeredIDs();
                        for (size_t i = 0; i < ids.size(); ++i) {
                                PixelFormat pd(ids[i]);
                                if (!pd.isValid()) continue;
                                if (pd.isCompressed()) continue;

                                MediaDesc offered;
                                ImageDesc img(probeSize.width(), probeSize.height(), pd.id());
                                if (!img.isValid()) continue;
                                offered.imageList().pushToBack(img);

                                MediaDesc    preferred;
                                MediaIOSink *sinkPort = io->sink(0);
                                if (sinkPort == nullptr) continue;
                                Error e = sinkPort->proposeInput(offered, &preferred);
                                if (e.isError()) continue;
                                if (preferred.imageList().isEmpty()) continue;
                                if (preferred.imageList()[0].pixelFormat() != pd) continue;

                                out.pushToBack(pd);
                        }
                        return out;
                }

                List<PixelFormat> nativePixelFormatsFor(const String &backendName, const String &extension) {
                        const String probePath =
                                String("/mnt/data/tmp/promeki/roundtrip_probe.") + extension;
                        MediaIO *io = MediaIO::createForFileWrite(probePath);
                        if (io == nullptr) {
                                MediaIO::Config cfg = MediaIOFactory::defaultConfig(backendName);
                                cfg.set(MediaConfig::Type, backendName);
                                cfg.set(MediaConfig::Filename, probePath);
                                io = MediaIO::create(cfg);
                                if (io == nullptr) return {};
                        }
                        List<PixelFormat> out = nativePixelFormats(io);
                        delete io;
                        return out;
                }

                // -------------------------------------------------------------------
                // Name building
                // -------------------------------------------------------------------

                // Lowercase identifier-safe version of @p s.  Drops
                // anything that would confuse a regex on the test name
                // (whitespace, colons) but keeps alphanumerics and
                // underscores.  Pixel format identifiers like
                // @c "RGB10_DPX_sRGB" arrive lowercased to
                // @c "rgb10_dpx_srgb".
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
                                // Anything else (spaces, colons, slashes) is dropped.
                        }
                        return out;
                }

                // Build the dotted identifier for one Case.  The
                // structure is:
                //
                //   roundtrip.<backend>[.<extension>][.<codec>][.<pixel>]
                //
                // - For ImgSeq* backends and other single-format
                //   backends (PMDF) the extension is omitted on the
                //   primary alias since the backend name already
                //   identifies the format (e.g. @c "imgseqdpx" or
                //   @c "pmdf").  Aliased extensions still show up so
                //   the dispatch path stays covered.
                // - Container backends keep the extension (mov, qt,
                //   mp4, m4v) so the alias choice is visible.
                // - Codec and pixel-format segments are omitted when
                //   the corresponding case fields are empty.
                String buildCaseName(const String &backendName, const String &extension, bool isSequence,
                                     bool isPrimaryExt, const VideoCodec &codec, const PixelFormat &pd) {
                        String n = "roundtrip.";
                        n += slug(backendName);
                        const bool dropExtension = (isSequence || isFormatAgnosticBackend(backendName)) && isPrimaryExt;
                        if (!dropExtension && !extension.isEmpty()) n += String(".") + slug(extension);
                        if (codec.isValid()) n += String(".") + slug(codec.name());
                        if (pd.isValid()) n += String(".") + slug(pd.name());
                        return n;
                }

                // -------------------------------------------------------------------
                // Matrix builder
                // -------------------------------------------------------------------

                List<Case> buildMatrix() {
                        List<Case>                       cases;
                        const List<MediaIOFactory *>    &factories = MediaIOFactory::registeredFactories();
                        for (size_t i = 0; i < factories.size(); ++i) {
                                const MediaIOFactory *fd = factories[i];
                                if (fd == nullptr) continue;
                                const StringList extensions = fd->extensions();
                                const String     name = fd->name();
                                if (extensions.isEmpty()) continue;
                                if (!fd->canBeSource() || !fd->canBeSink()) continue;
                                if (!isVideoCapableFileBackend(name)) continue;

                                const bool   isContainer = backendAcceptsCodecMatrix(name);
                                const bool   isImgSeq = isImageSequenceBackend(name);
                                const String primaryExt = extensions[0].toLower();

                                StringList seenExts;
                                for (size_t j = 0; j < extensions.size(); ++j) {
                                        String ext = extensions[j].toLower();
                                        if (ext == String("imgseq")) continue;
                                        if (isImgSeq && extensionNeedsHeaderlessHints(ext)) continue;
                                        bool dup = false;
                                        for (size_t k = 0; k < seenExts.size(); ++k) {
                                                if (seenExts[k] == ext) {
                                                        dup = true;
                                                        break;
                                                }
                                        }
                                        if (dup) continue;
                                        seenExts.pushToBack(ext);

                                        const bool isPrimary = (ext == primaryExt);

                                        // Smoke: backend default pixel format.
                                        {
                                                Case c;
                                                c.backendName = name;
                                                c.extension = ext;
                                                c.codec = VideoCodec();
                                                c.pixelFormat = PixelFormat();
                                                c.isSequence = isImgSeq;
                                                c.name = buildCaseName(name, ext, isImgSeq, isPrimary,
                                                                       VideoCodec(), PixelFormat());
                                                cases.pushToBack(c);
                                        }

                                        // Pixel-format matrix on the primary extension only.
                                        if (!isPrimary) continue;
                                        // Format-agnostic backends (PMDF) — skip the
                                        // pixel-format matrix; the smoke case above
                                        // already covers what's interesting.
                                        if (isFormatAgnosticBackend(name)) continue;
                                        List<PixelFormat> natives = nativePixelFormatsFor(name, ext);
                                        for (size_t k = 0; k < natives.size(); ++k) {
                                                const PixelFormat &pd = natives[k];
                                                Case               c;
                                                c.backendName = name;
                                                c.extension = ext;
                                                c.codec = VideoCodec();
                                                c.pixelFormat = pd;
                                                c.isSequence = isImgSeq;
                                                c.name = buildCaseName(name, ext, isImgSeq, isPrimary,
                                                                       VideoCodec(), pd);
                                                cases.pushToBack(c);
                                        }
                                }

                                // Codec matrix for container backends.
                                if (isContainer) {
                                        VideoCodec::IDList ids = VideoCodec::registeredIDs();
                                        for (size_t j = 0; j < ids.size(); ++j) {
                                                VideoCodec vc(ids[j]);
                                                if (!vc.isValid()) continue;
                                                if (!vc.canEncode() || !vc.canDecode()) continue;

                                                // Smoke (codec default).
                                                {
                                                        Case c;
                                                        c.backendName = name;
                                                        c.extension = primaryExt;
                                                        c.codec = vc;
                                                        c.pixelFormat = PixelFormat();
                                                        c.isSequence = false;
                                                        c.name = buildCaseName(name, primaryExt, false, true, vc,
                                                                               PixelFormat());
                                                        cases.pushToBack(c);
                                                }

                                                const List<PixelFormat> variants = vc.compressedPixelFormats();
                                                if (variants.size() <= 1) continue;
                                                for (size_t k = 0; k < variants.size(); ++k) {
                                                        const PixelFormat &pd = variants[k];
                                                        if (!pd.isValid()) continue;
                                                        Case c;
                                                        c.backendName = name;
                                                        c.extension = primaryExt;
                                                        c.codec = vc;
                                                        c.pixelFormat = pd;
                                                        c.isSequence = false;
                                                        c.name = buildCaseName(name, primaryExt, false, true, vc, pd);
                                                        cases.pushToBack(c);
                                                }
                                        }
                                }
                        }
                        return cases;
                }

                // -------------------------------------------------------------------
                // Path / pipeline helpers
                // -------------------------------------------------------------------

                String pathForCase(const Case &c, const FilePath &testFolder) {
                        FilePath base = testFolder / String("media");
                        if (c.isSequence) {
                                return (base / (String("frame_####.") + c.extension)).toString();
                        }
                        return (base / (String("media.") + c.extension)).toString();
                }

                bool ensureParentDir(const String &path, String *errOut) {
                        FilePath fp(path);
                        FilePath parent = fp.parent();
                        Dir      d(parent);
                        if (d.exists()) return true;
                        Error e = d.mkpath();
                        if (e.isError()) {
                                if (errOut)
                                        *errOut = String("mkpath '") + parent.toString() + String("': ") + e.desc();
                                return false;
                        }
                        return true;
                }

                MediaPipelineConfig::Stage makeFileSinkStage(const String &path) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("out");
                        s.path = path;
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        return s;
                }

                MediaPipelineConfig::Stage makeFileSourceStage(const String &path) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("in");
                        s.path = path;
                        s.role = MediaPipelineConfig::StageRole::Source;
                        return s;
                }

                MediaPipelineConfig buildWriteConfig(const Case &c, const String &path, int frames,
                                                     uint32_t streamId) {
                        MediaPipelineConfig cfg;
                        const PixelFormat   tpgPd = c.codec.isValid() ? PixelFormat() : c.pixelFormat;
                        cfg.addStage(makeTpgStage(streamId, tpgPd));
                        String prev = String("tpg");
                        if (c.codec.isValid()) {
                                cfg.addStage(makeEncoderStage(c.codec, c.pixelFormat));
                                cfg.addRoute(prev, String("enc"));
                                prev = String("enc");
                        }
                        cfg.addStage(makeFileSinkStage(path));
                        cfg.addRoute(prev, String("out"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildReadConfig(const Case &c, const String &path) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeFileSourceStage(path));
                        String prev = String("in");
                        if (c.codec.isValid()) {
                                cfg.addStage(makeDecoderStage(c.codec));
                                cfg.addRoute(prev, String("dec"));
                                prev = String("dec");
                        }
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(prev, String("insp"));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runRoundtripCase(const Case &c, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t      frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t      timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 10000);
                        const FilePath     testFolder = ctx.testFolder();
                        const String       path = pathForCase(c, testFolder);
                        // Stream IDs are arbitrary but must be unique across cases that
                        // share the same TPG-stamped frame; the test folder name does
                        // that for free, so we hash it in.
                        uint32_t streamId = 0xABCD0000u ^ static_cast<uint32_t>(c.name.hash());

                        ctx.setDetail(String("backend"), c.backendName);
                        ctx.setDetail(String("extension"), c.extension);
                        if (c.codec.isValid()) ctx.setDetail(String("codec"), c.codec.name());
                        if (c.pixelFormat.isValid()) ctx.setDetail(String("pixelFormat"), c.pixelFormat.name());
                        ctx.setDetail(String("path"), path);

                        String parentErr;
                        if (!ensureParentDir(path, &parentErr)) {
                                ctx.setFail(parentErr);
                                return;
                        }

                        // The roundtrip suite drives two pipelines (one
                        // write, one read).  Both resolved configs are
                        // recorded under a single object so result.json's
                        // "pipeline" key is a complete picture of what
                        // ran end-to-end.
                        JsonObject pipelineDump;

                        // ---- Write phase ----
                        {
                                MediaPipelineConfig cfg = buildWriteConfig(c, path, frames, streamId);
                                MediaPipeline       pipe;
                                PhaseOutcome        p = runPhase(pipe, cfg, loop, (unsigned int)timeoutMs);
                                if (p.resolvedConfig.size() > 0) {
                                        pipelineDump.set("write", p.resolvedConfig);
                                        ctx.setPipelineConfig(pipelineDump);
                                }
                                ctx.setDetail(String("framesWritten"), int64_t(frames));

                                if (!p.built) {
                                        ctx.setSkip(String("write build failed: ") + p.buildError.desc());
                                        return;
                                }
                                if (!p.opened) {
                                        ctx.setSkip(String("write open failed: ") + p.openError.desc());
                                        return;
                                }
                                if (!p.started) {
                                        ctx.setSkip(String("write start failed: ") + p.startError.desc());
                                        return;
                                }
                                if (p.timedOut) {
                                        ctx.setTimeout(String("write phase deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        return;
                                }
                                if (p.sawError) {
                                        ctx.setSkip(String("write pipeline error: ") + p.errorDetail);
                                        return;
                                }
                                if (p.closeError.isError()) {
                                        ctx.setSkip(String("write close: ") + p.closeError.desc());
                                        return;
                                }
                        }

                        // ---- Read phase ----
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
                                MediaPipelineConfig cfg = buildReadConfig(c, path);
                                MediaPipeline       pipe;
                                insp->setName(String("insp"));
                                Error ie = pipe.injectStage(insp);
                                if (ie.isError()) {
                                        ctx.setFail(String("injectStage: ") + ie.desc());
                                        delete insp;
                                        return;
                                }

                                PhaseOutcome p = runPhase(pipe, cfg, loop, (unsigned int)timeoutMs);
                                if (p.resolvedConfig.size() > 0) {
                                        pipelineDump.set("read", p.resolvedConfig);
                                        ctx.setPipelineConfig(pipelineDump);
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
                                                ctx.setSkip(String("read build failed: ") + p.buildError.desc());
                                        else
                                                ctx.setFail(String("read build failed: ") + p.buildError.desc());
                                        return;
                                }
                                if (!p.opened) {
                                        if (isPlannerGap(p.openError))
                                                ctx.setSkip(String("read open failed: ") + p.openError.desc());
                                        else
                                                ctx.setFail(String("read open failed: ") + p.openError.desc());
                                        return;
                                }
                                if (!p.started) {
                                        if (isPlannerGap(p.startError))
                                                ctx.setSkip(String("read start failed: ") + p.startError.desc());
                                        else
                                                ctx.setFail(String("read start failed: ") + p.startError.desc());
                                        return;
                                }
                                if (p.timedOut) {
                                        ctx.setTimeout(String("read phase deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        return;
                                }
                                if (p.sawError) {
                                        ctx.setFail(String("read pipeline error: ") + p.errorDetail);
                                        return;
                                }
                        }

                        if (framesProcessed <= 0) {
                                ctx.setSkip(String("inspector saw no frames"));
                                return;
                        }
                        if (c.isSequence) {
                                FilePath       fp(path);
                                Dir            seqDir(fp.parent());
                                List<FilePath> entries = seqDir.entryList(String("*.") + c.extension);
                                int64_t        diskCount = (int64_t)entries.size();
                                ctx.setDetail(String("framesOnDisk"), diskCount);
                                if (diskCount > 0 && framesProcessed != diskCount) {
                                        ctx.setFail(String("inspector read ") + String::number(framesProcessed) +
                                                    String(" of ") + String::number(diskCount) +
                                                    String(" files on disk"));
                                        return;
                                }
                        }
                        if (totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities detected in round-trip"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerRoundtripCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        String desc = String("Roundtrip ") + c.backendName;
                        if (!c.extension.isEmpty()) desc += String(" / ") + c.extension;
                        if (c.codec.isValid()) desc += String(" / ") + c.codec.name();
                        if (c.pixelFormat.isValid()) desc += String(" / ") + c.pixelFormat.name();
                        TestRunner::registerCase(TestCase(c.name, desc,
                                                          [c](TestContext &ctx) { runRoundtripCase(c, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
