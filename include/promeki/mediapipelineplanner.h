/**
 * @file      mediapipelineplanner.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/map.h>
#include <promeki/mediaio.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class MediaDesc;

/**
 * @brief Resolves a partial @ref MediaPipelineConfig into one whose
 *        every route is directly format-compatible by splicing in
 *        the bridging stages declared via @ref MediaIOFactory::bridge.
 * @ingroup pipeline
 *
 * The planner runs offline (no signals, no frame motion).  It walks
 * every route, queries the source's produced @ref MediaDesc and the
 * sink's accepted @ref MediaDesc, and — when they differ — chooses
 * the cheapest bridge (or short bridge chain) that closes the gap.
 *
 * @par How sources are queried
 *
 * For every source stage (no upstream in the route graph) the planner
 * needs to know the @ref MediaDesc that stage will produce.  It tries
 * the following sources in order:
 *
 *   1. The cached @ref MediaIO::mediaDesc on a stage that the caller
 *      already opened.
 *   2. The @c preferredFormat field of @ref MediaIO::describe.
 *   3. The pre-open hint set via @c MediaIO::setExpectedDesc.
 *   4. As a last resort, the planner opens the source briefly in
 *      @c MediaIO::Source mode to read its mediaDesc, then closes
 *      it.  Backends with side-effecting opens (RTP, V4L2 capture)
 *      pay the cost of one open / close cycle during planning.
 *      The probe uses only the stage's registered backend and
 *      @c path — it does @em not forward the caller's
 *      @c expectedDesc / @c expectedAudioDesc / @c expectedMetadata
 *      pre-open hints on the injected @ref MediaIO, nor any per-stage
 *      metadata from the input @ref MediaPipelineConfig.  Backends
 *      whose opened @c mediaDesc depends on those hints should
 *      implement @ref MediaIO::describe (strategy 2) or set
 *      @c expectedDesc (strategy 3) so the planner never reaches the
 *      probe path.
 *
 * @par Bridge solving
 *
 * For each gapped route the planner asks every registered backend
 * with a @ref MediaIOFactory::bridge callback whether it can
 * convert from the source desc to the sink's preferred desc.  The
 * cheapest applicable bridge wins.  When no single bridge fits, the
 * planner tries the codec-transitive two-hop pattern (VideoDecoder +
 * VideoEncoder) for compressed→compressed transitions.  Deeper
 * chains are out of scope for v1; a future revision will replace
 * this with a generic Dijkstra search bounded by
 * @ref Policy::maxBridgeDepth.
 *
 * @par Limitations in v1
 *
 *   - Simultaneous frame-rate @em and pixel-format gaps in a single
 *     hop are not solved.  CSC rejects frame-rate mismatches and
 *     FrameSync rejects pixel-format mismatches, so a 30 fps
 *     @c RGBA8 source feeding a 24 fps @c NV12 sink must be resolved
 *     by the caller inserting an explicit intermediate stage
 *     (typically a CSC ahead of FrameSync or vice-versa).  The
 *     planner reports @c Error::NotSupported with a diagnostic when
 *     it hits this case.
 *   - The codec-transitive two-hop path only applies when both ends
 *     are compressed.  A compressed source feeding an uncompressed
 *     sink needs a single VideoDecoder hop, which is handled by the
 *     normal single-hop path.
 *
 * @par Determinism
 *
 * Iteration order over the registered bridges is deterministic
 * (matches @ref MediaIOFactory::registeredFactories), so given the same input
 * config and registry state the planner always returns the same
 * resolved config.  The resolved config round-trips through JSON and
 * @ref DataStream the same as any hand-authored config.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * The planner is typically used briefly at pipeline-build time on the
 * caller's thread.  The static @ref MediaIO registry it queries is itself
 * thread-safe.
 */
class MediaPipelinePlanner {
        public:
                /**
                 * @brief Quality preference for bridge selection.
                 *
                 * Modifies the planner's cost function when there are
                 * multiple valid bridge chains.  All modes still respect
                 * @ref Policy::excludedBridges and
                 * @ref Policy::maxBridgeDepth.
                 */
                enum class Quality {
                        Highest,     ///< @brief Lowest unmodified cost wins (default).
                        Balanced,    ///< @brief Slight penalty for high-CPU bridges.
                        Fastest,     ///< @brief Heavy penalty for high-CPU bridges; prefers cheap CPU paths.
                        ZeroCopyOnly ///< @brief Reject any bridge with cost > 100; fail if no zero-copy chain exists.
                };

