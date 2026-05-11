/**
 * @file      mediapipelineplanner.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipelineplanner.h>

#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/pixelformat.h>
#include <promeki/set.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/videocodec.h>

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
                        if (s.role == MediaPipelineConfig::StageRole::Source) {
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

        Error topologicallySort(const MediaPipelineConfig &config, List<String> &order) {
                order.clear();
                Map<String, int>           inDeg;
                Map<String, List<String>>  adj;
                const auto                &stages = config.stages();
                for (size_t i = 0; i < stages.size(); ++i) {
                        inDeg.insert(stages[i].name, 0);
                        adj.insert(stages[i].name, List<String>());
                }
                const auto &routes = config.routes();
                for (size_t i = 0; i < routes.size(); ++i) {
                        adj[routes[i].from].pushToBack(routes[i].to);
                        inDeg[routes[i].to] += 1;
                }
                List<String> ready;
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
                if (io->pendingMediaDesc().isValid()) {
                        return io->pendingMediaDesc();
                }

                // Strategy 4: open the source briefly to read its mediaDesc.
                // This pays the open cost (and any side effects — RTP socket
                // bind, V4L2 device handle) but is the last-resort path that
                // keeps the planner usable for backends without describe()
                // overrides.
                //
                // Limitation: only the stage's type and path are forwarded
                // (those were baked into the MediaIO at makeStage time).
                // Stage-level metadata and pendingMediaDesc pre-open hints are
                // not re-applied here; backends whose opened mediaDesc
                // depends on those hints should implement describe() so the
                // planner never reaches this path.
                Error err = io->open().wait();
                if (err.isError()) return MediaDesc();
                MediaDesc desc = io->mediaDesc();
                io->close().wait();
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
                // Use the task-level forwarder so this works pre-open
                // (the planner queries stages before opening them).
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
                              BridgeStep *out, List<BridgeTraceEntry> *trace = nullptr) {
                const auto &factories = MediaIOFactory::registeredFactories();
                int         bestCost = INT_MAX;
                bool        found = false;
                for (const MediaIOFactory *fd : factories) {
                        if (fd == nullptr) continue;
                        const String fdName = fd->name();

                        BridgeTraceEntry entry;
                        entry.name = fdName;

                        if (policy.excludedBridges.contains(fdName)) {
                                entry.considered = false;
                                if (trace != nullptr) trace->pushToBack(entry);
                                continue;
                        }
                        entry.considered = true;

                        MediaConfig cfg;
                        int         rawCost = -1;
                        if (!fd->bridge(from, to, &cfg, &rawCost)) {
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
                                        out->backendName = fdName;
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
                                 List<BridgeStep> *out) {
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

        // CSC + VideoEncoder two-hop: uncompressed → compressed transitions
        // where the source PixelFormat isn't directly accepted by any
        // registered encoder backend for the target codec.  Defers to the
        // encoder's own @ref MediaIO::proposeInput to pick the intermediate
        // — that path replicates the encoder's chroma-subsampling and bit-
        // depth preference (NV12 by default), so the chosen CSC target
        // matches what the runtime VideoEncoder would have requested if the
        // planner had asked.  The audio axis must already match;
        // orthogonal video + audio gaps are layered on top by
        // @ref findOrthogonalChain.
        bool findVideoCscEncoderChain(const MediaDesc &from, const MediaDesc &to,
                                      const MediaPipelinePlanner::Policy &policy, List<BridgeStep> *out) {
                if (from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
                const PixelFormat &fromPd = from.imageList()[0].pixelFormat();
                const PixelFormat &toPd = to.imageList()[0].pixelFormat();
                if (!fromPd.isValid() || !toPd.isValid()) return false;
                if (fromPd.isCompressed()) return false;
                if (!toPd.isCompressed()) return false;
                if (policy.excludedBridges.contains("CSC")) return false;
                if (policy.excludedBridges.contains("VideoEncoder")) return false;

                // Audio must already match: this two-hop only addresses the
                // image axis.  Orthogonal video + audio gaps go through
                // @ref findOrthogonalChain, which calls this helper for the
                // video leg of its split.
                if (from.audioList().size() != to.audioList().size()) return false;
                const size_t audioCount = from.audioList().size();
                for (size_t i = 0; i < audioCount; ++i) {
                        const AudioDesc &a = from.audioList()[i];
                        const AudioDesc &b = to.audioList()[i];
                        if (a.sampleRate() != b.sampleRate()) return false;
                        if (a.channels() != b.channels()) return false;
                        if (a.format().id() != b.format().id()) return false;
                }

                const VideoCodec codec = VideoCodec::fromPixelFormat(toPd);
                if (!codec.isValid() || !codec.canEncode()) return false;

                // Build a transient VideoEncoder MediaIO so we can ask it
                // what input format it actually wants.  proposeInput knows
                // the encoder's chroma + bit-depth preference (driven by
                // VideoChromaSubsampling, default YUV420) and only ever
                // returns a format the encoder backends advertise.
                MediaIO::Config encConfig = MediaIOFactory::defaultConfig("VideoEncoder");
                encConfig.set(MediaConfig::Type, String("VideoEncoder"));
                encConfig.set(MediaConfig::VideoCodec, codec);
                MediaIO *encoderProbe = MediaIO::create(encConfig);
                if (encoderProbe == nullptr) return false;
                MediaDesc preferred;
                Error     pe = encoderProbe->proposeInput(from, &preferred);
                delete encoderProbe;
                if (pe.isError() || preferred.imageList().isEmpty()) return false;
                const PixelFormat &preferredPf = preferred.imageList()[0].pixelFormat();
                if (!preferredPf.isValid() || preferredPf.isCompressed()) return false;
                // No-op intermediate would mean the encoder accepts the
                // source directly — the single-bridge path would have
                // handled it.  Defensive: bail rather than emit a useless
                // CSC step.
                if (preferredPf == fromPd) return false;

                MediaDesc intermediate = from;
                for (size_t i = 0; i < intermediate.imageList().size(); ++i) {
                        intermediate.imageList()[i].setPixelFormat(preferredPf);
                }
                BridgeStep cscStep, encStep;
                if (!findSingleBridge(from, intermediate, policy, &cscStep)) return false;
                if (cscStep.backendName != "CSC") return false;
                if (!findSingleBridge(intermediate, to, policy, &encStep)) return false;
                if (encStep.backendName != "VideoEncoder") return false;

                if (out != nullptr) {
                        out->pushToBack(cscStep);
                        out->pushToBack(encStep);
                }
                return true;
        }

        // Audio codec-transitive two-hop: AudioDecoder → AudioEncoder for
        // compressed → compressed audio transitions.  Mirrors the video
        // path above with the lingua-franca intermediate being interleaved
        // 32-bit float PCM in the platform's native byte order — every
        // registered audio decoder either emits this directly or can
        // convert internally, and every encoder accepts it.
        bool findAudioCodecTransitive(const MediaDesc &from, const MediaDesc &to,
                                      const MediaPipelinePlanner::Policy &policy, List<BridgeStep> *out) {
                if (from.audioList().isEmpty() || to.audioList().isEmpty()) return false;
                const AudioFormat &fromFmt = from.audioList()[0].format();
                const AudioFormat &toFmt = to.audioList()[0].format();
                if (!fromFmt.isCompressed() || !toFmt.isCompressed()) return false;
                if (policy.excludedBridges.contains("AudioDecoder")) return false;
                if (policy.excludedBridges.contains("AudioEncoder")) return false;

                MediaDesc        intermediate = to;
                AudioDesc::List &auds = intermediate.audioList();
                for (size_t i = 0; i < auds.size(); ++i) {
                        auds[i].setFormat(AudioFormat(AudioFormat::NativeFloat));
                }

                BridgeStep dec, enc;
                if (!findSingleBridge(from, intermediate, policy, &dec)) return false;
                if (dec.backendName != "AudioDecoder") return false;
                if (!findSingleBridge(intermediate, to, policy, &enc)) return false;
                if (enc.backendName != "AudioEncoder") return false;

                if (out != nullptr) {
                        out->pushToBack(dec);
                        out->pushToBack(enc);
                }
                return true;
        }

        // Orthogonal-axes two-hop: splice a video bridge and an audio
        // bridge in sequence when both the image and audio sides differ
        // and no single bridge can solve them together.  Every bridge
        // factory (CSC, SRC, VideoEncoder, AudioEncoder, ...) is strict
        // about the @em other axis matching, so a route like
        // @c TPG{RGB,PCM} → @c Rtmp{H264,AAC} needs two bridges: one
        // VideoEncoder (changes image, leaves audio) and one
        // AudioEncoder (changes audio, leaves image).  This helper
        // tries video-first; if that fails it tries audio-first.
        bool findOrthogonalChain(const MediaDesc &from, const MediaDesc &to,
                                 const MediaPipelinePlanner::Policy &policy, List<BridgeStep> *out) {
                if (from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
                if (from.audioList().isEmpty() || to.audioList().isEmpty()) return false;

                // Both axes must actually differ — single-bridge already
                // handles single-axis gaps and we don't want to add a
                // pointless second hop.
                const bool imageDiffers =
                        from.imageList()[0].pixelFormat() != to.imageList()[0].pixelFormat();
                const bool audioDiffers =
                        from.audioList()[0].format().id() != to.audioList()[0].format().id();
                if (!imageDiffers || !audioDiffers) return false;

                // Helper: resolve the video leg of an orthogonal split.
                // Tries a single bridge first (CSC for uncompressed→uncompressed,
                // VideoEncoder for uncompressed→compressed when the source
                // is on the encoder's supported-input list); falls back to a
                // CSC + VideoEncoder two-hop when the source needs a colour-
                // space conversion before the encoder can consume it (e.g.
                // TPG's default RGB into NVENC, which only accepts YUV).
                auto findVideoLeg = [&](const MediaDesc &legFrom, const MediaDesc &legTo,
                                        List<BridgeStep> *legOut) -> bool {
                        BridgeStep single;
                        if (findSingleBridge(legFrom, legTo, policy, &single)) {
                                if (legOut != nullptr) legOut->pushToBack(single);
                                return true;
                        }
                        return findVideoCscEncoderChain(legFrom, legTo, policy, legOut);
                };

                // Try video-first: from → {to.image, from.audio} → to.
                {
                        MediaDesc intermediate = from;
                        intermediate.imageList() = to.imageList();
                        List<BridgeStep> videoChain;
                        BridgeStep       audioStep;
                        if (findVideoLeg(from, intermediate, &videoChain) &&
                            findSingleBridge(intermediate, to, policy, &audioStep)) {
                                if (out != nullptr) {
                                        for (size_t i = 0; i < videoChain.size(); ++i) {
                                                out->pushToBack(videoChain[i]);
                                        }
                                        out->pushToBack(audioStep);
                                }
                                return true;
                        }
                }

                // Try audio-first: from → {from.image, to.audio} → to.
                {
                        MediaDesc intermediate = from;
                        intermediate.audioList() = to.audioList();
                        BridgeStep       audioStep;
                        List<BridgeStep> videoChain;
                        if (findSingleBridge(from, intermediate, policy, &audioStep) &&
                            findVideoLeg(intermediate, to, &videoChain)) {
                                if (out != nullptr) {
                                        out->pushToBack(audioStep);
                                        for (size_t i = 0; i < videoChain.size(); ++i) {
                                                out->pushToBack(videoChain[i]);
                                        }
                                }
                                return true;
                        }
                }

                return false;
        }

        // ----------------------------------------------------------------
        // Head-bridge peeling via source-side renegotiation
        // ----------------------------------------------------------------
        //
        // Complements the route-level renegotiation in @ref resolveRoute,
        // which only asks the source whether it can produce the @em final
        // sink target.  When the chain solver picks a multi-hop sequence
        // whose leading step is a colour-space / sample-rate / scan
        // conversion the source could perform internally, the route-level
        // pass misses it: the final target is compressed (or otherwise
        // beyond the source's reach) so the source declines.
        //
        // This pass walks the chain from the head, instantiates each step
        // as a transient @ref MediaIO, queries its
        // @ref MediaIO::proposeOutput on the @em current produced desc to
        // learn the shape the step would emit, then asks the source the
        // same question.  When the source accepts that shape with a
        // non-empty @c configDelta, the delta is merged onto the source
        // stage's emitted config and the step is dropped.  Stops on the
        // first step the source cannot absorb.
        //
        // TPG → NVENC is the canonical case the route-level pass misses:
        // the chain solver inserts a CSC to convert TPG's default RGB
        // into the encoder's preferred NV12, but TPG can emit NV12
        // natively when its @c VideoPixelFormat key is set — so peeling
        // the head CSC removes a runtime stage that does no real work.
        //
        // Returns the descriptor the source actually produces after any
        // renegotiation (== @p producedDesc when nothing was peeled).
        MediaDesc peelHeadBridges(List<BridgeStep> &chain, MediaIO *fromStage, const String &fromStageName,
                                  const MediaDesc &producedDesc, MediaPipelineConfig *out) {
                MediaDesc currentDesc = producedDesc;
                if (fromStage == nullptr || out == nullptr) return currentDesc;
                while (!chain.isEmpty()) {
                        const BridgeStep &head = chain.front();
                        if (head.backendName.isEmpty()) break;
                        MediaIO::Config cfg = head.config;
                        cfg.set(MediaConfig::Type, head.backendName);
                        MediaIO *headIO = MediaIO::create(cfg);
                        if (headIO == nullptr) break;
                        MediaDesc headOut;
                        Error     po = headIO->proposeOutput(currentDesc, &headOut);
                        delete headIO;
                        if (po.isError() || !headOut.isValid()) break;
                        // Bridge that produces no shape change has nothing
                        // to renegotiate against; bail out so we don't strip
                        // a stage that's there for non-format reasons (e.g.
                        // queue depth).
                        if (headOut.formatEquals(currentDesc)) break;

                        MediaDesc   achievable;
                        MediaConfig delta;
                        Error       oe = fromStage->proposeOutput(headOut, &achievable, &delta);
                        if (oe.isError() || !achievable.isValid() || !achievable.formatEquals(headOut) ||
                            delta.isEmpty()) {
                                break;
                        }

                        // Source can absorb this step.  Merge the delta
                        // into the source stage's emitted config and drop
                        // the head bridge.
                        MediaPipelineConfig::StageList &stages = out->stages();
                        for (size_t si = 0; si < stages.size(); ++si) {
                                if (stages[si].name == fromStageName) {
                                        stages[si].config.merge(delta);
                                        break;
                                }
                        }
                        chain.remove(static_cast<size_t>(0));
                        currentDesc = achievable;
                }
                return currentDesc;
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

        void appendBridgeTrace(String *diagnostic, const List<BridgeTraceEntry> &trace) {
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
                           MediaPipelineConfig *out, int *bridgeCounter, MediaDesc *postDesc,
                           MediaDesc *renegotiatedFromDesc, String *diagnostic) {
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
                // Use the task-level forwarder so this works pre-open
                // — sinks only exist on a MediaIO once it has been
                // opened, but the planner queries before opening.
                Error pe = toStage->proposeInput(producedDesc, &preferred);
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

                // Source-side renegotiation.  Before splicing in a bridge,
                // ask the upstream stage whether it can produce @p target
                // directly via a config-key change.  Backends that can
                // (TPG, generators, format-flexible readers) populate
                // @c proposeOutput's @c configDelta with the
                // @ref MediaConfig keys that drive the new shape; the
                // planner merges those into the source stage's config in
                // @p out and skips the bridge.  Backends with no such
                // lever leave the delta empty, which we take as a hard
                // signal to fall through to the bridge solver.
                if (fromStage != nullptr && !target.formatEquals(producedDesc)) {
                        MediaDesc   achievable;
                        MediaConfig delta;
                        Error       oe = fromStage->proposeOutput(target, &achievable, &delta);
                        if (oe == Error::Ok && achievable.isValid() && achievable.formatEquals(target) &&
                            !delta.isEmpty()) {
                                MediaPipelineConfig::StageList &stages = out->stages();
                                for (size_t si = 0; si < stages.size(); ++si) {
                                        if (stages[si].name == route.from) {
                                                stages[si].config.merge(delta);
                                                break;
                                        }
                                }
                                out->addRoute(route);
                                if (renegotiatedFromDesc != nullptr) *renegotiatedFromDesc = achievable;
                                if (postDesc != nullptr) *postDesc = propagateThroughStage(toStage, achievable);
                                return Error::Ok;
                        }
                }

                List<BridgeStep>       chain;
                List<BridgeTraceEntry> trace;
                BridgeStep                      singleAttempt;
                bool single = findSingleBridge(producedDesc, target, policy, &singleAttempt, &trace);
                if (single) {
                        chain.pushToBack(singleAttempt);
                } else if (policy.maxBridgeDepth >= 2 && findCodecTransitive(producedDesc, target, policy, &chain)) {
                        // Two-hop succeeded after the single-hop search filled
                        // the trace; fall through to splice.
                } else if (policy.maxBridgeDepth >= 2 &&
                           findVideoCscEncoderChain(producedDesc, target, policy, &chain)) {
                        // CSC + VideoEncoder two-hop: the encoder doesn't
                        // accept the source PixelFormat directly, so splice
                        // a colour-space conversion in front of it.
                } else if (policy.maxBridgeDepth >= 2 &&
                           findAudioCodecTransitive(producedDesc, target, policy, &chain)) {
                        // Audio decoder → encoder two-hop succeeded; splice.
                } else if (policy.maxBridgeDepth >= 2 &&
                           findOrthogonalChain(producedDesc, target, policy, &chain)) {
                        // Independent image + audio gaps; splice video and
                        // audio bridges in sequence.
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
                        if (!producedDesc.audioList().isEmpty() && !target.audioList().isEmpty()) {
                                const AudioFormat &fromAf = producedDesc.audioList()[0].format();
                                const AudioFormat &toAf = target.audioList()[0].format();
                                if (fromAf.isCompressed() && toAf.isCompressed()) {
                                        appendDiagnostic(diagnostic,
                                                         "    audio codec-transitive (AudioDecoder+AudioEncoder): "
                                                         "tried, no decoder/encoder pair satisfied the gap.");
                                }
                        }
                        return Error::NotSupported;
                }

                // Before the depth check, try to shrink the chain by
                // peeling head steps the source can absorb via a config
                // delta.  The chain solver picked the cheapest sequence
                // assuming the source emits its current shape verbatim;
                // sources that can reconfigure (TPG, generators, format-
                // flexible readers) can render a leading CSC redundant.
                // Peeling first also lets a chain that would otherwise
                // exceed @c maxBridgeDepth slip under the limit when the
                // depth was inflated by an avoidable head step.
                MediaDesc producedAfterPeel = peelHeadBridges(chain, fromStage, route.from, producedDesc, out);
                if (producedAfterPeel.isValid() && !producedAfterPeel.formatEquals(producedDesc) &&
                    renegotiatedFromDesc != nullptr) {
                        *renegotiatedFromDesc = producedAfterPeel;
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
                        bridgeStage.role = MediaPipelineConfig::StageRole::Transform;
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
                (void)fromStage;
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
                           Map<String, MediaIO *> *stages, Set<String> *ownedNames,
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

        void destroyOwnedStages(Map<String, MediaIO *> &stages, const Set<String> &ownedNames) {
                for (auto it = stages.begin(); it != stages.end(); ++it) {
                        if (!ownedNames.contains(it->first)) continue;
                        MediaIO *io = it->second;
                        if (io == nullptr) continue;
                        if (io->isOpen()) io->close().wait();
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
        Map<String, MediaIO *> stages;
        Set<String>            ownedNames;
        String                 failedName;
        if (!buildStageMap(config, InjectedStages(), &stages, &ownedNames, &failedName)) {
                destroyOwnedStages(stages, ownedNames);
                if (diagnostic != nullptr) {
                        *diagnostic = String("MediaPipelinePlanner: cannot instantiate '") + failedName + "'.";
                }
                return false;
        }

        // Topological order so we discover source descs before
        // walking their downstream routes.
        List<String> order;
        if (topologicallySort(config, order).isError()) {
                destroyOwnedStages(stages, ownedNames);
                if (diagnostic != nullptr) {
                        *diagnostic = "MediaPipelinePlanner: cycle in route graph.";
                }
                return false;
        }

        Set<String> hasUpstream;
        for (const auto &r : config.routes()) hasUpstream.insert(r.to);

        Map<String, MediaDesc> producedBy;
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
                MediaDesc preferred;
                // Pre-open task-level forwarder; planner runs before
                // any stage's sinks have been instantiated.
                Error pe = to != nullptr ? to->proposeInput(fromDesc, &preferred) : Error::Invalid;
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
        List<String> order;
        if (topologicallySort(in, order).isError()) {
                if (diagnostic != nullptr) {
                        *diagnostic = "MediaPipelinePlanner: cycle in route graph.";
                }
                return Error::Invalid;
        }

        // 3. Instantiate / adopt stages so we can query them.
        // Injected stages keep their caller-owned identity; the
        // planner never closes or deletes them.
        Map<String, MediaIO *> stages;
        Set<String>            ownedNames;
        String                 failedName;
        if (!buildStageMap(in, injected, &stages, &ownedNames, &failedName)) {
                if (diagnostic != nullptr) {
                        *diagnostic = String("MediaPipelinePlanner: cannot instantiate '") + failedName + "'.";
                }
                destroyOwnedStages(stages, ownedNames);
                return Error::Invalid;
        }

        // 4. Discover source MediaDescs (stages with no upstream).
        Set<String> hasUpstream;
        for (const auto &r : in.routes()) hasUpstream.insert(r.to);

        Map<String, MediaDesc> producedBy;
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
        //
        // For stages the planner instantiated itself, overwrite the
        // emitted stage's @c config with the live MediaIO's config so
        // URL-driven backends (Rtmp, Rtp, file paths) surface their
        // resolved RtmpUrl / Filename / scheme-derived keys in the
        // planner output.  The live config is the same value that
        // would land back in the MediaPipeline at run time, so this
        // gives @c --plan an honest view of "what will actually open".
        // Injected stages are left alone — their config belongs to the
        // caller and the planner should not second-guess it.
        out->setPipelineMetadata(in.pipelineMetadata());
        out->setFrameCount(in.frameCount());
        for (const auto &s : in.stages()) {
                MediaPipelineConfig::Stage emit = s;
                if (ownedNames.contains(s.name)) {
                        auto it = stages.find(s.name);
                        if (it != stages.end() && it->second != nullptr) {
                                emit.config = it->second->config();
                                if (!s.type.isEmpty()) emit.config.set(MediaConfig::Type, s.type);
                        }
                }
                out->addStage(emit);
        }

        // 6. Walk routes in topological order so produced descs flow
        // forward correctly.
        Map<String, List<MediaPipelineConfig::Route>> routesByFrom;
        for (const auto &r : in.routes()) {
                if (!routesByFrom.contains(r.from)) {
                        routesByFrom.insert(r.from, List<MediaPipelineConfig::Route>());
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
                        MediaDesc renegotiatedFromDesc;
                        Error     rerr = resolveRoute(r, stages[r.from], stages[r.to], producedDesc, policy, out,
                                                      &bridgeCounter, &postDesc, &renegotiatedFromDesc, diagnostic);
                        if (rerr.isError()) {
                                destroyOwnedStages(stages, ownedNames);
                                *out = MediaPipelineConfig();
                                return rerr;
                        }
                        // When the source's output was renegotiated to
                        // match the sink's preferred input (no-CSC
                        // path), update the producer cache so any
                        // downstream fan-out routes see the new shape
                        // instead of the original.
                        if (renegotiatedFromDesc.isValid()) {
                                producedBy.insert(r.from, renegotiatedFromDesc);
                        }
                        producedBy.insert(r.to, postDesc);
                }
        }

        destroyOwnedStages(stages, ownedNames);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
