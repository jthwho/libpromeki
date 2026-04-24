/**
 * @file      roundtrip/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Functional test for end-to-end round-trips through MediaPipeline,
 * QuickTime, ImageFile, and VideoEncoder / VideoDecoder stages.
 *
 * For every supported container format + codec combination the test
 * builds a write pipeline that drives @ref MediaIOTask_TPG into the
 * file stage (with the @ref ImageDataEncoder and LTC stamp both
 * enabled), runs it for a fixed number of frames, then opens a read
 * pipeline that routes the written file back into
 * @ref MediaIOTask_Inspector.  The inspector's snapshot tells us how
 * many frames survived the round-trip and how many had recoverable
 * picture-data / LTC stamps.
 *
 * The list of combinations is built programmatically from the library
 * registries:
 *   - @ref MediaIO::registeredFormats gives every file-based backend
 *     plus its extensions.
 *   - @ref VideoCodec::registeredIDs filtered by
 *     @ref VideoCodec::canEncode and @ref VideoCodec::canDecode gives
 *     the set of codecs that can round-trip compressed data through
 *     a container (today: QuickTime).
 *
 * One or more @c --regex flags narrow that list at runtime.  The
 * filter is applied against the case's label (e.g.
 * @c "ImageFile:dpx" or @c "QuickTime:mov:H264").  A case is included
 * when any supplied regex matches it via @c std::regex_search.
 *
 * Usage:
 *   roundtrip-functest [--path DIR] [--regex PATTERN]... [--frames N] [--verbose]
 */

#include <cstdio>
#include <cstdlib>

#include <promeki/application.h>
#include <promeki/dir.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
#include <promeki/framecount.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_inspector.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/regex.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/videocodec.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

struct Options {
        String          path     = String("/mnt/data/tmp/promeki/roundtrip");
        StringList      regexes;
        int             frames   = 30;
        // Keep the raster small by default so the full matrix runs
        // fast on a developer's machine.  720p@59.94 stays in every
        // codec's happy path and doesn't spike disk I/O.
        VideoFormat     videoFormat{VideoFormat::Smpte720p59_94};
        String          logFile;    // --log FILE redirects logger output here
        unsigned int    phaseTimeoutMs = 10000; // per write / read phase
        bool            verbose  = false;
        bool            logConsole = false;  // --log-console keeps stderr logging
};

void usage() {
        std::fprintf(stderr,
                "Usage: roundtrip-functest [OPTIONS]\n"
                "\n"
                "  --path DIR          Destination directory for generated\n"
                "                      files (default /mnt/data/tmp/promeki/roundtrip).\n"
                "                      Created if missing.\n"
                "  --regex PATTERN     Restrict the case list to labels matching\n"
                "                      PATTERN (ECMAScript syntax, std::regex_search).\n"
                "                      Repeatable — a case is included if ANY supplied\n"
                "                      pattern matches.  When omitted, every case runs.\n"
                "  --frames N          Frames per round-trip (default 30).\n"
                "  --timeout-ms MS     Per-phase watchdog timeout (default 10000).\n"
                "                      A phase that exceeds this is cut off, marked\n"
                "                      TIMEOUT, and the matrix continues with the\n"
                "                      next case — one deadlocked pipeline can't\n"
                "                      stall the rest of the run.\n"
                "  --log FILE          Route the promeki Logger to FILE instead of the\n"
                "                      terminal.  Each case is bracketed by a pair of\n"
                "                      log lines that mark start / end, so a hang\n"
                "                      shows up as an unterminated entry.\n"
                "  --log-console       Keep the console (stderr) logger enabled.  By\n"
                "                      default the test silences stderr logging so the\n"
                "                      per-case [PASS|FAIL|SKIP] summary stays readable.\n"
                "  --list              Print the case list and exit.\n"
                "  --verbose           Raise logger to Debug (applies to whichever sink\n"
                "                      is enabled — file, console, or both).\n"
                "  -h, --help          Show this help.\n");
}

bool parseOptions(int argc, char **argv, Options &o, bool &listOnly) {
        listOnly = false;
        for(int i = 1; i < argc; ++i) {
                String a(argv[i]);
                if(a == "-h" || a == "--help") { usage(); std::exit(0); }
                auto needValue = [&](const String &flag) {
                        if(i + 1 >= argc) {
                                std::fprintf(stderr,
                                        "Error: %s expects an argument\n",
                                        flag.cstr());
                                return false;
                        }
                        return true;
                };
                if(a == "--path") {
                        if(!needValue(a)) return false;
                        o.path = argv[++i];
                        continue;
                }
                if(a == "--regex") {
                        if(!needValue(a)) return false;
                        o.regexes.pushToBack(String(argv[++i]));
                        continue;
                }
                if(a == "--frames") {
                        if(!needValue(a)) return false;
                        o.frames = std::atoi(argv[++i]);
                        if(o.frames <= 0) {
                                std::fprintf(stderr,
                                        "Error: --frames must be > 0\n");
                                return false;
                        }
                        continue;
                }
                if(a == "--log") {
                        if(!needValue(a)) return false;
                        o.logFile = argv[++i];
                        continue;
                }
                if(a == "--timeout-ms") {
                        if(!needValue(a)) return false;
                        long v = std::atol(argv[++i]);
                        if(v < 1) {
                                std::fprintf(stderr,
                                        "Error: --timeout-ms must be > 0\n");
                                return false;
                        }
                        o.phaseTimeoutMs = (unsigned int)v;
                        continue;
                }
                if(a == "--log-console") { o.logConsole = true; continue; }
                if(a == "--list") { listOnly = true; continue; }
                if(a == "--verbose") { o.verbose = true; continue; }
                std::fprintf(stderr, "Error: unknown option '%s'\n", a.cstr());
                return false;
        }
        return true;
}

