/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * CSC benchmark utility.  Measures conversion throughput for a matrix of
 * PixelDesc pairs and writes the results to a JSON file for tracking
 * performance over time.
 *
 * With no -s/-d options, runs the built-in standard conversion set.
 * With -s and/or -d, benchmarks the cross product of the given formats.
 * Use -l to list all available PixelDesc names.
 */

#include <promeki/cscpipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/image.h>
#include <promeki/pixeldesc.h>
#include <promeki/elapsedtimer.h>
#include <promeki/buildinfo.h>
#include <promeki/json.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/datetime.h>
#include <promeki/cmdlineparser.h>
#include <cstdio>

using namespace promeki;

struct ConvPair {
        PixelDesc::ID src;
        PixelDesc::ID dst;
};

struct BenchResult {
        String          srcName;
        String          dstName;
        double          mpixPerSec;
        double          totalMs;
        int             iterations;
        bool            identity;
        bool            fastPath;
        int             stages;
};

static BenchResult benchConversion(PixelDesc::ID srcId, PixelDesc::ID dstId,
                                   size_t width, size_t height, int iterations,
                                   const MediaNodeConfig &config) {
        BenchResult result;
        result.srcName = PixelDesc(srcId).name();
        result.dstName = PixelDesc(dstId).name();
        result.iterations = iterations;

        CSCPipeline pipeline(srcId, dstId, config);
        result.identity = pipeline.isIdentity();
        result.fastPath = pipeline.isFastPath();
        result.stages = pipeline.stageCount();

        if(!pipeline.isValid()) {
                result.mpixPerSec = 0;
                result.totalMs = 0;
                return result;
        }

        Image src(width, height, srcId);
        Image dst(width, height, dstId);

        if(!src.isValid() || !dst.isValid()) {
                result.mpixPerSec = 0;
                result.totalMs = 0;
                return result;
        }

        // Fill source with a non-trivial pattern
        uint8_t *data = static_cast<uint8_t *>(src.data(0));
        size_t planeSize = src.plane(0)->size();
        for(size_t i = 0; i < planeSize; i++) {
                data[i] = static_cast<uint8_t>((i * 137 + 43) & 0xFF);
        }

        // Warm up
        pipeline.execute(src, dst);

        // Benchmark
        double totalPixels = static_cast<double>(width) * height * iterations;
        ElapsedTimer timer;
        for(int i = 0; i < iterations; i++) {
                pipeline.execute(src, dst);
        }
        int64_t elapsedUs = timer.elapsedUs();

        result.totalMs = elapsedUs / 1000.0;
        result.mpixPerSec = (elapsedUs > 0) ? (totalPixels / elapsedUs) : 0.0;
        return result;
}

static PixelDesc::ID resolveFormat(const String &name) {
        PixelDesc pd = PixelDesc::lookup(name);
        if(pd.isValid()) return pd.id();
        std::fprintf(stderr, "Error: unknown PixelDesc '%s'\n", name.cstr());
        return PixelDesc::Invalid;
}

static void listFormats() {
        auto ids = PixelDesc::registeredIDs();
        std::printf("Available PixelDesc formats (%d total, excluding compressed):\n\n",
                    static_cast<int>(ids.size()));
        for(auto id : ids) {
                PixelDesc pd(id);
                if(pd.isCompressed()) continue;
                std::printf("  %s\n", pd.name().cstr());
        }
        return;
}

