/**
 * @file      inspector.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * @ref MediaIOTask_Inspector full-pipeline benchmark cases for
 * promeki-bench.  Each case wires a default-config @c TPG MediaIO
 * source to a default-config @c Inspector MediaIO sink and pumps
 * frames through the pair, measuring frames-per-second of the
 * end-to-end "produce → encode data band → decode data band → run
 * all four checks → drop" loop.
 *
 * This is the closest the bench gets to a production-shaped number:
 * it covers the encoder, the decoder, the LTC decoder, the CSC
 * pipeline (twice — once for the encoder primer build, once for the
 * decoder slice), and all of the inspector's per-frame bookkeeping.
 * If the inspector ever shows up as a bottleneck in real use, this
 * is the case to look at first.
 *
 * ### BenchParams keys read by this suite
 *
 * | Key                  | Type        | Default      | Description                                  |
 * |----------------------|-------------|--------------|----------------------------------------------|
 * | `inspector.size+=`   | StringList  | "1920x1080"  | Frame dimensions, repeatable                 |
 * | `inspector.format+=` | StringList  | "RGBA8_sRGB" | PixelDesc names to bench                     |
 *
 * The default keeps the case set small (1 case per (format, size)
 * pair) because each iteration drives a TPG read + an Inspector
 * write — heavier than the pure encoder / decoder cases that share
 * a single image across iterations.
 */

#include "cases.h"
#include "../benchparams.h"

#include <promeki/config.h>

#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC

#include <promeki/benchmarkrunner.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_inspector.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/videoformat.h>
#include <promeki/pixeldesc.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <cstdio>
#include <cstdlib>

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

namespace {

struct PipelineSpec {
        PixelDesc::ID pd;
        uint32_t      width;
        uint32_t      height;
};

PixelDesc::ID resolveInspectorFormat(const String &name) {
        PixelDesc pd = PixelDesc::lookup(name);
        if(pd.isValid()) return pd.id();
        std::fprintf(stderr,
                     "promeki-bench: inspector: unknown PixelDesc '%s'\n",
                     name.cstr());
        return PixelDesc::Invalid;
}

bool parseInspectorSize(const String &s, uint32_t &w, uint32_t &h) {
        const char *p = s.cstr();
        char *end = nullptr;
        long ww = std::strtol(p, &end, 10);
        if(end == p || *end != 'x') return false;
        const char *q = end + 1;
        char *end2 = nullptr;
        long hh = std::strtol(q, &end2, 10);
        if(end2 == q || ww <= 0 || hh <= 0) return false;
        w = static_cast<uint32_t>(ww);
        h = static_cast<uint32_t>(hh);
        return true;
}

List<PipelineSpec> resolveSpecs() {
        List<PipelineSpec> out;
        StringList sizeNames = benchParams().getStringList(String("inspector.size"));
        if(sizeNames.isEmpty()) sizeNames.pushToBack(String("1920x1080"));
        StringList fmtNames = benchParams().getStringList(String("inspector.format"));
        if(fmtNames.isEmpty()) fmtNames.pushToBack(String("RGBA8_sRGB"));

        for(const auto &fn : fmtNames) {
                PixelDesc::ID id = resolveInspectorFormat(fn);
                if(id == PixelDesc::Invalid) continue;
                for(const auto &sn : sizeNames) {
                        uint32_t w = 0, h = 0;
                        if(!parseInspectorSize(sn, w, h)) {
                                std::fprintf(stderr,
                                             "promeki-bench: inspector: bad size '%s' "
                                             "(expected WxH)\n", sn.cstr());
                                continue;
                        }
                        out.pushToBack({id, w, h});
                }
        }
        return out;
}

String pipelineSizeLabel(const PipelineSpec &p) {
        return String::number(p.width) + "x" + String::number(p.height);
}

/**
 * @brief Builds the bench closure for one TPG → Inspector pipeline spec.
 *
 * Setup happens once per case invocation (outside the timed loop):
 * the TPG and Inspector are constructed, configured, and opened.
 * The hot path then reads one frame from the TPG and writes it to
 * the inspector per iteration — the same path a real consumer
 * would exercise at video rate.
 */
BenchmarkCase::Function buildPipelineCase(PipelineSpec spec) {
        return [spec](BenchmarkState &state) {
                // Source: a default-config TPG with the dimensions
                // and pixel format under test.  Frame rate is fixed
                // to 30 fps so the LTC decoder lock-in time is
                // predictable across runs.
                MediaIO::Config tpgCfg = MediaIO::defaultConfig("TPG");
                tpgCfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(spec.width, spec.height), FrameRate(FrameRate::FPS_30)));
                tpgCfg.set(MediaConfig::VideoPixelFormat, PixelDesc(spec.pd));
                MediaIO *tpg = MediaIO::create(tpgCfg);
                if(tpg == nullptr || tpg->open(MediaIO::Output).isError()) {
                        if(tpg != nullptr) delete tpg;
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }

                // Sink: a default-config Inspector with the periodic
                // log disabled so log throughput doesn't influence
                // the measurement.
                MediaIO::Config insCfg = MediaIO::defaultConfig("Inspector");
                insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
                MediaIO *inspector = MediaIO::create(insCfg);
                if(inspector == nullptr || inspector->open(MediaIO::Input).isError()) {
                        if(inspector != nullptr) delete inspector;
                        tpg->close();
                        delete tpg;
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }

                // Untimed warmup: pump enough frames to:
                //   - prime the encoder primer cells via CSC
                //   - prime the LtcDecoder lock-in
                //   - prime the decoder slice / CSC cache
                // 8 frames is comfortably more than the LTC lock-in
                // window at 30 fps and keeps the warmup tiny.
                for(int i = 0; i < 8; i++) {
                        Frame::Ptr frame;
                        if(tpg->readFrame(frame).isError()) break;
                        inspector->writeFrame(frame);
                }

                // Hot path: one TPG read + one Inspector write per
                // iteration.
                for(auto _ : state) {
                        (void)_;
                        Frame::Ptr frame;
                        tpg->readFrame(frame);
                        inspector->writeFrame(frame);
                }

                state.setItemsProcessed(state.iterations());
                // Approximate bytes per iter = the picture's plane-0
                // payload, which is what the inspector actually
                // reads.  Audio bytes are negligible by comparison.
                const size_t bytesPerIter =
                        static_cast<size_t>(spec.width) *
                        static_cast<size_t>(spec.height) * 4;  // assume 4 bytes/pixel for the upper bound
                state.setBytesProcessed(state.iterations() * bytesPerIter);
                state.setLabel(pipelineSizeLabel(spec) + " " +
                               PixelDesc(spec.pd).name() +
                               " TPG → Inspector pipeline");

                tpg->close();
                inspector->close();
                delete tpg;
                delete inspector;
        };
}

}  // namespace

void registerInspectorCases() {
        const String suite("inspector");
        for(const auto &spec : resolveSpecs()) {
                String name = String("pipeline_") + pipelineSizeLabel(spec) + "_" +
                              PixelDesc(spec.pd).name();
                String desc = String("TPG → Inspector full-pipeline frame loop "
                                     "(decode image data + LTC + A/V sync + continuity) — ") +
                              pipelineSizeLabel(spec) + " " + PixelDesc(spec.pd).name();
                BenchmarkRunner::registerCase(BenchmarkCase(
                        suite, name, desc, buildPipelineCase(spec)));
        }
}

String inspectorParamHelp() {
        return String(
                "inspector suite parameters:\n"
                "  inspector.size+=WxH      Add a frame size (default: 1920x1080)\n"
                "  inspector.format+=<name> Add a PixelDesc (default: RGBA8_sRGB)\n"
                "\n"
                "  Each (format, size) pair registers a single case that drives a\n"
                "  default-config TPG → default-config Inspector pipeline at 30 fps.\n"
                "  Every iteration reads one frame from the TPG and writes it to the\n"
                "  Inspector — the inspector decodes both data bands, decodes the\n"
                "  audio LTC, computes the A/V sync offset, and runs the continuity\n"
                "  check.  This is the closest measurement promeki-bench has to\n"
                "  end-to-end production cost; if the inspector shows up in a profile,\n"
                "  this case is the right starting point for investigation.\n"
                "  The Inspector's periodic log is disabled inside the bench so log\n"
                "  output doesn't influence the measurement.\n");
}

}  // namespace benchutil
PROMEKI_NAMESPACE_END

#else  // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

void registerInspectorCases() {
        // PROAV / CSC disabled at configure time — nothing to register.
}

String inspectorParamHelp() {
        return String(
                "inspector suite parameters: (disabled — built without PROMEKI_ENABLE_CSC)\n");
}

}  // namespace benchutil
PROMEKI_NAMESPACE_END

#endif  // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC
