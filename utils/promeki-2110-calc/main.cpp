/**
 * @file      main.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * promeki-2110-calc — command-line front-end for the SMPTE ST 2110-21
 * traffic-shaping calculator (@ref promeki::St2110TrafficCalc).  Give
 * it a video format, a wire pixel format (or an explicit
 * sampling+depth), and a sender class and it prints every value the
 * standard's §6 / §7 model is expressed in.
 */

#include <cstdio>
#include <cstdlib>

#include <promeki/cmdlineparser.h>
#include <promeki/error.h>
#include <promeki/pixelformat.h>
#include <promeki/st2110trafficcalc.h>
#include <promeki/st2110video.h>
#include <promeki/string.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

void printUsage(const CmdLineParser &parser) {
        std::printf("promeki-2110-calc — SMPTE ST 2110-21 traffic-shaping calculator\n\n");
        std::printf("Usage:\n");
        std::printf("  promeki-2110-calc --format <fmt> [--pixel-format <pf> | --sampling <s> --depth <d>]\n");
        std::printf("                    [--type <N|NL|W>] [--packing <gpm|bpm>] [--mtu <octets>]\n\n");
        std::printf("Examples:\n");
        std::printf("  promeki-2110-calc -f 1080p59.94 -s YCbCr422 -d 10 -t N\n");
        std::printf("  promeki-2110-calc -f 2160p60 -p YUV10_422_2110_Rec709 -t W --mtu 9000\n\n");
        std::printf("Options:\n");
        const StringList usage = parser.generateUsage();
        for (size_t i = 0; i < usage.size(); ++i) {
                std::printf("  %s\n", usage[i].cstr());
        }
}

// Maps a friendly --type value (N / NL / W, case-insensitive, with or
// without a 2110TP prefix) to the matching sender class.
RtpSenderType parseSenderType(const String &v) {
        const String u = v.toUpper();
        if (u == String("N") || u == String("2110TPN")) return RtpSenderType::TypeN;
        if (u == String("NL") || u == String("2110TPNL")) return RtpSenderType::TypeNL;
        if (u == String("W") || u == String("2110TPW")) return RtpSenderType::TypeW;
        return RtpSenderType::Unknown;
}

// Resolves a --sampling value.  Accepts the project enum name
// ("YCbCr422"), a bare structure ("422" → YCbCr 4:2:2), or the
// ST 2110-20 wire spelling ("YCbCr-4:2:2").
St2110Sampling parseSampling(const String &v) {
        const String u = v.toUpper();
        if (u == String("444")) return St2110Sampling::YCbCr444;
        if (u == String("422")) return St2110Sampling::YCbCr422;
        if (u == String("420")) return St2110Sampling::YCbCr420;
        St2110Sampling viaWire = St2110Video::samplingFromWire(v);
        if (viaWire.isValid()) return viaWire;
        return St2110Sampling(v); // project enum name, e.g. "YCbCr422" / "Rgb"
}

// Resolves a --depth value.  Accepts a bare bit count ("10") or the
// project enum name ("Bits10").
St2110Depth parseDepth(const String &v) {
        St2110Depth viaWire = St2110Video::depthFromWire(v);
        if (viaWire.isValid()) return viaWire;
        return St2110Depth(v);
}

} // namespace

int main(int argc, char *argv[]) {
        String           formatStr;
        String           pixelFormatStr;
        String           samplingStr;
        String           depthStr;
        String           typeStr("N");
        String           packingStr("gpm");
        int              mtu = 1500;
        bool             showHelp = false;

        CmdLineParser parser;
        parser.registerOptions({
                {'h', "help", "Show this help and exit",
                 CmdLineParser::OptionCallback([&]() { showHelp = true; return 0; })},
                {'f', "format", "Video format, e.g. 1080p59.94, 2160p60, 1080i59.94",
                 CmdLineParser::OptionStringCallback([&](const String &s) { formatStr = s; return 0; })},
                {'p', "pixel-format", "Wire/source PixelFormat name (mapped to 2110 sampling+depth)",
                 CmdLineParser::OptionStringCallback([&](const String &s) { pixelFormatStr = s; return 0; })},
                {'s', "sampling", "ST 2110-20 sampling: 444/422/420 or YCbCr422/Rgb/...",
                 CmdLineParser::OptionStringCallback([&](const String &s) { samplingStr = s; return 0; })},
                {'d', "depth", "ST 2110-20 bit depth: 8/10/12/16 (or Bits10, ...)",
                 CmdLineParser::OptionStringCallback([&](const String &s) { depthStr = s; return 0; })},
                {'t', "type", "Sender class: N (narrow), NL (narrow linear), W (wide)",
                 CmdLineParser::OptionStringCallback([&](const String &s) { typeStr = s; return 0; })},
                {0, "packing", "Packing mode: gpm (default) or bpm",
                 CmdLineParser::OptionStringCallback([&](const String &s) { packingStr = s; return 0; })},
                {0, "mtu", "IP MTU / MAXIP in octets (default 1500)",
                 CmdLineParser::OptionIntCallback([&](int v) { mtu = v; return 0; })},
        });

        if (parser.parseMain(argc, argv) != 0) {
                printUsage(parser);
                return 2;
        }
        if (showHelp) {
                printUsage(parser);
                return 0;
        }

        if (formatStr.isEmpty()) {
                std::fprintf(stderr, "error: --format is required\n\n");
                printUsage(parser);
                return 2;
        }

        // ---- Video format ----------------------------------------
        auto [format, fmtErr] = VideoFormat::fromString(formatStr);
        if (fmtErr.isError() || !format.isValid()) {
                std::fprintf(stderr, "error: could not parse video format '%s'\n", formatStr.cstr());
                return 2;
        }

        // ---- Sender type -----------------------------------------
        const RtpSenderType senderType = parseSenderType(typeStr);
        if (senderType == RtpSenderType::Unknown) {
                std::fprintf(stderr, "error: --type must be one of N, NL, W (got '%s')\n",
                             typeStr.cstr());
                return 2;
        }

        // ---- Packetization model ---------------------------------
        St2110TrafficCalc::PacketModel model;
        model.mtu = mtu;
        const String pk = packingStr.toLower();
        if (pk == String("bpm")) {
                model.packingMode = St2110PackingMode::Bpm;
        } else if (pk == String("gpm")) {
                model.packingMode = St2110PackingMode::Gpm;
        } else {
                std::fprintf(stderr, "error: --packing must be gpm or bpm (got '%s')\n",
                             packingStr.cstr());
                return 2;
        }

        // ---- Compute (PixelFormat path or explicit sampling+depth)
        St2110TrafficCalc::Result result;
        if (!pixelFormatStr.isEmpty()) {
                Error            pfErr;
                const PixelFormat pf = PixelFormat::lookup(pixelFormatStr, &pfErr);
                if (pfErr.isError()) {
                        std::fprintf(stderr, "error: unknown pixel format '%s'\n",
                                     pixelFormatStr.cstr());
                        return 2;
                }
                result = St2110TrafficCalc::compute(format, pf, senderType, model);
        } else {
                if (samplingStr.isEmpty() || depthStr.isEmpty()) {
                        std::fprintf(stderr,
                                     "error: specify either --pixel-format or both "
                                     "--sampling and --depth\n");
                        return 2;
                }
                const St2110Sampling sampling = parseSampling(samplingStr);
                const St2110Depth    depth = parseDepth(depthStr);
                if (!sampling.isValid()) {
                        std::fprintf(stderr, "error: could not parse --sampling '%s'\n",
                                     samplingStr.cstr());
                        return 2;
                }
                if (!depth.isValid()) {
                        std::fprintf(stderr, "error: could not parse --depth '%s'\n",
                                     depthStr.cstr());
                        return 2;
                }
                result = St2110TrafficCalc::compute(format, sampling, depth, senderType, model);
        }

        if (!result.isValid()) {
                std::fprintf(stderr, "error: calculation failed (%s)\n", result.error.name().cstr());
                return 1;
        }

        std::printf("%s", result.toString().cstr());
        return 0;
}