static List<ConvPair> standardPairs() {
        List<ConvPair> pairs;
        auto add = [&](PixelDesc::ID s, PixelDesc::ID d) {
                pairs.pushToBack({s, d});
        };

        // Identity (baseline overhead measurement)
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::RGBA8_sRGB);
        add(PixelDesc::YUV8_422_Rec709,          PixelDesc::YUV8_422_Rec709);

        // Same color model, format change only
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::RGB8_sRGB);
        add(PixelDesc::RGB8_sRGB,                PixelDesc::RGBA8_sRGB);
        add(PixelDesc::BGRA8_sRGB,               PixelDesc::RGBA8_sRGB);

        // RGB -> YCbCr (capture/encode path)
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_Rec709);
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_UYVY_Rec709);
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_Planar_Rec709);
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_Planar_Rec709);
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_SemiPlanar_Rec709);

        // YCbCr -> RGB (decode/display path)
        add(PixelDesc::YUV8_422_Rec709,          PixelDesc::RGBA8_sRGB);
        add(PixelDesc::YUV8_422_UYVY_Rec709,     PixelDesc::RGBA8_sRGB);
        add(PixelDesc::YUV8_422_Planar_Rec709,   PixelDesc::RGBA8_sRGB);
        add(PixelDesc::YUV8_420_Planar_Rec709,   PixelDesc::RGBA8_sRGB);
        add(PixelDesc::YUV8_420_SemiPlanar_Rec709, PixelDesc::RGBA8_sRGB);

        // NV21 and NV16 paths
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_NV21_Rec709);
        add(PixelDesc::YUV8_420_NV21_Rec709,     PixelDesc::RGBA8_sRGB);
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_SemiPlanar_Rec709);
        add(PixelDesc::YUV8_422_SemiPlanar_Rec709, PixelDesc::RGBA8_sRGB);

        // 4:1:1
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_411_Planar_Rec709);
        add(PixelDesc::YUV8_411_Planar_Rec709,   PixelDesc::RGBA8_sRGB);

        // Cross-standard YCbCr
        add(PixelDesc::YUV8_422_Rec709,          PixelDesc::YUV8_422_Rec601);
        add(PixelDesc::YUV8_422_Rec601,          PixelDesc::YUV8_422_Rec709);

        // 10-bit conversions
        add(PixelDesc::RGBA10_LE_sRGB,           PixelDesc::YUV10_422_UYVY_LE_Rec709);
        add(PixelDesc::YUV10_422_UYVY_LE_Rec709, PixelDesc::RGBA10_LE_sRGB);
        add(PixelDesc::RGBA10_LE_sRGB,           PixelDesc::YUV10_422_Planar_LE_Rec709);
        add(PixelDesc::YUV10_422_Planar_LE_Rec709, PixelDesc::RGBA10_LE_sRGB);
        add(PixelDesc::RGBA10_LE_sRGB,           PixelDesc::YUV10_420_SemiPlanar_LE_Rec709);
        add(PixelDesc::YUV10_420_SemiPlanar_LE_Rec709, PixelDesc::RGBA10_LE_sRGB);
        add(PixelDesc::RGBA10_LE_sRGB,           PixelDesc::YUV10_422_v210_Rec709);
        add(PixelDesc::YUV10_422_v210_Rec709,    PixelDesc::RGBA10_LE_sRGB);

        // 10-bit Rec.2020
        add(PixelDesc::RGBA10_LE_sRGB,             PixelDesc::YUV10_422_UYVY_LE_Rec2020);
        add(PixelDesc::YUV10_422_UYVY_LE_Rec2020,  PixelDesc::RGBA10_LE_sRGB);
        add(PixelDesc::RGBA10_LE_sRGB,             PixelDesc::YUV10_420_Planar_LE_Rec2020);
        add(PixelDesc::YUV10_420_Planar_LE_Rec2020, PixelDesc::RGBA10_LE_sRGB);

        // Bit depth change
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::RGBA16_LE_sRGB);
        add(PixelDesc::RGBA16_LE_sRGB,           PixelDesc::RGBA8_sRGB);

        // Linear <-> nonlinear
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::RGBAF32_LE_LinearRec709);
        add(PixelDesc::RGBAF32_LE_LinearRec709,  PixelDesc::RGBA8_sRGB);

        // Monochrome
        add(PixelDesc::RGBA8_sRGB,               PixelDesc::Mono8_sRGB);
        add(PixelDesc::Mono8_sRGB,               PixelDesc::RGBA8_sRGB);

        return pairs;
}

int main(int argc, char **argv) {
        int width = 1920;
        int height = 1080;
        int iterations = 10;
        String outputPath = "cscbench.json";
        bool quiet = false;
        bool list = false;
        StringList srcFormats;
        StringList dstFormats;
        MediaNodeConfig config;

        CmdLineParser parser;
        parser.registerOptions({
                {'w', "width",      "Image width in pixels (default: 1920)",
                 CmdLineParser::OptionIntCallback([&](int v) { width = v; return 0; })},
                {'e', "height",     "Image height in pixels (default: 1080)",
                 CmdLineParser::OptionIntCallback([&](int v) { height = v; return 0; })},
                {'n', "iterations", "Iterations per conversion (default: 10)",
                 CmdLineParser::OptionIntCallback([&](int v) { iterations = v; return 0; })},
                {'o', "output",     "Output JSON file path (default: cscbench.json)",
                 CmdLineParser::OptionStringCallback([&](const String &s) { outputPath = s; return 0; })},
                {'s', "src",        "Source PixelDesc name (repeatable, cross with -d)",
                 CmdLineParser::OptionStringCallback([&](const String &s) { srcFormats.pushToBack(s); return 0; })},
                {'d', "dst",        "Target PixelDesc name (repeatable, cross with -s)",
                 CmdLineParser::OptionStringCallback([&](const String &s) { dstFormats.pushToBack(s); return 0; })},
                {'c', "config",     "CSC config hint as key=value (repeatable, e.g. -c Path=scalar)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                        size_t eq = s.find('=');
                        if(eq == String::npos) {
                                std::fprintf(stderr, "Error: -c value must be key=value, got '%s'\n", s.cstr());
                                return 1;
                        }
                        config.set(s.left(eq), s.mid(eq + 1));
                        return 0;
                 })},
                {'l', "list",       "List all available PixelDesc names and exit",
                 CmdLineParser::OptionCallback([&]() { list = true; return 0; })},
                {'q', "quiet",      "Suppress per-conversion output",
                 CmdLineParser::OptionCallback([&]() { quiet = true; return 0; })},
        });

        int ret = parser.parseMain(argc, argv);
        if(ret != 0) return ret;

        if(list) {
                listFormats();
                return 0;
        }

        // Build the conversion pair list
        List<ConvPair> pairs;
        if(srcFormats.isEmpty() && dstFormats.isEmpty()) {
                // No custom formats: run the standard set
                pairs = standardPairs();
        } else {
                // Resolve format names to IDs
                List<PixelDesc::ID> srcIDs;
                List<PixelDesc::ID> dstIDs;

                for(const auto &name : srcFormats) {
                        PixelDesc::ID id = resolveFormat(name);
                        if(id == PixelDesc::Invalid) return 1;
                        srcIDs.pushToBack(id);
                }
                for(const auto &name : dstFormats) {
                        PixelDesc::ID id = resolveFormat(name);
                        if(id == PixelDesc::Invalid) return 1;
                        dstIDs.pushToBack(id);
                }

                // If only one side given, use it as both
                if(srcIDs.isEmpty()) srcIDs = dstIDs;
                if(dstIDs.isEmpty()) dstIDs = srcIDs;

                // Cross product
                for(auto sid : srcIDs) {
                        for(auto did : dstIDs) {
                                pairs.pushToBack({sid, did});
                        }
                }
        }

        if(pairs.isEmpty()) {
                std::fprintf(stderr, "No conversion pairs to benchmark.\n");
                return 1;
        }

        double totalMpix = static_cast<double>(width) * height / 1e6;
        const BuildInfo *bi = getBuildInfo();

        std::printf("CSC Benchmark\n");
        std::printf("  Build:      %s\n", bi->repoident);
        std::printf("  Image:      %dx%d (%.2f Mpix)\n", width, height, totalMpix);
        std::printf("  Iterations: %d\n", iterations);
        std::printf("  Pairs:      %d\n", static_cast<int>(pairs.size()));
        String path = config.get(CSCPipeline::KeyPath, "optimized").get<String>();
        std::printf("  Path:       %s\n\n", path.cstr());

        if(!quiet) {
                std::printf("%-40s %-40s %8s %8s %6s %s\n",
                            "Source", "Target", "Mpix/s", "ms/frm", "Stages", "Flags");
                for(int i = 0; i < 120; i++) std::putchar('-');
                std::putchar('\n');
        }

        List<BenchResult> results;
        for(const auto &p : pairs) {
                BenchResult r = benchConversion(p.src, p.dst,
                                                width, height, iterations, config);
                results.pushToBack(r);

                if(!quiet) {
                        double msPerFrame = (r.iterations > 0 && r.totalMs > 0)
                                ? r.totalMs / r.iterations : 0;
                        String flags;
                        if(r.identity) flags = "identity";
                        else if(r.fastPath) flags = "fastpath";

                        std::printf("%-40s %-40s %8.1f %8.2f %6d %s\n",
                                    r.srcName.cstr(), r.dstName.cstr(),
                                    r.mpixPerSec, msPerFrame, r.stages,
                                    flags.cstr());
                }
        }

        // Write JSON output
        JsonObject root;
        root.set("version", 1);
        root.set("date", DateTime::now().toString());
        root.set("build", bi->repoident);
        root.set("width", width);
        root.set("height", height);
        root.set("iterations", iterations);
        root.set("path", path);

        JsonArray benchmarks;
        for(const auto &r : results) {
                JsonObject entry;
                entry.set("src", r.srcName);
                entry.set("dst", r.dstName);
                entry.set("mpix_per_sec", r.mpixPerSec);
                entry.set("ms_total", r.totalMs);
                entry.set("ms_per_frame", (r.iterations > 0) ? r.totalMs / r.iterations : 0.0);
                entry.set("iterations", r.iterations);
                entry.set("stages", r.stages);
                entry.set("identity", r.identity);
                entry.set("fast_path", r.fastPath);
                benchmarks.add(entry);
        }
        root.set("benchmarks", benchmarks);

        String json = root.toString(2);
        FILE *fp = std::fopen(outputPath.cstr(), "w");
        if(fp) {
                std::fputs(json.cstr(), fp);
                std::fputc('\n', fp);
                std::fclose(fp);
                std::printf("\nResults written to %s\n", outputPath.cstr());
        } else {
                std::fprintf(stderr, "Error: could not open %s for writing\n", outputPath.cstr());
                return 1;
        }

        return 0;
}
