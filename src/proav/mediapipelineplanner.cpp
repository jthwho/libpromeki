/**
 * @file      mediapipelineplanner.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipelineplanner.h>

#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiotask.h>
#include <promeki/pixelformat.h>
#include <promeki/set.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

#include <climits>

PROMEKI_NAMESPACE_BEGIN

namespace {

        PROMEKI_DEBUG(MediaPipelinePlanner)

        // ----------------------------------------------------------------
        // Stage instantiation helpers
        // ----------------------------------------------------------------
        //
        // The planner needs live MediaIO instances to query describe() /
        // proposeInput() / proposeOutput().  These are temporary — created
        // when plan() starts, destroyed before plan() returns.  We do not
        // share them with the runtime MediaPipeline; the resolved config is
        // the only artefact that escapes.

        MediaIO *makeStage(const MediaPipelineConfig::Stage &s) {
                if (!s.type.isEmpty()) {
                        MediaConfig cfg = s.config;
                        cfg.set(MediaConfig::Type, s.type);
                        if (!s.path.isEmpty()) cfg.set(MediaConfig::Filename, s.path);
                        return MediaIO::create(cfg);
                }
                if (!s.path.isEmpty()) {
                        if (s.mode == MediaIO::Source) {
                                return MediaIO::createForFileRead(s.path);
                        }
                        return MediaIO::createForFileWrite(s.path);
                }
                return nullptr;
        }


        // ----------------------------------------------------------------
        // Topological sort (Kahn) — duplicated locally to avoid coupling
        // the planner to MediaPipeline's instance method.
        // ----------------------------------------------------------------

        Error topologicallySort(const MediaPipelineConfig &config, promeki::List<String> &order) {
                order.clear();
                promeki::Map<String, int>                   inDeg;
                promeki::Map<String, promeki::List<String>> adj;
                const auto                                 &stages = config.stages();
                for (size_t i = 0; i < stages.size(); ++i) {
                        inDeg.insert(stages[i].name, 0);
                        adj.insert(stages[i].name, promeki::List<String>());
                }
                const auto &routes = config.routes();
                for (size_t i = 0; i < routes.size(); ++i) {
                        adj[routes[i].from].pushToBack(routes[i].to);
                        inDeg[routes[i].to] += 1;
                }
                promeki::List<String> ready;
                for (size_t i = 0; i < stages.size(); ++i) {
                        if (inDeg[stages[i].name] == 0) ready.pushToBack(stages[i].name);
                }
                while (!ready.isEmpty()) {
                        const String n = ready.front();
                        ready.remove(static_cast<size_t>(0));
                        order.pushToBack(n);
                        const auto &nbrs = adj[n];
                        for (size_t i = 0; i < nbrs.size(); ++i) {
                                const String &m = nbrs[i];
                                inDeg[m] -= 1;
                                if (inDeg[m] == 0) ready.pushToBack(m);
                        }
                }
                if (order.size() != stages.size()) return Error::Invalid;
                return Error::Ok;
        }

        // ----------------------------------------------------------------
        // Source MediaDesc discovery
        // ----------------------------------------------------------------
        //
        // Walk the fallback chain: live mediaDesc on an already-open stage,
        // then describe().preferredFormat, then expectedDesc, then a brief
        // open / close cycle.  Returns an invalid MediaDesc when none of the
        // strategies produce something usable.

        MediaDesc discoverSourceDesc(MediaIO *io) {
                if (io == nullptr) return MediaDesc();

                // Strategy 1: caller already opened the stage.
                if (io->isOpen() && io->mediaDesc().isValid()) {
                        return io->mediaDesc();
                }

                // Strategy 2: backend describe().
                MediaIODescription d;
                if (io->describe(&d).isOk() && d.preferredFormat().isValid()) {
                        return d.preferredFormat();
                }

                // Strategy 3: caller-provided pre-open hint.
                if (io->expectedDesc().isValid()) {
                        return io->expectedDesc();
                }

                // Strategy 4: open the source briefly to read its mediaDesc.
                // This pays the open cost (and any side effects — RTP socket
                // bind, V4L2 device handle) but is the last-resort path that
                // keeps the planner usable for backends without describe()
                // overrides.
                //
                // Limitation: only the stage's type and path are forwarded
                // (those were baked into the MediaIO at makeStage time).
                // Stage-level metadata and expectedDesc pre-open hints are
                // not re-applied here; backends whose opened mediaDesc
                // depends on those hints should implement describe() so the
                // planner never reaches this path.
                Error err = io->open(MediaIO::Source);
                if (err.isError()) return MediaDesc();
                MediaDesc desc = io->mediaDesc();
                (void)io->close();
                return desc;
        }

        // ----------------------------------------------------------------
        // Output desc propagation
        // ----------------------------------------------------------------
        //
        // After a stage receives an input desc, what does it produce?  For
        // sources the answer comes from discoverSourceDesc.  For transforms
        // and sinks, ask the backend's proposeOutput; sinks usually return
        // NotSupported (they're terminal), in which case the planner uses
        // the input desc unchanged so any downstream consumer of a sink
        // (rare but possible via fan-out chains) sees something.

        MediaDesc propagateThroughStage(MediaIO *io, const MediaDesc &input) {
                if (io == nullptr) return input;
                MediaDesc out;
                if (io->proposeOutput(input, &out).isOk() && out.isValid()) return out;
                return input;
        }

        // ----------------------------------------------------------------
        // Bridge solving — single hop with quality-adjusted cost.
        // ----------------------------------------------------------------

        struct BridgeStep {
                        String      backendName; // e.g. "CSC"
                        MediaConfig config;      // ready-to-instantiate stage config
                        int         cost = 0;    // adjusted cost (after Quality bias)
        };

        // One trace entry per bridge that the solver considered, in
        // registry order.  Captured optionally so plan() can render a
        // rich, self-explanatory diagnostic when it fails — the user
        // reads the trace and sees exactly which bridges declined and
        // (when one accepted) which won.
        struct BridgeTraceEntry {
                        String name;
                        bool   considered = false; // false when excluded by policy
                        bool   accepted = false;
                        int    rawCost = -1;
                        int    adjustedCost = -1;
                        bool   rejectedByQuality = false; // ZeroCopyOnly threshold
        };

        bool findSingleBridge(const MediaDesc &from, const MediaDesc &to, const MediaPipelinePlanner::Policy &policy,
                              BridgeStep *out, promeki::List<BridgeTraceEntry> *trace = nullptr) {
                const auto &formats = MediaIO::registeredFormats();
                int         bestCost = INT_MAX;
                bool        found = false;
                for (const auto &fd : formats) {
                        if (!fd.bridge) continue;

                        BridgeTraceEntry entry;
                        entry.name = fd.name;

                        if (policy.excludedBridges.contains(fd.name)) {
                                entry.considered = false;
                                if (trace != nullptr) trace->pushToBack(entry);
                                continue;
                        }
                        entry.considered = true;

                        MediaConfig cfg;
                        int         rawCost = -1;
                        if (!fd.bridge(from, to, &cfg, &rawCost)) {
                                entry.accepted = false;
                                if (trace != nullptr) trace->pushToBack(entry);
                                continue;
                        }
                        entry.rawCost = rawCost;

                        const int cost = MediaPipelinePlanner::adjustCostForQuality(rawCost, policy.quality);
                        entry.adjustedCost = cost;

                        // ZeroCopyOnly: hard reject anything beyond the
                        // metadata / lossless-conversion bands.
                        if (policy.quality == MediaPipelinePlanner::Quality::ZeroCopyOnly && rawCost > 100) {
                                entry.rejectedByQuality = true;
                                if (trace != nullptr) trace->pushToBack(entry);
                                continue;
                        }

                        entry.accepted = true;
                        if (trace != nullptr) trace->pushToBack(entry);

                        if (cost < bestCost) {
                                bestCost = cost;
                                if (out != nullptr) {
                                        out->backendName = fd.name;
                                        out->config = cfg;
                                        out->cost = cost;
                                }
                                found = true;
                        }
                }
                return found;
        }

        // Codec-transitive two-hop: VideoDecoder → VideoEncoder for
        // compressed → compressed transitions.  Synthesises an intermediate
        // uncompressed MediaDesc compatible with both ends.
        bool findCodecTransitive(const MediaDesc &from, const MediaDesc &to, const MediaPipelinePlanner::Policy &policy,
                                 promeki::List<BridgeStep> *out) {
                if (from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
                const PixelFormat &fromPd = from.imageList()[0].pixelFormat();
                const PixelFormat &toPd = to.imageList()[0].pixelFormat();
                if (!fromPd.isCompressed() || !toPd.isCompressed()) return false;
                if (policy.excludedBridges.contains("VideoDecoder")) return false;
                if (policy.excludedBridges.contains("VideoEncoder")) return false;

                // Construct a plausible intermediate: same raster + frame rate
                // as `to`, NV12 4:2:0 8-bit Rec.709 — the lingua franca that
                // every decoder can output and every encoder accepts in
                // practice.  Refine when we encounter a codec that needs
                // something else.
                MediaDesc intermediate = to;
                if (!intermediate.imageList().isEmpty()) {
                        ImageDesc &img = intermediate.imageList()[0];
                        img.setPixelFormat(PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
                }

                BridgeStep dec, enc;
                if (!findSingleBridge(from, intermediate, policy, &dec)) return false;
                if (dec.backendName != "VideoDecoder") return false;
                if (!findSingleBridge(intermediate, to, policy, &enc)) return false;
                if (enc.backendName != "VideoEncoder") return false;

                if (out != nullptr) {
                        out->pushToBack(dec);
                        out->pushToBack(enc);
                }
                return true;
        }

        // ----------------------------------------------------------------
        // Diagnostic formatting
        // ----------------------------------------------------------------
        //
        // Renders one MediaDesc into a single human-readable line.  Mirrors
        // MediaIODescription::summary's helper but is duplicated locally so
        // the planner doesn't have to depend on a private detail of the
        // description type.
        String shortMediaDescLine(const MediaDesc &desc) {
                if (!desc.isValid()) return String("<invalid>");
                String line;
                if (desc.frameRate().isValid()) {
                        line += desc.frameRate().toString();
                        line += "  ";
                }
                for (size_t i = 0; i < desc.imageList().size(); ++i) {
                        if (i) line += " | ";
                        line += desc.imageList()[i].toString();
                }
                if (!desc.audioList().isEmpty()) {
                        if (!line.isEmpty()) line += "  +  ";
                        for (size_t i = 0; i < desc.audioList().size(); ++i) {
                                if (i) line += " | ";
                                line += desc.audioList()[i].toString();
                        }
                }
                if (line.isEmpty()) line = "<empty>";
                return line;
        }

        void appendDiagnostic(String *diagnostic, const String &line) {
                if (diagnostic == nullptr) return;
                if (!diagnostic->isEmpty()) *diagnostic += "\n";
                *diagnostic += line;
        }

        void appendBridgeTrace(String *diagnostic, const promeki::List<BridgeTraceEntry> &trace) {
                if (diagnostic == nullptr) return;
                if (trace.isEmpty()) {
                        appendDiagnostic(diagnostic, "    bridges considered: (none registered)");
                        return;
                }
                appendDiagnostic(diagnostic, "    bridges considered:");
                constexpr size_t kNameWidth = 14;
                for (size_t i = 0; i < trace.size(); ++i) {
                        const BridgeTraceEntry &e = trace[i];
                        String                  line = "      ";
                        line += e.name;
                        if (e.name.size() < kNameWidth) {
                                line += String(kNameWidth - e.name.size(), ' ');
                        }
                        if (!e.considered) {
                                line += " excluded by policy";
                        } else if (e.accepted) {
                                line += " ACCEPTED  rawCost=";
                                line += String::number(e.rawCost);
                                if (e.adjustedCost != e.rawCost) {
                                        line += "  adjusted=";
                                        line += String::number(e.adjustedCost);
                                }
                        } else if (e.rejectedByQuality) {
                                line += " rejected by ZeroCopyOnly  rawCost=";
                                line += String::number(e.rawCost);
                        } else {
                                line += " declined";
                        }
                        appendDiagnostic(diagnostic, line);
                }
        }

        // ----------------------------------------------------------------
        // Per-route resolution
        // ----------------------------------------------------------------

        Error resolveRoute(const MediaPipelineConfig::Route &route, MediaIO *fromStage, MediaIO *toStage,
                           const MediaDesc &producedDesc, const MediaPipelinePlanner::Policy &policy,
                           MediaPipelineConfig *out, int *bridgeCounter, MediaDesc *postDesc, String *diagnostic) {
                if (toStage == nullptr) {
                        appendDiagnostic(diagnostic,
                                         String("MediaPipelinePlanner: stage '") + route.to + "' is missing.");
                        return Error::Invalid;
                }

                // Ask the sink what it wants given the upstream desc.
                // Compare structurally (ignoring metadata) so a sink that
                // tags its preferred desc with, say, a colour-space marker
                // isn't forced into a pointless bridge when the upstream
                // produces an otherwise identical shape.
                MediaDesc preferred;
                Error     pe = toStage->proposeInput(producedDesc, &preferred);
                if (pe == Error::Ok && preferred.formatEquals(producedDesc)) {
                        // Direct route.
                        out->addRoute(route);
                        if (postDesc != nullptr) {
                                *postDesc = propagateThroughStage(toStage, producedDesc);
                        }
                        return Error::Ok;
                }

                // If the sink reported NotSupported it has no opinion about
                // the offered shape — fall back to a same-as-offered target
                // so the bridge solver gets a concrete goal.  Real sinks that
                // care return Ok with a populated `preferred`; pure passthroughs
                // return Ok with preferred==offered (already handled above).
                MediaDesc target = (pe == Error::Ok && preferred.isValid()) ? preferred : producedDesc;

                promeki::List<BridgeStep>       chain;
                promeki::List<BridgeTraceEntry> trace;
                BridgeStep                      singleAttempt;
                bool single = findSingleBridge(producedDesc, target, policy, &singleAttempt, &trace);
                if (single) {
                        chain.pushToBack(singleAttempt);
                } else if (policy.maxBridgeDepth >= 2 && findCodecTransitive(producedDesc, target, policy, &chain)) {
                        // Two-hop succeeded after the single-hop search filled
                        // the trace; fall through to splice.
                } else {
                        appendDiagnostic(diagnostic, String("MediaPipelinePlanner: route '") + route.from + "' -> '" +
                                                             route.to + "' has a format gap and no bridge chain fits.");
                        appendDiagnostic(diagnostic,
                                         String("    upstream produced: ") + shortMediaDescLine(producedDesc));
                        appendDiagnostic(diagnostic, String("    sink preferred:    ") + shortMediaDescLine(target));
                        appendBridgeTrace(diagnostic, trace);
                        if (producedDesc.imageList().isEmpty() || target.imageList().isEmpty()) {
                                appendDiagnostic(diagnostic, "    codec-transitive (Decoder+Encoder): not applicable "
                                                             "(missing image desc).");
                        } else {
                                const PixelFormat &fromPd = producedDesc.imageList()[0].pixelFormat();
                                const PixelFormat &toPd = target.imageList()[0].pixelFormat();
                                if (!fromPd.isCompressed() || !toPd.isCompressed()) {
                                        appendDiagnostic(diagnostic,
                                                         "    codec-transitive (Decoder+Encoder): not applicable "
                                                         "(both ends must be compressed).");
                                } else {
                                        appendDiagnostic(diagnostic,
                                                         "    codec-transitive (Decoder+Encoder): tried, no "
                                                         "decoder/encoder pair satisfied the gap.");
                                }

                                // Flag the "simultaneous rate + pixel gap"
                                // case explicitly — CSC and FrameSync each
                                // decline when the @em other axis differs, so
                                // a single-hop solve is impossible.  Tell the
                                // caller they need to author an explicit
                                // intermediate rather than expect the planner
                                // to invent one.
                                const bool rateDiffers = producedDesc.frameRate() != target.frameRate();
                                const bool pixelDiffers = fromPd != toPd;
                                if (rateDiffers && pixelDiffers && !fromPd.isCompressed() && !toPd.isCompressed()) {
                                        appendDiagnostic(diagnostic,
                                                         "    hint: both frame rate and pixel format differ. "
                                                         "CSC handles pixel-only gaps; FrameSync handles "
                                                         "rate-only gaps.  Insert an explicit CSC or FrameSync "
                                                         "stage so each bridge sees only one axis of change.");
                                }
                        }
                        return Error::NotSupported;
                }

                if (static_cast<int>(chain.size()) > policy.maxBridgeDepth) {
                        appendDiagnostic(diagnostic, String("MediaPipelinePlanner: bridge chain for '") + route.from +
                                                             "' -> '" + route.to + "' exceeds maxBridgeDepth (" +
                                                             String::number(chain.size()) + " > " +
                                                             String::number(policy.maxBridgeDepth) + ").");
                        return Error::NotSupported;
                }

                // Splice each step in.
                String prevName = route.from;
                for (size_t i = 0; i < chain.size(); ++i) {
                        MediaPipelineConfig::Stage bridgeStage;
                        bridgeStage.name = "br" + String::number(*bridgeCounter) + "_" + route.from + "_" + route.to;
                        bridgeStage.type = chain[i].backendName;
                        bridgeStage.mode = MediaIO::Transform;
                        bridgeStage.config = chain[i].config;
                        out->addStage(bridgeStage);

                        MediaPipelineConfig::Route hopRoute;
                        hopRoute.from = prevName;
                        hopRoute.to = bridgeStage.name;
                        out->addRoute(hopRoute);

                        prevName = bridgeStage.name;
                        (*bridgeCounter)++;
                }
                MediaPipelineConfig::Route lastRoute;
                lastRoute.from = prevName;
                lastRoute.to = route.to;
                if (!route.fromTrack.isEmpty()) lastRoute.fromTrack = route.fromTrack;
                if (!route.toTrack.isEmpty()) lastRoute.toTrack = route.toTrack;
                out->addRoute(lastRoute);

                if (postDesc != nullptr) {
                        *postDesc = propagateThroughStage(toStage, target);
                }
                (void)fromStage; // currently unused; reserved for proposeOutput-driven
                                 // source negotiation in a later phase.
                return Error::Ok;
        }

} // namespace

// ============================================================================
// Public API
// ============================================================================

int MediaPipelinePlanner::adjustCostForQuality(int rawCost, Quality quality) {
        switch (quality) {
                case Quality::Highest:
                case Quality::ZeroCopyOnly: return rawCost;
                case Quality::Balanced:
                        // Penalise heavy bridges (cost > 1000) by 25 %.
                        return rawCost > 1000 ? rawCost + (rawCost / 4) : rawCost;
                case Quality::Fastest:
                        // Penalise heavy bridges (cost > 1000) by 200 %, and
                        // the bounded-error band by 50 %, so the cheapest CPU
                        // path wins even at some quality cost.
                        if (rawCost > 1000) return rawCost * 3;
                        if (rawCost > 100) return rawCost + (rawCost / 2);
                        return rawCost;
        }
        return rawCost;
}

namespace {

        // Builds the per-stage MediaIO query map.  Injected stages (caller-
        // owned) come from @p injected; everything else is freshly built
        // via the registry.  Returns the set of names whose MediaIO this
        // helper allocated — those are the ones destroyStages owns and
        // must delete.  Returns false if any non-injected stage failed to
        // instantiate.
        bool buildStageMap(const MediaPipelineConfig &config, const MediaPipelinePlanner::InjectedStages &injected,
                           promeki::Map<String, MediaIO *> *stages, promeki::Set<String> *ownedNames,
                           String *failedName) {
                for (const auto &s : config.stages()) {
                        auto injIt = injected.find(s.name);
                        if (injIt != injected.end() && injIt->second != nullptr) {
                                // Caller-built stage — observe only.
                                stages->insert(s.name, injIt->second);
                                continue;
                        }
                        MediaIO *io = makeStage(s);
                        if (io == nullptr) {
                                if (failedName != nullptr) *failedName = s.name;
                                return false;
                        }
                        stages->insert(s.name, io);
                        ownedNames->insert(s.name);
                }
                return true;
        }

        void destroyOwnedStages(promeki::Map<String, MediaIO *> &stages, const promeki::Set<String> &ownedNames) {
                for (auto it = stages.begin(); it != stages.end(); ++it) {
                        if (!ownedNames.contains(it->first)) continue;
                        MediaIO *io = it->second;
                        if (io == nullptr) continue;
                        if (io->isOpen()) (void)io->close();
                        delete io;
                }
                stages.clear();
        }

} // namespace

bool MediaPipelinePlanner::isResolved(const MediaPipelineConfig &config, String *diagnostic) {
        // Build temporary stages, walk routes, return false on the
        // first gap.  Mirrors plan() but never inserts bridges.
        // No injected stages here — callers that need to check a
        // pipeline with injected stages should call plan() directly
        // and ignore the resolved-config output.
        promeki::Map<String, MediaIO *> stages;
        promeki::Set<String>            ownedNames;
        String                          failedName;
        if (!buildStageMap(config, InjectedStages(), &stages, &ownedNames, &failedName)) {
                destroyOwnedStages(stages, ownedNames);
                if (diagnostic != nullptr) {
                        *diagnostic = String("MediaPipelinePlanner: cannot instantiate '") + failedName + "'.";
                }
                return false;
        }

        // Topological order so we discover source descs before
        // walking their downstream routes.
        promeki::List<String> order;
        if (topologicallySort(config, order).isError()) {
                destroyOwnedStages(stages, ownedNames);
                if (diagnostic != nullptr) {
                        *diagnostic = "MediaPipelinePlanner: cycle in route graph.";
                }
                return false;
        }

        promeki::Set<String> hasUpstream;
        for (const auto &r : config.routes()) hasUpstream.insert(r.to);

        promeki::Map<String, MediaDesc> producedBy;
        for (size_t i = 0; i < order.size(); ++i) {
                const String &name = order[i];
                if (!hasUpstream.contains(name)) {
                        producedBy.insert(name, discoverSourceDesc(stages[name]));
                }
        }

        bool resolved = true;
        for (const auto &r : config.routes()) {
                MediaIO        *to = stages[r.to];
                const MediaDesc fromDesc = producedBy.contains(r.from) ? producedBy[r.from] : MediaDesc();
                MediaDesc       preferred;
                Error           pe = to->proposeInput(fromDesc, &preferred);
                if (pe.isError() || !preferred.formatEquals(fromDesc)) {
                        if (diagnostic != nullptr) {
                                *diagnostic = String("MediaPipelinePlanner: route '") + r.from + "' -> '" + r.to +
                                              "' is not directly resolvable.";
                        }
                        resolved = false;
                        break;
                }
                producedBy.insert(r.to, propagateThroughStage(to, fromDesc));
        }

        destroyOwnedStages(stages, ownedNames);
        return resolved;
}

Error MediaPipelinePlanner::plan(const MediaPipelineConfig &in, MediaPipelineConfig *out, const Policy &policy,
                                 String *diagnostic) {
        return plan(in, out, InjectedStages(), policy, diagnostic);
}

Error MediaPipelinePlanner::plan(const MediaPipelineConfig &in, MediaPipelineConfig *out,
                                 const InjectedStages &injected, const Policy &policy, String *diagnostic) {
        if (out == nullptr) return Error::Invalid;
        *out = MediaPipelineConfig();

        // 1. Validate input.
        Error vErr = in.validate();
        if (vErr.isError()) {
                if (diagnostic != nullptr) {
                        *diagnostic = String("MediaPipelinePlanner: input config invalid (") + vErr.name() + ").";
                }
                return vErr;
        }

        // 2. Topologically sort.
        promeki::List<String> order;
        if (topologicallySort(in, order).isError()) {
                if (diagnostic != nullptr) {
                        *diagnostic = "MediaPipelinePlanner: cycle in route graph.";
                }
                return Error::Invalid;
        }

        // 3. Instantiate / adopt stages so we can query them.
        // Injected stages keep their caller-owned identity; the
        // planner never closes or deletes them.
        promeki::Map<String, MediaIO *> stages;
        promeki::Set<String>            ownedNames;
        String                          failedName;
        if (!buildStageMap(in, injected, &stages, &ownedNames, &failedName)) {
                if (diagnostic != nullptr) {
                        *diagnostic = String("MediaPipelinePlanner: cannot instantiate '") + failedName + "'.";
                }
                destroyOwnedStages(stages, ownedNames);
                return Error::Invalid;
        }

        // 4. Discover source MediaDescs (stages with no upstream).
        promeki::Set<String> hasUpstream;
        for (const auto &r : in.routes()) hasUpstream.insert(r.to);

        promeki::Map<String, MediaDesc> producedBy;
        for (size_t i = 0; i < order.size(); ++i) {
                const String &name = order[i];
                if (hasUpstream.contains(name)) continue;
                MediaDesc src = discoverSourceDesc(stages[name]);
                if (!src.isValid()) {
                        if (diagnostic != nullptr) {
                                *diagnostic = String("MediaPipelinePlanner: cannot determine "
                                                     "produced MediaDesc for source '") +
                                              name + "' (no describe(), expectedDesc(), or open()-able shape).";
                        }
                        destroyOwnedStages(stages, ownedNames);
                        return Error::NotSupported;
                }
                producedBy.insert(name, src);
        }

        // 5. Seed the output with every input stage's metadata and
        // every input stage's declaration.  Routes we re-emit as we
        // walk them so we can splice bridges in at the right point.
        // Pipeline-wide settings (metadata, frame-count cap) are
        // preserved verbatim — the planner only splices bridges; the
        // user's runtime caps should survive planning unchanged.
        out->setPipelineMetadata(in.pipelineMetadata());
        out->setFrameCount(in.frameCount());
        for (const auto &s : in.stages()) out->addStage(s);

        // 6. Walk routes in topological order so produced descs flow
        // forward correctly.
        promeki::Map<String, promeki::List<MediaPipelineConfig::Route>> routesByFrom;
        for (const auto &r : in.routes()) {
                if (!routesByFrom.contains(r.from)) {
                        routesByFrom.insert(r.from, promeki::List<MediaPipelineConfig::Route>());
                }
                routesByFrom[r.from].pushToBack(r);
        }

        int bridgeCounter = 0;
        for (size_t i = 0; i < order.size(); ++i) {
                const String &fromName = order[i];
                if (!routesByFrom.contains(fromName)) continue;
                const auto &routes = routesByFrom[fromName];
                for (size_t j = 0; j < routes.size(); ++j) {
                        const auto     &r = routes[j];
                        const MediaDesc producedDesc =
                                producedBy.contains(fromName) ? producedBy[fromName] : MediaDesc();
                        MediaDesc postDesc;
                        Error     rerr = resolveRoute(r, stages[r.from], stages[r.to], producedDesc, policy, out,
                                                      &bridgeCounter, &postDesc, diagnostic);
                        if (rerr.isError()) {
                                destroyOwnedStages(stages, ownedNames);
                                *out = MediaPipelineConfig();
                                return rerr;
                        }
                        producedBy.insert(r.to, postDesc);
                }
        }

        destroyOwnedStages(stages, ownedNames);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