// ---------------------------------------------------------------------------
// Case generation — pure registry introspection, no hard-coded lists.
// ---------------------------------------------------------------------------

struct Case {
        String      label;          // e.g. "ImageFile:dpx:RGB10_DPX_sRGB"
        String      backendName;    // "ImageFile", "QuickTime", ...
        String      extension;      // "dpx", "mov", ...
        VideoCodec  codec;          // invalid for raw / container-default cases
        PixelFormat   pixelFormat;      // forced video pixel format (invalid = backend default)
        bool        isSequence = false;  // true for file-per-frame formats
};

// Container backends that accept multiple codecs.  We detect this by
// checking whether the backend can round-trip compressed video
// through the generic VideoEncoder / VideoDecoder pair — today that
// only applies to QuickTime.  When a second container lands (MXF,
// IVF, etc.) it just needs to appear in @ref MediaIO::registeredFormats
// with canBeSource+canBeSink+extensions and return true here.
static bool backendAcceptsCodecMatrix(const String &name) {
        return name == String("QuickTime");
}

// The test's primary mandate is video round-trips through the
// planner / QuickTime / VideoEncoder / VideoDecoder / ImageFile
// quartet.  Audio-only backends (@c AudioFile) and network-transport
// backends (@c Rtp, future SRT / NDI) live in separate test domains
// and would never succeed with a TPG → file → Inspector flow, so
// skip them here instead of reporting noisy false-failures.
//
// The image-file subsystem registers one @ref MediaIO::FormatDesc
// per underlying format (@c ImgSeqDPX, @c ImgSeqPNG, ...) plus a
// legacy @c "ImageFile" umbrella that advertises every extension at
// once.  This test iterates the per-format entries only — the
// umbrella would produce duplicate cases for every extension — so
// any name starting with @c "ImgSeq" is accepted alongside the
// container-backend name @c "QuickTime".
static bool isVideoCapableFileBackend(const String &name) {
        if(name == String("QuickTime")) return true;
        if(name.size() >= 6 &&
           name.left(6) == String("ImgSeq")) return true;
        return false;
}

// True for an @c ImgSeq-style backend that produces one file per
// frame.  The round-trip matrix wants to know this so it can build
// the right filename pattern (with a @c #### mask) for the stage.
static bool isImageSequenceBackend(const String &name) {
        return name.size() >= 6 && name.left(6) == String("ImgSeq");
}

// Image-file extensions that require caller-supplied size / pixel-desc
// hints (headerless raw YUV formats).  Skipped from the default case
// list because the test would need to push bespoke per-ext config into
// the sink and the planner cannot guess the layout from TPG's RGB8
// output.  These are exercised separately by targeted unit tests.
static bool extensionNeedsHeaderlessHints(const String &ext) {
        return ext == String("uyvy") || ext == String("yuyv") ||
               ext == String("yuy2") || ext == String("v210") ||
               ext == String("i420") || ext == String("nv12") ||
               ext == String("yuv420p") || ext == String("i422") ||
               ext == String("yuv422p") || ext == String("yuv");
}

// Probe @p io 's sink at @ref MediaIO::proposeInput with every
// uncompressed @ref PixelFormat currently registered; keep the ones it
// accepts unchanged.  The sink does not need to be open — probe lives
// entirely on the task's proposeInput virtual, which the ImageFile
// and QuickTime tasks both implement synchronously against the
// offered desc.
static List<PixelFormat> nativePixelFormats(MediaIO *io) {
        List<PixelFormat> out;
        if(io == nullptr) return out;

        // The probe raster just needs to be a plausible size so that
        // ImageDesc doesn't reject a degenerate value and (for chroma-
        // subsampled formats) the planar layout is well-defined.
        const Size2Du32 probeSize(1920, 1080);

        PixelFormat::IDList ids = PixelFormat::registeredIDs();
        for(size_t i = 0; i < ids.size(); ++i) {
                PixelFormat pd(ids[i]);
                if(!pd.isValid()) continue;
                if(pd.isCompressed()) continue;

                MediaDesc offered;
                ImageDesc img(probeSize.width(), probeSize.height(),
                              pd.id());
                if(!img.isValid()) continue;
                offered.imageList().pushToBack(img);

                MediaDesc preferred;
                Error e = io->proposeInput(offered, &preferred);
                if(e.isError()) continue;
                if(preferred.imageList().isEmpty()) continue;
                if(preferred.imageList()[0].pixelFormat() != pd) continue;

                out.pushToBack(pd);
        }
        return out;
}

// Native-format query for a specific (backend, extension) pair.
// Creates a throwaway sink via the same factory path MediaPipeline
// uses at build time (@ref MediaIO::createForFileWrite), queries it,
// then tears it down.  We route through the filesystem factory
// rather than @ref MediaIO::create because the ImageFile task's
// proposeInput reads the config's @ref MediaConfig::Filename to
// resolve the extension — @ref createForFileWrite stamps that key
// for us.
static List<PixelFormat> nativePixelFormatsFor(const String &backendName,
                                            const String &extension) {
        // Path only needs to exist in the form the task expects; the
        // file is never created because the sink is never opened.
        // We land it inside the configured temp dir (the user's
        // memory note pins this to /mnt/data/tmp/promeki) just in
        // case a backend does probe the filesystem.
        const String probePath = String("/mnt/data/tmp/promeki/roundtrip_probe.")
                + extension;
        MediaIO *io = MediaIO::createForFileWrite(probePath);
        if(io == nullptr) {
                // Some backends match on path only via canHandlePath —
                // create() via registered Type name is a fallback.
                MediaIO::Config cfg = MediaIO::defaultConfig(backendName);
                cfg.set(MediaConfig::Type, backendName);
                cfg.set(MediaConfig::Filename, probePath);
                io = MediaIO::create(cfg);
                if(io == nullptr) return {};
        }
        List<PixelFormat> out = nativePixelFormats(io);
        delete io;
        return out;
}

static List<Case> buildCases() {
        List<Case> cases;
        const MediaIO::FormatDescList &formats = MediaIO::registeredFormats();
        for(size_t i = 0; i < formats.size(); ++i) {
                const MediaIO::FormatDesc &fd = formats[i];
                // File-based I/O only — device backends (V4L2, RTP) and
                // pure transforms have no usable extension list.
                if(fd.extensions.isEmpty()) continue;
                if(!fd.canBeSource || !fd.canBeSink) continue;
                if(!isVideoCapableFileBackend(fd.name)) continue;

                // Collapse duplicate extensions — some backends list
                // upper/lower aliases ("jpg", "JPEG", "jfif") that
                // all map to the same underlying format.
                //
                // For container formats like QuickTime the aliases
                // (mov / qt / mp4 / m4v) map to a single underlying
                // writer too — the per-pixel-desc matrix is the same
                // for each.  We run the matrix against the primary
                // extension only and emit one smoke case per alias so
                // the test still covers the extension dispatch.
                //
                // Per-format image-sequence backends (@c ImgSeqDPX,
                // @c ImgSeqSGI, ...) each cover one underlying file
                // format so their aliases (sgi/rgb, pnm/ppm/pgm, ...)
                // land on identical file content — the matrix runs
                // on the primary extension only and the per-case
                // label omits the extension entirely, since the
                // backend name alone already identifies the format.
                // Alias extensions still get a smoke case so the
                // extension-dispatch path stays covered.
                const bool isContainer = backendAcceptsCodecMatrix(fd.name);
                const bool isImgSeq    = isImageSequenceBackend(fd.name);
                const String primaryExt = fd.extensions[0].toLower();

                StringList seenExts;
                for(size_t j = 0; j < fd.extensions.size(); ++j) {
                        String ext = fd.extensions[j].toLower();
                        if(ext == String("imgseq")) continue;
                        if(isImgSeq
                           && extensionNeedsHeaderlessHints(ext)) continue;
                        bool dup = false;
                        for(size_t k = 0; k < seenExts.size(); ++k) {
                                if(seenExts[k] == ext) { dup = true; break; }
                        }
                        if(dup) continue;
                        seenExts.pushToBack(ext);

                        const bool isPrimary = (ext == primaryExt);

                        // Smoke case: one per extension with backend
                        // default pixel format.  ImgSeq cases drop
                        // the primary extension from the label since
                        // the backend name (@c ImgSeqDPX) already
                        // identifies the format; aliased ImgSeq
                        // extensions still append the ext so e.g.
                        // @c "ImgSeqSGI:rgb" is distinguishable from
                        // @c "ImgSeqSGI".  Container backends keep
                        // the extension in the label to surface
                        // which alias (.mov / .qt / .mp4) was tested.
                        {
                                Case c;
                                c.backendName = fd.name;
                                c.extension   = ext;
                                c.codec       = VideoCodec();
                                c.pixelFormat   = PixelFormat();
                                c.isSequence  = isImgSeq;
                                if(isImgSeq && isPrimary) {
                                        c.label = fd.name;
                                } else {
                                        c.label = fd.name + String(":") + ext;
                                }
                                cases.pushToBack(c);
                        }

                        // Pixel-format matrix.  Every backend runs
                        // the matrix against the primary extension
                        // only — aliases would just duplicate rows.
                        if(!isPrimary) continue;

                        List<PixelFormat> natives =
                                nativePixelFormatsFor(fd.name, ext);
                        for(size_t k = 0; k < natives.size(); ++k) {
                                const PixelFormat &pd = natives[k];
                                Case c;
                                c.backendName = fd.name;
                                c.extension   = ext;
                                c.codec       = VideoCodec();
                                c.pixelFormat   = pd;
                                c.isSequence  = isImgSeq;
                                if(isImgSeq) {
                                        c.label = fd.name + String(":") + pd.name();
                                } else {
                                        c.label = fd.name + String(":") + ext
                                                + String(":") + pd.name();
                                }
                                cases.pushToBack(c);
                        }
                }

                // Codec matrix for container backends.  Each codec
                // gets one smoke case (default compressed variant)
                // plus one case per entry in @ref compressedPixelFormats
                // — JPEG, for example, registers 10 variants covering
                // YUV / RGB + Rec.601 / Rec.709 + limited / full range.
                // The smoke case makes codec-level plumbing (NVENC
                // session, VideoDecoder attach) visible in the report
                // even when the downstream variant enumeration is
                // empty or one-entry.
                if(isContainer) {
                        VideoCodec::IDList ids = VideoCodec::registeredIDs();
                        for(size_t j = 0; j < ids.size(); ++j) {
                                VideoCodec vc(ids[j]);
                                if(!vc.isValid()) continue;
                                if(!vc.canEncode() || !vc.canDecode()) continue;

                                // Smoke (codec default) case.
                                {
                                        Case c;
                                        c.backendName = fd.name;
                                        c.extension   = primaryExt;
                                        c.codec       = vc;
                                        c.pixelFormat   = PixelFormat();
                                        c.isSequence  = false;
                                        c.label = fd.name + String(":") + primaryExt
                                                + String(":") + vc.name();
                                        cases.pushToBack(c);
                                }

                                // One case per compressed PixelFormat the
                                // codec's registry entry covers.  The
                                // pipeline's planner uses
                                // @c VideoPixelFormat set on the
                                // encoder stage to steer the compressed
                                // output variant — see makeEncoderStage.
                                //
                                // Skip codecs whose registry entry only
                                // covers a single compressed variant —
                                // the smoke case above already exercises
                                // that one path, and generating a second
                                // label like "mov:H264:H264" just adds
                                // noise to the report.
                                const List<PixelFormat> variants =
                                        vc.compressedPixelFormats();
                                if(variants.size() <= 1) continue;
                                for(size_t k = 0; k < variants.size(); ++k) {
                                        const PixelFormat &pd = variants[k];
                                        if(!pd.isValid()) continue;
                                        Case c;
                                        c.backendName = fd.name;
                                        c.extension   = primaryExt;
                                        c.codec       = vc;
                                        c.pixelFormat   = pd;
                                        c.isSequence  = false;
                                        c.label = fd.name + String(":") + primaryExt
                                                + String(":") + vc.name()
                                                + String(":") + pd.name();
                                        cases.pushToBack(c);
                                }
                        }
                }
        }
        return cases;
}

static List<Case> filterCases(const List<Case> &cases, const StringList &regexes) {
        if(regexes.isEmpty()) return cases;
        // Compile once; caller-supplied patterns are trusted.
        List<RegEx> compiled;
        for(size_t i = 0; i < regexes.size(); ++i) {
                compiled.pushToBack(RegEx(regexes[i]));
        }
        List<Case> out;
        for(size_t i = 0; i < cases.size(); ++i) {
                bool keep = false;
                for(size_t j = 0; j < compiled.size(); ++j) {
                        if(compiled[j].search(cases[i].label)) { keep = true; break; }
                }
                if(keep) out.pushToBack(cases[i]);
        }
        return out;
}

// ---------------------------------------------------------------------------
// Path building
// ---------------------------------------------------------------------------

// Label → filesystem-safe identifier.  The label already avoids path
// separators; we just replace ':' with '_' so the directory name
// stays portable.
static String safeTag(const String &label) {
        return label.replace(String(":"), String("_"));
}

static String pathForCase(const Case &c, const String &base) {
        const String tag = safeTag(c.label);
        if(c.isSequence) {
                // One subdir per case so parallel sequences don't
                // collide in a shared scan directory.  The '####'
                // mask is recognized by MediaIOTask_ImageFile.
                return base + String("/") + tag + String("/frame_####.")
                        + c.extension;
        }
        return base + String("/") + tag + String(".") + c.extension;
}

static bool ensureParentDir(const String &path) {
        FilePath fp(path);
        FilePath parent = fp.parent();
        Dir d(parent);
        if(d.exists()) return true;
        Error e = d.mkpath();
        if(e.isError()) {
                std::fprintf(stderr, "mkpath '%s': %s\n",
                        parent.toString().cstr(), e.desc().cstr());
                return false;
        }
        return true;
}

// ---------------------------------------------------------------------------
// Pipeline config builders
// ---------------------------------------------------------------------------

// TPG source stage.  Binary data encoder + LTC on by default so the
// inspector has something to validate on the read side.  When
// @p tpgPixelFormat is valid, TPG is asked to produce frames in that
// pixel format — for uncompressed-matrix cases this drives the sink
// end-to-end without a CSC hop.  For compressed-matrix cases the
// TPG stays on its default RGB8 and the encoder stage carries the
// compressed variant selection via VideoPixelFormat instead.
static MediaPipelineConfig::Stage makeTpgStage(const Options &opts,
                                                uint32_t streamId,
                                                const PixelFormat &tpgPixelFormat) {
        MediaPipelineConfig::Stage s;
        s.name = String("tpg");
        s.type = String("TPG");
        s.mode = MediaIO::Source;
        s.config = MediaIO::defaultConfig("TPG");
        s.config.set(MediaConfig::Type,                 String("TPG"));
        s.config.set(MediaConfig::VideoFormat,          opts.videoFormat);
        s.config.set(MediaConfig::VideoEnabled,         true);
        s.config.set(MediaConfig::AudioEnabled,         true);
        s.config.set(MediaConfig::TimecodeEnabled,      true);
        s.config.set(MediaConfig::TpgDataEncoderEnabled, true);
        s.config.set(MediaConfig::StreamID,             streamId);
        if(tpgPixelFormat.isValid() && !tpgPixelFormat.isCompressed()) {
                s.config.set(MediaConfig::VideoPixelFormat, tpgPixelFormat);
        }
        return s;
}

// VideoEncoder transform stage for compressed-container cases.  Uses
// a high bitrate so the lossy encoders (H.264, HEVC) preserve the
// TPG's picture-data band well enough for the inspector to recover
// the frame number / stream id stamps.  When @p variant is valid it
// selects the compressed output variant the codec should produce —
// e.g. the difference between @c PixelFormat::JPEG_YUV8_420_Rec709 and
// @c PixelFormat::JPEG_YUV8_422_Rec601 is exactly this selection.
static MediaPipelineConfig::Stage makeEncoderStage(const VideoCodec &codec,
                                                    const PixelFormat &variant) {
        MediaPipelineConfig::Stage s;
        s.name = String("enc");
        s.type = String("VideoEncoder");
        s.mode = MediaIO::Transform;
        s.config = MediaIO::defaultConfig("VideoEncoder");
        s.config.set(MediaConfig::Type,         String("VideoEncoder"));
        s.config.set(MediaConfig::VideoCodec,   codec);
        // 50 Mbit/s is high enough that H.264/HEVC at 720p keep the
        // top-of-frame picture-data band readable — the default
        // 5 Mbit/s would sometimes smear the black/white cells into
        // unrecoverable grey at this resolution.
        s.config.set(MediaConfig::BitrateKbps,  int32_t(50000));
        // All-intra so every frame is self-contained and the round-
        // trip pairing between write and read stays 1:1.
        s.config.set(MediaConfig::GopLength,    int32_t(1));
        // VideoPixelFormat on the encoder stage is read by the
        // backend (and consulted by the planner) as "produce this
        // compressed variant".  Only forward valid compressed
        // PixelFormats here — other shapes would make no sense on an
        // encoder output.
        if(variant.isValid() && variant.isCompressed()) {
                s.config.set(MediaConfig::VideoPixelFormat, variant);
        }
        return s;
}

static MediaPipelineConfig::Stage makeDecoderStage(const VideoCodec &codec) {
        MediaPipelineConfig::Stage s;
        s.name = String("dec");
        s.type = String("VideoDecoder");
        s.mode = MediaIO::Transform;
        s.config = MediaIO::defaultConfig("VideoDecoder");
        s.config.set(MediaConfig::Type,         String("VideoDecoder"));
        s.config.set(MediaConfig::VideoCodec,   codec);
        return s;
}

static MediaPipelineConfig::Stage makeFileSinkStage(const String &path) {
        MediaPipelineConfig::Stage s;
        s.name = String("out");
        s.path = path;   // empty type → pipeline uses createForFileWrite
        s.mode = MediaIO::Sink;
        return s;
}

static MediaPipelineConfig::Stage makeFileSourceStage(const String &path) {
        MediaPipelineConfig::Stage s;
        s.name = String("in");
        s.path = path;   // empty type → pipeline uses createForFileRead
        s.mode = MediaIO::Source;
        return s;
}

static MediaPipelineConfig::Stage makeInspectorStage() {
        MediaPipelineConfig::Stage s;
        s.name = String("insp");
        s.type = String("Inspector");
        s.mode = MediaIO::Sink;
        s.config = MediaIO::defaultConfig("Inspector");
        s.config.set(MediaConfig::Type, String("Inspector"));
        return s;
}

static MediaPipelineConfig buildWriteConfig(const Case &c,
                                             const String &path,
                                             const Options &opts,
                                             uint32_t streamId) {
        MediaPipelineConfig cfg;
        // For raw (no codec) cases the case's PixelFormat is the TPG
        // output format; for compressed cases it's the encoder's
        // output variant and TPG stays on its default.
        const PixelFormat tpgPd = c.codec.isValid() ? PixelFormat() : c.pixelFormat;
        cfg.addStage(makeTpgStage(opts, streamId, tpgPd));
        String prev = String("tpg");
        if(c.codec.isValid()) {
                cfg.addStage(makeEncoderStage(c.codec, c.pixelFormat));
                cfg.addRoute(prev, String("enc"));
                prev = String("enc");
        }
        cfg.addStage(makeFileSinkStage(path));
        cfg.addRoute(prev, String("out"));
        // Bound the infinite TPG source by the test's frame budget —
        // MediaPipeline closes each sink once it has received exactly
        // N frames, rather than relying on a racy frame-counter signal
        // that can strand frames in transform buffers or let the sink
        // write one extra file before the async close takes effect.
        cfg.setFrameCount(FrameCount(opts.frames));
        return cfg;
}

static MediaPipelineConfig buildReadConfig(const Case &c, const String &path) {
        MediaPipelineConfig cfg;
        cfg.addStage(makeFileSourceStage(path));
        String prev = String("in");
        if(c.codec.isValid()) {
                cfg.addStage(makeDecoderStage(c.codec));
                cfg.addRoute(prev, String("dec"));
                prev = String("dec");
        }
        cfg.addStage(makeInspectorStage());
        cfg.addRoute(prev, String("insp"));
        return cfg;
}

// ---------------------------------------------------------------------------
// Per-case runner
// ---------------------------------------------------------------------------

struct RunResult {
        enum Status { Pass, Fail, Skip, Timeout };
        Status   status = Skip;
        String   message;               // reason on Skip / Fail / Timeout
        int64_t  framesWritten         = 0;
        int64_t  framesProcessed       = 0;
        int64_t  framesWithPictureData = 0;
        int64_t  framesWithLtc         = 0;
        int64_t  totalDiscontinuities  = 0;
};

// Drives one MediaPipeline through open / start / exec / close.  The
// write path relies on @ref MediaPipelineConfig::setFrameCount so the
// pipeline closes each sink after exactly N frames; the read path
// waits for the natural EOF cascade.  Either way the blocking @c
// loop->exec() returns when @c closedSignal fires.
struct PhaseOutcome {
        bool    built   = false;
        bool    opened  = false;
        bool    started = false;
        bool    sawError = false;
        bool    timedOut = false;
        Error   buildError;
        Error   openError;
        Error   startError;
        Error   closeError;
        String  errorDetail;    // first pipeline-error stage+error
};

static PhaseOutcome runPhase(MediaPipeline &pipe,
                              const MediaPipelineConfig &cfg,
                              EventLoop *loop,
                              unsigned int timeoutMs) {
        PhaseOutcome p;
        p.buildError = pipe.build(cfg, /*autoplan=*/true);
        if(p.buildError.isError()) return p;
        p.built = true;

        pipe.pipelineErrorSignal.connect(
                [&p](const String &stageName, Error err) {
                        if(!p.sawError) {
                                p.errorDetail = stageName + String(": ")
                                        + err.desc();
                        }
                        p.sawError = true;
                }, &pipe);
        pipe.closedSignal.connect(
                [&p, loop](Error err) {
                        p.closeError = err;
                        loop->quit(0);
                }, &pipe);

        p.openError = pipe.open();
        if(p.openError.isError()) return p;
        p.opened = true;

        p.startError = pipe.start();
        if(p.startError.isError()) {
                (void)pipe.close();
                return p;
        }
        p.started = true;

        // Watchdog: a one-shot timer that fires the first time the
        // phase exceeds @p timeoutMs.  Most phases run in tens of
        // milliseconds at the default 720p / 10-frame settings, so a
        // 10 s default has plenty of head-room while still catching
        // a real hang (bad decoder, deadlocked close cascade) in a
        // bounded amount of wall time.  When it fires we set the
        // @c timedOut flag and quit the loop — the caller then
        // interprets the missing @c closedSignal payload as timeout
        // and moves on to the next case.  The pipeline's stack-local
        // destructor still runs; any frames still in flight are
        // cleaned up in-scope, not leaked into the next case.
        int watchdogId = -1;
        if(timeoutMs > 0) {
                const unsigned int ms = timeoutMs;
                watchdogId = loop->startTimer(
                        ms,
                        [&p, loop, ms]() {
                                if(p.timedOut) return;
                                p.timedOut = true;
                                promekiWarn("roundtrip-functest: phase "
                                            "watchdog fired after %u ms — "
                                            "pipeline appears deadlocked",
                                            ms);
                                loop->quit(0);
                        },
                        /*singleShot=*/true);
        }

        loop->exec();
        if(watchdogId >= 0) loop->stopTimer(watchdogId);

        // Best-effort drain of the hung pipeline: cancel anything
        // still on the strands so the stack-local destructor doesn't
        // park the test thread.  If the pipeline is genuinely wedged
        // the strand workers keep running, but the controller state
        // is released and we can continue.
        if(p.timedOut) {
                StringList names = pipe.stageNames();
                for(size_t i = 0; i < names.size(); ++i) {
                        MediaIO *io = pipe.stage(names[i]);
                        if(io != nullptr) io->cancelPending();
                }
        }

        return p;
}

static RunResult runCase(const Case &c, const Options &opts,
                          uint32_t streamId) {
        RunResult r;
        EventLoop *loop = Application::mainEventLoop();
        if(loop == nullptr) {
                r.status = RunResult::Fail;
                r.message = String("no main EventLoop");
                return r;
        }

        const String path = pathForCase(c, opts.path);
        if(!ensureParentDir(path)) {
                r.status = RunResult::Fail;
                r.message = String("could not create parent directory");
                return r;
        }

        // ---- Write phase ----
        {
                MediaPipelineConfig cfg = buildWriteConfig(c, path, opts, streamId);
                MediaPipeline pipe;

                PhaseOutcome p = runPhase(pipe, cfg, loop, opts.phaseTimeoutMs);

                // The pipeline's @ref MediaPipelineConfig::setFrameCount
                // cap makes the write side terminate deterministically
                // at exactly @c opts.frames.  Report the configured
                // budget so the per-case line shows the same number we
                // asked the pipeline to deliver.
                r.framesWritten = opts.frames;

                // Build / open / pipeline-error failures classify as
                // Skip, not Fail, because they almost always mean
                // "this container + codec combination isn't supported
                // in this build" (missing NVENC, writer rejecting a
                // codec, etc.) rather than a genuine round-trip
                // regression.  The test's job is to flag data-loss /
                // data-corruption in combinations that the registry
                // claims work — it is not a completeness audit of
                // what each backend supports.
                if(!p.built) {
                        r.status = RunResult::Skip;
                        r.message = String("write build failed: ") + p.buildError.desc();
                        return r;
                }
                if(!p.opened) {
                        r.status = RunResult::Skip;
                        r.message = String("write open failed: ") + p.openError.desc();
                        return r;
                }
                if(!p.started) {
                        r.status = RunResult::Skip;
                        r.message = String("write start failed: ") + p.startError.desc();
                        return r;
                }
                if(p.timedOut) {
                        r.status = RunResult::Timeout;
                        r.message = String("write phase deadlocked past ")
                                + String::number(opts.phaseTimeoutMs)
                                + String(" ms");
                        return r;
                }
                if(p.sawError) {
                        r.status = RunResult::Skip;
                        r.message = String("write pipeline error: ")
                                + p.errorDetail;
                        return r;
                }
                if(p.closeError.isError()) {
                        r.status = RunResult::Skip;
                        r.message = String("write close: ") + p.closeError.desc();
                        return r;
                }
                if(r.framesWritten == 0) {
                        r.status = RunResult::Skip;
                        r.message = String("write produced no frames");
                        return r;
                }
        }

        // ---- Read phase ----
        // The Inspector needs to be constructed directly so the test
        // can snapshot its accumulator after the pipeline closes —
        // the generic MediaIO::create path hides the typed task.  We
        // build the MediaIO wrapper, adopt the task, inject it into
        // the pipeline by name, and rely on injectStage to skip the
        // registry-based construction for that stage.
        MediaIOTask_Inspector *inspTask = new MediaIOTask_Inspector();
        MediaIO *inspIO = new MediaIO();
        {
                MediaIO::Config inspCfg = MediaIO::defaultConfig("Inspector");
                inspCfg.set(MediaConfig::Type, String("Inspector"));
                inspIO->setConfig(inspCfg);
                Error ae = inspIO->adoptTask(inspTask);
                if(ae.isError()) {
                        r.status = RunResult::Fail;
                        r.message = String("adoptTask: ") + ae.desc();
                        delete inspIO;   // also deletes inspTask
                        return r;
                }
        }

        {
                MediaPipelineConfig cfg = buildReadConfig(c, path);
                MediaPipeline pipe;
                Error ie = pipe.injectStage(String("insp"), inspIO);
                if(ie.isError()) {
                        r.status = RunResult::Fail;
                        r.message = String("injectStage: ") + ie.desc();
                        delete inspIO;
                        return r;
                }

                PhaseOutcome p = runPhase(pipe, cfg, loop,
                        opts.phaseTimeoutMs);
                // Read side runs until the source produces EOF and the
                // close cascade completes.

                // Snapshot the inspector BEFORE deleting the MediaIO
                // wrapper, which would destroy the adopted task.
                InspectorSnapshot snap = inspTask->snapshot();
                r.framesProcessed       = snap.framesProcessed.value();
                r.framesWithPictureData = snap.framesWithPictureData.value();
                r.framesWithLtc         = snap.framesWithLtc.value();
                r.totalDiscontinuities  = snap.totalDiscontinuities;

                // Injected stages are not deleted by MediaPipeline,
                // so the wrapper still owns the task here.
                delete inspIO;  // deletes inspTask too

                // Read-path failures come in two flavours:
                //   - Planner / registry gaps (@c Error::NotSupported on
                //     build / open / start): the file on disk might be
                //     fine, we just don't have a pipeline that can
                //     consume it here.  These are interesting signal
                //     for what the library advertises but hasn't wired
                //     up yet, but they're not data corruption and
                //     they tend to cluster around the same underlying
                //     gap; treat as Skip so the summary's Fail bucket
                //     stays reserved for actual round-trip breakage.
                //   - Mid-stream pipeline errors after a successful
                //     start: the reader chose a path and then failed
                //     executing it, e.g. a decoder that claimed to
                //     support a bitstream but actually can't.  Those
                //     stay as Fail.
                auto isPlannerGap = [](const Error &e) {
                        return e == Error::NotSupported;
                };
                if(!p.built) {
                        r.status = isPlannerGap(p.buildError)
                                ? RunResult::Skip : RunResult::Fail;
                        r.message = String("read build failed: ") + p.buildError.desc();
                        return r;
                }
                if(!p.opened) {
                        r.status = isPlannerGap(p.openError)
                                ? RunResult::Skip : RunResult::Fail;
                        r.message = String("read open failed: ") + p.openError.desc();
                        return r;
                }
                if(!p.started) {
                        r.status = isPlannerGap(p.startError)
                                ? RunResult::Skip : RunResult::Fail;
                        r.message = String("read start failed: ") + p.startError.desc();
                        return r;
                }
                if(p.timedOut) {
                        r.status = RunResult::Timeout;
                        r.message = String("read phase deadlocked past ")
                                + String::number(opts.phaseTimeoutMs)
                                + String(" ms");
                        return r;
                }
                if(p.sawError) {
                        r.status = RunResult::Fail;
                        r.message = String("read pipeline error: ")
                                + p.errorDetail;
                        return r;
                }
        }

        // Verification:
        //  - At least one frame must have made it end-to-end.  Exact
        //    counts are not enforced: MediaPipeline's close cascade
        //    can strand a handful of in-flight frames in a transform
        //    stage's buffer when the source closes mid-stream, so the
        //    writer may put fewer than @c opts.frames files on disk.
        //    The reader's job is to tell us what's actually there.
        //  - No continuity gaps — the test's strongest invariant.
        //    TPG stamps monotonic frame numbers and timecodes into
        //    every frame via @ref ImageDataEncoder + LTC, and the
        //    inspector flags any gap.  If the round-trip preserved
        //    the frames in-order and intact, the discontinuity count
        //    is zero even on lossy codecs.
        //  - For an ImageFile sequence, the disk file count sets a
        //    concrete ceiling — mismatch vs. the inspector count means
        //    the reader skipped files, which is a real failure.
        if(r.framesProcessed <= 0) {
                r.status = RunResult::Skip;
                r.message = String("inspector saw no frames");
                return r;
        }
        if(c.isSequence) {
                const String path = pathForCase(c, opts.path);
                FilePath fp(path);
                Dir seqDir(fp.parent());
                int64_t diskCount = 0;
                List<FilePath> entries = seqDir.entryList(
                        String("*.") + c.extension);
                diskCount = (int64_t)entries.size();
                if(diskCount > 0 && r.framesProcessed != diskCount) {
                        r.status = RunResult::Fail;
                        r.message = String("inspector read ")
                                + String::number(r.framesProcessed)
                                + String(" of ")
                                + String::number(diskCount)
                                + String(" files on disk");
                        return r;
                }
        }
        if(r.totalDiscontinuities != 0) {
                r.status = RunResult::Fail;
                r.message = String::number(r.totalDiscontinuities)
                        + String(" discontinuities detected in round-trip");
                return r;
        }

        r.status = RunResult::Pass;
        return r;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

static const char *statusWord(RunResult::Status s) {
        switch(s) {
                case RunResult::Pass:    return "PASS";
                case RunResult::Fail:    return "FAIL";
                case RunResult::Skip:    return "SKIP";
                case RunResult::Timeout: return "TIME";
        }
        return "?";
}

static void printCaseResult(const Case &c, const RunResult &r) {
        std::printf("[%4s] %-32s  written=%lld  processed=%lld  pic=%lld  ltc=%lld  disc=%lld",
                statusWord(r.status),
                c.label.cstr(),
                (long long)r.framesWritten,
                (long long)r.framesProcessed,
                (long long)r.framesWithPictureData,
                (long long)r.framesWithLtc,
                (long long)r.totalDiscontinuities);
        if(!r.message.isEmpty()) {
                std::printf("  (%s)", r.message.cstr());
        }
        std::printf("\n");
        std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv) {
        Application app(argc, argv);
        Application::setAppName(String("roundtrip-functest"));

        Options opts;
        bool listOnly = false;
        if(!parseOptions(argc, argv, opts, listOnly)) return 1;

        // Logger wiring.  Default behaviour:
        //   - console (stderr) logging disabled so the per-case
        //     summary table isn't drowned out by backend chatter
        //   - no file sink unless the user asks for one with --log
        //   - --log-console restores the stderr sink for interactive
        //     debugging
        //   - --verbose drops the level to Debug on whichever sinks
        //     are active, which is what we want whether the log is
        //     going to a file, to stderr, or both
        Logger &logger = Logger::defaultLogger();
        if(!opts.logConsole) {
                logger.setConsoleLoggingEnabled(false);
        }
        if(!opts.logFile.isEmpty()) {
                logger.setLogFile(opts.logFile);
        }
        if(opts.verbose) {
                logger.setLogLevel(Logger::LogLevel::Debug);
        }

        // Build + filter the case list up front so --list gives the
        // user exactly what a real run would exercise.
        List<Case> cases = buildCases();
        cases = filterCases(cases, opts.regexes);

        if(cases.isEmpty()) {
                std::fprintf(stderr,
                        "Error: no cases matched (registry has %zu candidates; %zu regex filter%s)\n",
                        (size_t)buildCases().size(),
                        (size_t)opts.regexes.size(),
                        opts.regexes.size() == 1 ? "" : "s");
                return 1;
        }

        if(listOnly) {
                std::printf("%zu case%s:\n", (size_t)cases.size(),
                        cases.size() == 1 ? "" : "s");
                for(size_t i = 0; i < cases.size(); ++i) {
                        std::printf("  %s\n", cases[i].label.cstr());
                }
                return 0;
        }

        std::printf("roundtrip-functest: %zu case%s, %d frame%s each, path=%s\n",
                (size_t)cases.size(),
                cases.size() == 1 ? "" : "s",
                opts.frames,
                opts.frames == 1 ? "" : "s",
                opts.path.cstr());

        // Make sure the root exists once — individual cases also
        // mkpath their own subdirs, but creating the top-level dir
        // eagerly gives a clearer error if the user passed a bad
        // --path.
        Dir rootDir(FilePath{opts.path});
        if(!rootDir.exists()) {
                Error e = rootDir.mkpath();
                if(e.isError()) {
                        std::fprintf(stderr, "mkpath '%s': %s\n",
                                opts.path.cstr(), e.desc().cstr());
                        return 1;
                }
        }

        int passed = 0;
        int failed = 0;
        int skipped = 0;
        int timedOut = 0;
        uint32_t streamId = 0xABCD0000u;
        for(size_t i = 0; i < cases.size(); ++i) {
                const Case &c = cases[i];
                // Bracket every case in the logger so a hang in the
                // middle of the matrix shows up as an unterminated
                // "BEGIN" entry — the per-case stdout line (printed
                // via @c printCaseResult below) never prints until
                // runCase returns, so the log is the only place that
                // captures "which case was running when we locked up".
                promekiInfo("=== BEGIN case %zu / %zu : %s ===",
                        i + 1, (size_t)cases.size(), c.label.cstr());
                RunResult r = runCase(c, opts, streamId++);
                promekiInfo("=== END   case %zu / %zu : %s -> %s%s%s ===",
                        i + 1, (size_t)cases.size(), c.label.cstr(),
                        statusWord(r.status),
                        r.message.isEmpty() ? "" : " : ",
                        r.message.cstr());
                printCaseResult(c, r);
                switch(r.status) {
                        case RunResult::Pass:    ++passed;   break;
                        case RunResult::Fail:    ++failed;   break;
                        case RunResult::Skip:    ++skipped;  break;
                        case RunResult::Timeout: ++timedOut; break;
                }
        }

        std::printf("\nSummary: %d passed, %d failed, %d skipped, %d timed out of %zu case%s\n",
                passed, failed, skipped, timedOut,
                (size_t)cases.size(),
                cases.size() == 1 ? "" : "s");
        // Timeouts count as failures for the exit code — a deadlocked
        // pipeline is a real bug even when we can't reduce it to a
        // clean Fail diagnosis.
        const bool overallPass = (failed == 0 && timedOut == 0);
        std::printf("RESULT: %s\n", overallPass ? "PASS" : "FAIL");
        return overallPass ? 0 : 1;
}