                /**
                 * @brief Tunable knobs for the planner.
                 */
                struct Policy {
                                /** @brief Default constructor — fields take their default values. */
                                Policy() {}

                                /** @brief Quality preference; default @ref Quality::Highest. */
                                Quality quality = Quality::Highest;

                                /**
                         * @brief Maximum number of bridges the planner may
                         *        insert per route.  Caps both single-hop
                         *        retries and (future) Dijkstra search
                         *        depth.  Default 4.
                         */
                                int maxBridgeDepth = 4;

                                /**
                         * @brief Backend type names the planner is forbidden
                         *        from using as bridges.  Use to force a
                         *        particular path (e.g. block "VideoEncoder"
                         *        to force a transcode-free pipeline).
                         */
                                StringList excludedBridges;
                };

                /**
                 * @brief Map of stage name to externally-built MediaIO.
                 *
                 * Some backends (the SDL player, a future hardware-
                 * device backend) cannot be instantiated through the
                 * registry because they need application-supplied
                 * resources (window handles, audio device pointers).
                 * Callers register them with
                 * @ref MediaPipeline::injectStage; the planner needs
                 * the same map so it can call @ref MediaIO::describe
                 * and @ref MediaIO::proposeInput on the live instance
                 * instead of failing to build a stand-in from the
                 * registry.
                 */
                using InjectedStages = ::promeki::Map<String, MediaIO *>;

                /**
                 * @brief Resolves @p in into a fully-direct @p out.
                 *
                 * On success @p out has every route's source MediaDesc
                 * directly accepted by the route's sink — the same
                 * shape contract that @ref MediaPipeline::build assumes
                 * but never enforces.  Inserted bridge stages get
                 * generated names of the form
                 * @c "<from>__bridge<N>__<to>" so they are stable
                 * across re-plans.
                 *
                 * On failure @p out is left empty.  When @p diagnostic
                 * is non-null the planner writes a multi-line, human-
                 * readable description of the first unsolvable route.
                 *
                 * @param in         Partial / unresolved pipeline config.
                 * @param out        Receives the resolved config.
                 * @param policy     Optional planner policy.
                 * @param diagnostic Optional human-readable error detail.
                 * @return @c Error::Ok on success, otherwise the first
                 *         underlying error (@ref Error::NotSupported for
                 *         unbridgeable routes, @ref Error::Invalid for a
                 *         malformed input config).
                 */
                static Error plan(const MediaPipelineConfig &in, MediaPipelineConfig *out,
                                  const Policy &policy = Policy(), String *diagnostic = nullptr);

                /**
                 * @brief Same as @ref plan but consults @p injected for
                 *        externally-built stages.
                 *
                 * For every stage name in the input config, the planner
                 * uses the matching @c MediaIO* from @p injected if
                 * present (querying it for @c describe / @c proposeInput
                 * just like a registry-built stage).  When a name is
                 * absent the planner falls back to building from the
                 * registry as before.  This is how injected SDL /
                 * device handles take part in the autoplan pass.
                 */
                static Error plan(const MediaPipelineConfig &in, MediaPipelineConfig *out,
                                  const InjectedStages &injected, const Policy &policy = Policy(),
                                  String *diagnostic = nullptr);

                /**
                 * @brief Returns true when every route in @p config is
                 *        already direct (the source MediaDesc matches
                 *        what the sink reports via @ref MediaIO::proposeInput).
                 *
                 * Cheap: runs the same describe / proposeInput queries
                 * as @ref plan but without instantiating any bridges.
                 * Useful as a pre-flight check before
                 * @ref MediaPipeline::build to decide whether
                 * @ref plan needs to run at all.
                 *
                 * @param config The pipeline config to inspect.
                 * @param diagnostic Optional output describing the
                 *                   first gapped route on a false return.
                 * @return @c true when no bridge is required.
                 */
                static bool isResolved(const MediaPipelineConfig &config, String *diagnostic = nullptr);

                /**
                 * @brief Applies the @p policy's quality bias to a raw
                 *        bridge cost.
                 *
                 * Exposed for tests; planner callers normally just
                 * pass the cost back in.  See @ref Quality for the
                 * meaning of each bias.
                 */
                static int adjustCostForQuality(int rawCost, Quality quality);
};

PROMEKI_NAMESPACE_END
