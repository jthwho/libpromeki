/**
 * @file      csc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Color-space conversion benchmark cases for promeki-bench.
 *
 * Each source → destination pair in the standard matrix is registered
 * as an individual `BenchmarkCase` so baseline tracking and regression
 * comparisons can report per-pair deltas.  When `csc.src` and / or
 * `csc.dst` are set in `BenchParams` the cross product of those lists
 * is registered instead.
 *
 * ### BenchParams keys read by this suite
 *
 * | Key                  | Type        | Default  | Description                                |
 * |----------------------|-------------|----------|--------------------------------------------|
 * | `csc.width`          | int         | 1920     | Image width in pixels                      |
 * | `csc.height`         | int         | 1080     | Image height in pixels                     |
 * | `csc.src`            | StringList  | (none)   | PixelDesc names used as conversion sources |
 * | `csc.dst`            | StringList  | (none)   | PixelDesc names used as conversion sinks   |
 * | `csc.config.<KEY>`   | Scalar      | (none)   | MediaConfig override passed to CSCPipeline |
 *
 * When `csc.src` and `csc.dst` are both empty the standard conversion
 * matrix is registered; otherwise the cross product of the two lists
 * is registered (a single-sided list uses itself for the missing side,
 * matching the legacy cscbench semantics).
 */

#include "cases.h"
#include "../benchparams.h"

#include <promeki/config.h>

#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC

#include <promeki/benchmarkrunner.h>
#include <promeki/cscpipeline.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/image.h>
#include <promeki/pixeldesc.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <cstdio>

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

namespace {

struct ConvPair {
        PixelDesc::ID src;
        PixelDesc::ID dst;
};

/**
 * @brief Looks up a PixelDesc by name, returning Invalid and logging on miss.
 *
 * Declared early so the anchor / pair-generation helpers below can
 * use it; the same function is also used by `registerCscCases()` to
 * resolve user-supplied `csc.src` / `csc.dst` names.
 */
PixelDesc::ID resolveFormat(const String &name) {
        PixelDesc pd = PixelDesc::lookup(name);
        if(pd.isValid()) return pd.id();
        std::fprintf(stderr, "promeki-bench: unknown PixelDesc '%s'\n", name.cstr());
        return PixelDesc::Invalid;
}

/**
 * @brief Returns every registered non-compressed PixelDesc ID.
 *
 * CSC benches care about scalar pixel conversion; compressed formats
 * (JPEG, JPEG XS) belong in a dedicated codec suite, so they are
 * filtered out here.
 */
List<PixelDesc::ID> nonCompressedFormats() {
        List<PixelDesc::ID> out;
        PixelDesc::IDList ids = PixelDesc::registeredIDs();
        for(auto id : ids) {
                PixelDesc pd(id);
                if(pd.isCompressed()) continue;
                out.pushToBack(id);
        }
        return out;
}

/**
 * @brief Canonical anchor PixelDesc names used when `csc.anchors` is unset.
 *
 * RGBA8_sRGB is the canonical RGB pivot — almost every capture,
 * display, and codec path goes through it at some point.
 * YUV8_422_Rec709 is the canonical YCbCr pivot for HD video work.
 * Together they form a complete graph over the common CSC paths
 * without forcing a full N² run.
 */
StringList defaultAnchorNames() {
        StringList out;
        out.pushToBack(String("RGBA8_sRGB"));
        out.pushToBack(String("YUV8_422_Rec709"));
        return out;
}

/**
 * @brief Resolves the active anchor list from BenchParams.
 *
 * Reads `csc.anchors` (a StringList) and falls back to
 * `defaultAnchorNames()` if the key is absent.  Names that do not
 * resolve to a valid `PixelDesc::ID` are skipped with a warning.
 */
List<PixelDesc::ID> resolveAnchors() {
        StringList names = benchParams().getStringList(String("csc.anchors"));
        if(names.isEmpty()) names = defaultAnchorNames();

        List<PixelDesc::ID> out;
        for(const auto &n : names) {
                PixelDesc::ID id = resolveFormat(n);
                if(id == PixelDesc::Invalid) continue;
                out.pushToBack(id);
        }
        return out;
}

/**
 * @brief Packs an ordered (src, dst) PixelDesc pair into a dedup key.
 *
 * PixelDesc IDs are small (a few hundred at most), so a 32-bit packed
 * key fits comfortably and gives us O(1) dedup lookups via Map.
 */
uint32_t packPairKey(PixelDesc::ID s, PixelDesc::ID d) {
        return (static_cast<uint32_t>(s) << 16) | static_cast<uint32_t>(d);
}

/**
 * @brief Builds the default CSC pair set anchored on the resolved anchor list.
 *
 * For every anchor, every non-compressed format becomes a pair in
 * both directions (anchor → format and format → anchor).  Pairs that
 * fail `CSCPipeline::isValid()` are dropped.  Duplicates (e.g. the
 * anchor-to-anchor identity pair reached twice when multiple anchors
 * overlap) are filtered through a packed-key dedup map.
 *
 * The size is bounded by `2 × anchors × formats`, which is ~540
 * pairs with the default two anchors and ~135 registered formats —
 * a ~5-minute run at default settings, versus the ~2½ hours the full
 * matrix would take.
 */
List<ConvPair> anchoredPairs() {
        List<ConvPair> pairs;
        List<PixelDesc::ID> anchors = resolveAnchors();
        if(anchors.isEmpty()) return pairs;

        List<PixelDesc::ID> candidates = nonCompressedFormats();
        MediaConfig validityConfig;
        Map<uint32_t, bool> seen;

        auto add = [&](PixelDesc::ID s, PixelDesc::ID d) {
                uint32_t key = packPairKey(s, d);
                if(seen.contains(key)) return;
                CSCPipeline pipeline(s, d, validityConfig);
                if(!pipeline.isValid()) return;
                pairs.pushToBack({s, d});
                seen.insert(key, true);
        };

        for(auto anc : anchors) {
                for(auto cand : candidates) {
                        add(anc, cand);
                        add(cand, anc);
                }
        }
        return pairs;
}

/**
 * @brief Enumerates every valid CSC conversion the library supports.
 *
 * The full N² matrix of non-compressed `PixelDesc` IDs, filtered
 * against `CSCPipeline::isValid()`.  Used when `csc.full=1` is set
 * or when the user explicitly asks for exhaustive coverage.  Expect
 * ~18k cases and a multi-hour run at default measurement settings;
 * this mode is for regression sweeps, not day-to-day dev work.
 */
List<ConvPair> fullMatrixPairs() {
        List<ConvPair> pairs;
        List<PixelDesc::ID> candidates = nonCompressedFormats();
        MediaConfig validityConfig;

        for(auto src : candidates) {
                for(auto dst : candidates) {
                        CSCPipeline pipeline(src, dst, validityConfig);
                        if(!pipeline.isValid()) continue;
                        pairs.pushToBack({src, dst});
                }
        }
        return pairs;
}

/**
 * @brief Reads `csc.config.*` entries out of BenchParams into a MediaConfig.
 *
 * Every BenchParams key that begins with `csc.config.` contributes one
 * entry to the returned MediaConfig; the suffix after the prefix is
 * passed through as a MediaConfigID, letting users override any
 * CSC-relevant key without the case file knowing about it in advance.
 */
MediaConfig readCscMediaConfig() {
        MediaConfig cfg;
        const String prefix("csc.config.");
        benchParams().forEach([&](const String &key, const StringList &values) {
                if(!key.startsWith(prefix)) return;
                if(values.isEmpty()) return;
                String suffix = key.mid(prefix.size());
                if(suffix.isEmpty()) return;
                cfg.set(MediaConfigID(suffix), values.back());
        });
        return cfg;
}

/**
 * @brief Builds the case body for a single src → dst pair.
 *
 * The returned callable captures the pair by value, constructs a
 * `CSCPipeline` at case-invocation time (so BenchParams changes such
 * as `csc.config.CscPath` are honored), preallocates source and
 * destination images, warms the source buffer with a non-trivial
 * pattern, and executes the pipeline in the hot loop.
 */
BenchmarkCase::Function buildCase(ConvPair pair) {
        return [pair](BenchmarkState &state) {
                BenchParams &params = benchParams();
                int width  = params.getInt(String("csc.width"), 1920);
                int height = params.getInt(String("csc.height"), 1080);

                MediaConfig cfg = readCscMediaConfig();
                CSCPipeline pipeline(pair.src, pair.dst, cfg);
                bool identity = pipeline.isIdentity();
                bool fastPath = pipeline.isFastPath();
                int  stages   = pipeline.stageCount();

                if(!pipeline.isValid()) {
                        state.setCounter(String("invalid"), 1.0);
                        // Run a no-op loop so the runner sees iterations.
                        for(auto _ : state) (void)_;
                        return;
                }

                Image src(width, height, pair.src);
                Image dst(width, height, pair.dst);
                if(!src.isValid() || !dst.isValid()) {
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }

                // Fill source with a non-trivial pattern so codec-like
                // paths can't take shortcuts on zeroed input.
                uint8_t *data = static_cast<uint8_t *>(src.data(0));
                size_t planeSize = src.plane(0)->size();
                for(size_t i = 0; i < planeSize; i++) {
                        data[i] = static_cast<uint8_t>((i * 137 + 43) & 0xFF);
                }

                // Discard the first conversion — the warmup covers
                // one-time allocations and CPU caches inside the
                // pipeline.
                pipeline.execute(src, dst);

                double mpix = static_cast<double>(width)
                            * static_cast<double>(height);
                size_t bytesPerIter = static_cast<size_t>(planeSize);

                for(auto _ : state) {
                        (void)_;
                        pipeline.execute(src, dst);
                }

                state.setItemsProcessed(state.iterations());
                state.setBytesProcessed(state.iterations() * bytesPerIter);

                state.setCounter(String("mpix_per_iter"), mpix / 1.0e6);
                state.setCounter(String("stages"), static_cast<double>(stages));
                state.setCounter(String("identity"), identity ? 1.0 : 0.0);
                state.setCounter(String("fast_path"), fastPath ? 1.0 : 0.0);

                String label = String::number(width) + "x" + String::number(height)
                             + " " + PixelDesc(pair.src).name()
                             + " -> " + PixelDesc(pair.dst).name();
                state.setLabel(label);
        };
}

String caseName(ConvPair pair) {
        return PixelDesc(pair.src).name()
             + "_to_"
             + PixelDesc(pair.dst).name();
}

String caseDescription(ConvPair pair) {
        return String("CSC from ") + PixelDesc(pair.src).name()
             + " to " + PixelDesc(pair.dst).name();
}

} // namespace

