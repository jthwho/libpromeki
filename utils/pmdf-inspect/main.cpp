/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * pmdf-inspect: a thin CLI over DebugMediaFile / VariantQuery /
 * Frame::dump / MediaIO::copyFrames.  Subcommands delegate entirely
 * to library primitives; no format, parsing, or rendering logic
 * lives here.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <promeki/debugmediafile.h>
#include <promeki/variantquery.h>
#include <promeki/frame.h>
#include <promeki/mediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

using namespace promeki;

namespace {

struct Args {
        String  filename;
        // Mutually exclusive subcommand, in argument order.
        enum Command {
                CmdSummary,
                CmdFrame,
                CmdFrames,
                CmdFind,
                CmdDumpToc,
                CmdExtractImage,
                CmdExtractAudio,
                CmdHelp
        };
        Command command = CmdSummary;

        // Arguments for specific subcommands.
        int64_t frameIndex = 0;
        int64_t rangeStart = 0;
        int64_t rangeEnd   = -1;  // -1 = "until end"
        String  expression;
        int64_t fromFrame  = 0;
        bool    findAll    = false;
        String  outputPath;
        int     imageIndex = 0;
        int     audioIndex = 0;
        String  format;       // optional template for --find output
        bool    quiet        = false;
        bool    minimal      = false;
};

void usage(const char *argv0) {
        std::fprintf(stderr,
"Usage: %s <file.pmdf> [options]\n"
"\n"
"Commands (pick one):\n"
"  --summary                     Default.  Prints frame count,\n"
"                                session info, first/last frame indices.\n"
"  --frame N                     Dumps every detail of frame N.\n"
"  --frames A:B                  Dumps the range [A, B).  Either bound may be\n"
"                                omitted (e.g. ':10' or '5:').\n"
"  --find EXPR [--from N] [--all]\n"
"                                Scans from frame N (default 0).  Each\n"
"                                match is dumped in full (same output as\n"
"                                --frame); --all keeps scanning past the\n"
"                                first match, default stops after one.\n"
"                                Use --quiet / --minimal / --format to\n"
"                                switch to a condensed output style.\n"
"  --dump-toc                    Prints the frame index (from TOC or linear\n"
"                                scan).\n"
"  --extract-image A:B -o PATH [--image-index N]\n"
"                                Pulls image N (default 0) from frames\n"
"                                [A, B) and writes them via whichever MediaIO\n"
"                                sink matches PATH's extension (DPX, PNG,\n"
"                                raw, ...).  PATH should contain a '####' or\n"
"                                '%%04d' mask for multi-frame ranges.\n"
"  --extract-audio A:B -o PATH [--audio-index N]\n"
"                                Pulls audio N (default 0) from frames\n"
"                                [A, B) and writes to PATH (WAV, FLAC, ...).\n"
"\n"
"Shared options:\n"
"  --quiet                       With --find, print just the matching\n"
"                                frame numbers (one per line).\n"
"  --minimal                     With --find, print '<N> +<delta>' for\n"
"                                each match, where <delta> is the number\n"
"                                of frames since the previous match (or\n"
"                                since --from for the first match).\n"
"  --format TMPL                 With --find, render each match via\n"
"                                VariantLookup<Frame>::format(TMPL) instead of\n"
"                                the full dump.\n"
"  --help                        Show this help.\n",
        argv0);
}

bool parseRange(const char *s, int64_t &start, int64_t &end) {
        const char *colon = std::strchr(s, ':');
        if(colon == nullptr) {
                // Single number → [N, N+1)
                char *endp = nullptr;
                long long v = std::strtoll(s, &endp, 10);
                if(endp == s || *endp != '\0') return false;
                start = v;
                end   = v + 1;
                return true;
        }
        std::string left(s, colon);
        std::string right(colon + 1);
        if(left.empty()) start = 0;
        else {
                char *endp = nullptr;
                long long v = std::strtoll(left.c_str(), &endp, 10);
                if(endp == left.c_str() || *endp != '\0') return false;
                start = v;
        }
        if(right.empty()) end = -1;
        else {
                char *endp = nullptr;
                long long v = std::strtoll(right.c_str(), &endp, 10);
                if(endp == right.c_str() || *endp != '\0') return false;
                end = v;
        }
        return true;
}

bool parseArgs(int argc, char **argv, Args &out) {
        if(argc < 2) return false;
        int i = 1;
        // Filename first (unless only --help was given).
        if(std::strcmp(argv[i], "--help") == 0) {
                out.command = Args::CmdHelp;
                return true;
        }
        out.filename = argv[i++];
        out.command  = Args::CmdSummary;

        while(i < argc) {
                std::string a = argv[i];
                if(a == "--help") { out.command = Args::CmdHelp; return true; }
                else if(a == "--summary") { out.command = Args::CmdSummary; ++i; }
                else if(a == "--frame" && i + 1 < argc) {
                        out.command = Args::CmdFrame;
                        char *endp = nullptr;
                        out.frameIndex = std::strtoll(argv[i + 1], &endp, 10);
                        if(*endp != '\0') return false;
                        i += 2;
                }
                else if(a == "--frames" && i + 1 < argc) {
                        out.command = Args::CmdFrames;
                        if(!parseRange(argv[i + 1], out.rangeStart, out.rangeEnd)) return false;
                        i += 2;
                }
                else if(a == "--find" && i + 1 < argc) {
                        out.command    = Args::CmdFind;
                        out.expression = argv[i + 1];
                        i += 2;
                }
                else if(a == "--from" && i + 1 < argc) {
                        char *endp = nullptr;
                        out.fromFrame = std::strtoll(argv[i + 1], &endp, 10);
                        if(*endp != '\0') return false;
                        i += 2;
                }
                else if(a == "--all") { out.findAll = true; ++i; }
                else if(a == "--quiet") { out.quiet = true; ++i; }
                else if(a == "--minimal") { out.minimal = true; ++i; }
                else if(a == "--format" && i + 1 < argc) {
                        out.format = argv[i + 1];
                        i += 2;
                }
                else if(a == "--dump-toc") { out.command = Args::CmdDumpToc; ++i; }
                else if(a == "--extract-image" && i + 1 < argc) {
                        out.command = Args::CmdExtractImage;
                        if(!parseRange(argv[i + 1], out.rangeStart, out.rangeEnd)) return false;
                        i += 2;
                }
                else if(a == "--extract-audio" && i + 1 < argc) {
                        out.command = Args::CmdExtractAudio;
                        if(!parseRange(argv[i + 1], out.rangeStart, out.rangeEnd)) return false;
                        i += 2;
                }
                else if(a == "-o" && i + 1 < argc) {
                        out.outputPath = argv[i + 1];
                        i += 2;
                }
                else if(a == "--image-index" && i + 1 < argc) {
                        char *endp = nullptr;
                        out.imageIndex = static_cast<int>(std::strtol(argv[i + 1], &endp, 10));
                        if(*endp != '\0') return false;
                        i += 2;
                }
                else if(a == "--audio-index" && i + 1 < argc) {
                        char *endp = nullptr;
                        out.audioIndex = static_cast<int>(std::strtol(argv[i + 1], &endp, 10));
                        if(*endp != '\0') return false;
                        i += 2;
                }
                else {
                        std::fprintf(stderr, "Unknown or malformed option: %s\n", argv[i]);
                        return false;
                }
        }
        return true;
}

int cmdSummary(DebugMediaFile &f) {
        std::printf("File: %s\n", f.filename().cstr());
        std::printf("Frames: %lld\n", static_cast<long long>(f.frameCount().value()));
        std::printf("Footer: %s\n", f.hasFooter() ? "present" : "missing (linear scan)");
        std::printf("Session info:\n");
        StringList lines = f.sessionInfo().dump();
        if(lines.isEmpty()) {
                std::printf("  (empty)\n");
        } else {
                for(const String &ln : lines) {
                        std::printf("  %s\n", ln.cstr());
                }
        }
        return 0;
}

// Common frame-dump layout shared by --frame, --frames, and the
// default --find output.  Emits "Frame N:" followed by the frame's
// dump lines indented by two spaces, so single- and multi-frame
// output look identical and downstream tools can rely on a stable
// format regardless of which command produced it.
void printFrameDump(int64_t idx, const Frame &frame) {
        std::printf("Frame[%lld]:\n", static_cast<long long>(idx));
        StringList lines = frame.dump(String("  "));
        for(const String &ln : lines) std::printf("%s\n", ln.cstr());
}

int cmdFrame(DebugMediaFile &f, int64_t idx) {
        Frame::Ptr frame;
        Error e = f.readFrameAt(idx, frame);
        if(e.isError()) {
                std::fprintf(stderr, "Failed to read frame %lld: %s\n",
                             static_cast<long long>(idx), e.name().cstr());
                return 1;
        }
        printFrameDump(idx, *frame);
        return 0;
}

int cmdFrames(DebugMediaFile &f, int64_t start, int64_t end) {
        int64_t fc = f.frameCount().value();
        if(end < 0) end = fc;
        if(start < 0) start = 0;
        if(end > fc) end = fc;
        for(int64_t i = start; i < end; ++i) {
                int rc = cmdFrame(f, i);
                if(rc != 0) return rc;
        }
        return 0;
}

int cmdFind(DebugMediaFile &f, const Args &args) {
        auto [query, perr] = VariantQuery<Frame>::parse(args.expression);
        if(perr.isError()) {
                std::fprintf(stderr, "Parse error: %s\n",
                             query.errorDetail().cstr());
                return 1;
        }

        int64_t fc    = f.frameCount().value();
        int64_t start = args.fromFrame < 0 ? 0 : args.fromFrame;
        int64_t matches = 0;
        int64_t lastMatch = start - 1;  // for --minimal delta; first hit is i - start + 1 frames in
        for(int64_t i = start; i < fc; ++i) {
                Frame::Ptr frame;
                Error e = f.readFrameAt(i, frame);
                if(e.isError()) {
                        std::fprintf(stderr, "read frame %lld: %s\n",
                                     static_cast<long long>(i), e.name().cstr());
                        return 1;
                }
                if(!query.match(*frame)) continue;
                ++matches;
                if(!args.format.isEmpty()) {
                        String s = VariantLookup<Frame>::format(*frame, args.format);
                        std::printf("%s\n", s.cstr());
                } else if(args.quiet) {
                        std::printf("%lld\n", static_cast<long long>(i));
                } else if(args.minimal) {
                        int64_t delta = i - lastMatch;
                        std::printf("%lld +%lld\n",
                                    static_cast<long long>(i),
                                    static_cast<long long>(delta));
                } else {
                        // Default: full dump, same layout as --frame* output.
                        printFrameDump(i, *frame);
                }
                lastMatch = i;
                if(!args.findAll) break;
        }
        if(matches == 0 && !args.quiet && !args.minimal) {
                std::fprintf(stderr, "no matches\n");
        }
        return matches > 0 ? 0 : 2;
}

int cmdDumpToc(DebugMediaFile &f) {
        const auto &idx = f.index();
        if(idx.isEmpty()) {
                std::printf("(index is empty)\n");
                return 0;
        }
        std::printf("# frame  offset            presentationUs\n");
        for(const auto &e : idx) {
                std::printf("%-7lld  %-16lld  %lld\n",
                            static_cast<long long>(e.frameNumber.value()),
                            static_cast<long long>(e.fileOffset),
                            static_cast<long long>(e.presentationUs));
        }
        return 0;
}

Frame::Ptr trimForImage(const Frame::Ptr &in, int imageIdx) {
        if(!in.isValid()) return Frame::Ptr();
        auto vids = in->videoPayloads();
        if(imageIdx < 0 || static_cast<size_t>(imageIdx) >= vids.size())
                return Frame::Ptr();
        Frame::Ptr out = Frame::Ptr::create();
        Frame *raw = out.modify();
        raw->metadata() = in->metadata();
        raw->addPayload(vids[imageIdx]);
        return out;
}

Frame::Ptr trimForAudio(const Frame::Ptr &in, int audioIdx) {
        if(!in.isValid()) return Frame::Ptr();
        auto auds = in->audioPayloads();
        if(audioIdx < 0 || static_cast<size_t>(audioIdx) >= auds.size())
                return Frame::Ptr();
        Frame::Ptr out = Frame::Ptr::create();
        Frame *raw = out.modify();
        raw->metadata() = in->metadata();
        raw->addPayload(auds[audioIdx]);
        return out;
}

int cmdExtract(const Args &args, bool isImage) {
        if(args.outputPath.isEmpty()) {
                std::fprintf(stderr, "-o PATH is required for --extract-*\n");
                return 1;
        }
        MediaIO::Config srcCfg;
        srcCfg.set(MediaConfig::Type, "PMDF");
        srcCfg.set(MediaConfig::Filename, args.filename);
        MediaIO *src = MediaIO::create(srcCfg);
        if(src == nullptr) {
                std::fprintf(stderr, "Could not create source MediaIO\n");
                return 1;
        }

        // createForFileWrite resolves by extension (including masked
        // sequence paths like foo.%04d.dpx) which findFormatForPath
        // does not — the latter only fires canHandlePath() probes.
        MediaIO *dst = MediaIO::createForFileWrite(args.outputPath);
        if(dst == nullptr) {
                std::fprintf(stderr, "No MediaIO sink for '%s' (unknown extension)\n",
                             args.outputPath.cstr());
                delete src;
                return 1;
        }

        Error e = src->open(MediaIO::Source);
        if(e.isError()) {
                std::fprintf(stderr, "open source: %s\n", e.name().cstr());
                delete src; delete dst;
                return 1;
        }
        e = dst->open(MediaIO::Sink);
        if(e.isError()) {
                std::fprintf(stderr, "open sink: %s\n", e.name().cstr());
                src->close();
                delete src; delete dst;
                return 1;
        }

        const FrameCount srcCount = src->frameCount();
        const int64_t fc = srcCount.isFinite() ? srcCount.value() : -1;
        int64_t start = args.rangeStart;
        int64_t end   = args.rangeEnd < 0 ? fc : args.rangeEnd;
        if(fc >= 0 && end > fc) end = fc;
        int64_t count = (end > start) ? (end - start) : 0;

        int imageIdx = args.imageIndex;
        int audioIdx = args.audioIndex;
        auto mutate = [isImage, imageIdx, audioIdx]
                (const Frame::Ptr &in, int64_t) -> Frame::Ptr {
                return isImage ? trimForImage(in, imageIdx)
                               : trimForAudio(in, audioIdx);
        };

        auto [copied, ce] = MediaIO::copyFrames(src, dst, start, count, mutate);
        src->close();
        dst->close();
        delete src; delete dst;
        if(ce.isError()) {
                std::fprintf(stderr, "copyFrames: %s\n", ce.name().cstr());
                return 1;
        }
        const int64_t copiedInt = copied.isFinite() ? copied.value() : 0;
        std::printf("Wrote %lld frame%s to %s\n",
                    static_cast<long long>(copiedInt),
                    copiedInt == 1 ? "" : "s",
                    args.outputPath.cstr());
        return 0;
}

} // namespace

int main(int argc, char **argv) {
        Args args;
        if(!parseArgs(argc, argv, args)) {
                usage(argv[0]);
                return 1;
        }
        if(args.command == Args::CmdHelp) {
                usage(argv[0]);
                return 0;
        }

        // Extract paths open their own MediaIOs; everything else uses a
        // standalone DebugMediaFile for lightweight access.
        if(args.command == Args::CmdExtractImage) return cmdExtract(args, true);
        if(args.command == Args::CmdExtractAudio) return cmdExtract(args, false);

        DebugMediaFile f;
        Error e = f.open(args.filename, DebugMediaFile::Read);
        if(e.isError()) {
                std::fprintf(stderr, "open '%s': %s\n",
                             args.filename.cstr(), e.name().cstr());
                return 1;
        }

        switch(args.command) {
        case Args::CmdSummary:      return cmdSummary(f);
        case Args::CmdFrame:        return cmdFrame(f, args.frameIndex);
        case Args::CmdFrames:       return cmdFrames(f, args.rangeStart, args.rangeEnd);
        case Args::CmdFind:         return cmdFind(f, args);
        case Args::CmdDumpToc:      return cmdDumpToc(f);
        default:                    return 0;
        }
}