void registerCscCases() {
        const BenchParams &params = benchParams();
        StringList customSrc = params.getStringList(String("csc.src"));
        StringList customDst = params.getStringList(String("csc.dst"));
        bool       fullMatrix = params.getBool(String("csc.full"), false);

        List<ConvPair> pairs;

        if(!customSrc.isEmpty() || !customDst.isEmpty()) {
                // Explicit cross product overrides the default set.
                List<PixelDesc::ID> srcIDs;
                List<PixelDesc::ID> dstIDs;
                for(const auto &n : customSrc) {
                        PixelDesc::ID id = resolveFormat(n);
                        if(id == PixelDesc::Invalid) continue;
                        srcIDs.pushToBack(id);
                }
                for(const auto &n : customDst) {
                        PixelDesc::ID id = resolveFormat(n);
                        if(id == PixelDesc::Invalid) continue;
                        dstIDs.pushToBack(id);
                }
                if(srcIDs.isEmpty()) srcIDs = dstIDs;
                if(dstIDs.isEmpty()) dstIDs = srcIDs;

                for(auto sid : srcIDs) {
                        for(auto did : dstIDs) {
                                pairs.pushToBack({sid, did});
                        }
                }
        } else if(fullMatrix) {
                // Exhaustive mode: every valid pair, ~18k cases, for
                // regression sweeps.
                pairs = fullMatrixPairs();
        } else {
                // Default: every conversion that touches a canonical
                // anchor format (~500 pairs at default anchor count),
                // auto-generated from PixelDesc::registeredIDs() so the
                // set expands automatically as new formats land.
                pairs = anchoredPairs();
        }

        for(const auto &p : pairs) {
                BenchmarkRunner::registerCase(BenchmarkCase(
                        String("csc"),
                        caseName(p),
                        caseDescription(p),
                        buildCase(p)));
        }
        return;
}

String cscParamHelp() {
        return String(
                "csc suite parameters:\n"
                "  csc.width=<int>          Image width in pixels (default: 1920)\n"
                "  csc.height=<int>         Image height in pixels (default: 1080)\n"
                "  csc.anchors+=<name>      Canonical pivot format (default anchors:\n"
                "                             RGBA8_sRGB, YUV8_422_Rec709)\n"
                "  csc.full=1               Register every valid pair (full N x N matrix,\n"
                "                             ~18k cases — for regression sweeps only)\n"
                "  csc.src+=<name>          Explicit source PixelDesc (overrides the default\n"
                "                             set and forms a cross product with csc.dst)\n"
                "  csc.dst+=<name>          Explicit destination PixelDesc\n"
                "  csc.config.<KEY>=<val>   MediaConfig override passed into the CSCPipeline\n"
                "                             e.g. csc.config.CscPath=Scalar\n"
                "\n"
                "  The CSC case set is generated by walking PixelDesc::registeredIDs(),\n"
                "  filtering out compressed formats, and validating each candidate\n"
                "  against CSCPipeline — there is no hand-curated list, so new pixel\n"
                "  formats show up in the bench automatically.  The default mode\n"
                "  registers only pairs where at least one endpoint is a canonical\n"
                "  anchor (configurable via csc.anchors), keeping run time reasonable\n"
                "  at roughly 500 pairs.  Use csc.full=1 for exhaustive coverage, or\n"
                "  csc.src / csc.dst for a targeted cross product.\n");
}

} // namespace benchutil
PROMEKI_NAMESPACE_END

#else // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

void registerCscCases() {
        // CSC disabled at configure time — no cases to register.
}

String cscParamHelp() {
        return String(
                "csc suite parameters: (disabled — built without PROMEKI_ENABLE_CSC)\n");
}

} // namespace benchutil
PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC
