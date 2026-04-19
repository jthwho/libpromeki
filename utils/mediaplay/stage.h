/**
 * @file      mediaplay/stage.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Type-aware CLI-value parsing, stage classification, and stage
 * builders that turn a StageSpec into a live MediaIO instance.
 */

#pragma once

#include <promeki/audiodesc.h>
#include <promeki/enum.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/variant.h>

namespace mediaplay {

// --------------------------------------------------------------------------
// Synthetic stage names
// --------------------------------------------------------------------------
//
// mediaplay recognises two stage identifiers that do not map to a
// registered MediaIO backend:
//
//   "SDL"      — the SDL player sink, constructed via
//                createSDLPlayer() with the utility's own
//                SDLVideoWidget / SDLAudioOutput.  Not a registered
//                backend because it needs UI handles.
//
//   "__file__" — used internally to mark a stage whose argument is a
//                filesystem path that should be routed through
//                MediaIO::createForFile{Read,Write}().  Never typed
//                directly on the CLI; classifyStageArg() inserts it
//                when it sees a path-shaped argument.
extern const char *const kStageSdl;
extern const char *const kStageFile;

/**
 * @brief Declarative description of a single pipeline stage.
 *
 * Populated from the CLI by the option parser and consumed by main()
 * to build the real MediaIO instances.  A stage is either a MediaIO
 * backend (by registered name) or a filesystem path that the MediaIO
 * file-write auto-detection will resolve to the right backend.
 */
struct StageSpec {
        promeki::String             type;           ///< Backend name (e.g. "TPG", "QuickTime", "SDL", "__file__").
        promeki::String             path;           ///< Filename when @c type is @c __file__.
        promeki::MediaIO::Config    config;         ///< Stage-specific config, populated from --ic / --oc / --cc.
        promeki::Metadata           metadata;       ///< Stage-specific metadata, populated from --im / --om / --cm.
        // Raw key:value strings collected on the command line, in the
        // order they appeared.  Applied to @c config after the stage's
        // default config is loaded so values can be parsed against the
        // real target types.
        promeki::StringList         rawKeyValues;
        // Raw key:value strings for the metadata schema, parsed
        // against the backend's @c defaultMetadata() the same way
        // @c rawKeyValues is parsed against @c defaultConfig().
        promeki::StringList         rawMetaKeyValues;
};

// --------------------------------------------------------------------------
// SDL pseudo-backend schema
// --------------------------------------------------------------------------
//
// The SDL player is a special case: it can't be a registry-managed
// MediaIO backend because SDLWindow / SDLVideoWidget / SDLAudioOutput
// have to be constructed on the main thread (and the main thread is
// blocked inside MediaIO::open when the strand worker runs Open, so a
// post-callable-and-wait pattern would deadlock).  Instead, mediaplay
// keeps the synthetic kStageSdl marker and exposes SDL's config /
// metadata schemas via these helpers, so from the user's perspective
// it still looks like any other `-o NAME` backend: it shows up in the
// `--i list` / `--o list` registry dump, the `--help` schema table,
// and accepts `--oc Key:Value` just like TPG / ImageFile / QuickTime.

/** @brief Returns the SDL player's config schema with empty / default values. */
promeki::MediaIO::Config sdlDefaultConfig();

/** @brief Returns the SDL player's config specs. */
promeki::MediaIO::Config::SpecMap sdlConfigSpecs();

/** @brief Returns the SDL player's metadata schema (currently empty). */
promeki::Metadata sdlDefaultMetadata();

/** @brief Human-readable description for the SDL entry in listings. */
const char *sdlDescription();

// --------------------------------------------------------------------------
// Listing helpers (value == "list" sentinel, and --in list)
// --------------------------------------------------------------------------

/** @brief Dumps every registered MediaIO backend and exits. */
[[noreturn]] void listMediaIOBackendsAndExit();

/**
 * @brief Prints every value of an Enum::Type and exits.
 *
 * @param keyLabel   Fully-qualified label to print as the header
 *                   (e.g. @c "--sc[TPG].VideoPattern").
 * @param type       The enum type whose values should be listed.
 * @param isEnumList When @c true the header is labelled
 *                   @c "EnumList <Type>" instead of @c "Enum <Type>"
 *                   and a comma-separated usage hint is appended
 *                   after the value list.  Pass @c true for keys
 *                   whose spec type is @c Variant::TypeEnumList.
 */
[[noreturn]] void listEnumTypeAndExit(const promeki::String &keyLabel,
                                      promeki::Enum::Type type,
                                      bool isEnumList = false);

/** @brief Prints every registered PixelDesc and exits. */
[[noreturn]] void listPixelFormatsAndExit(const promeki::String &keyLabel);

/**
 * @brief Prints every registered VideoCodec and exits.
 *
 * Used by `--cc VideoCodec:list` (and similar) to enumerate every
 * codec the @ref VideoCodec registry knows about, with E/D capability
 * flags showing whether the codec has encoder / decoder factories
 * registered in this build.
 */
[[noreturn]] void listVideoCodecsAndExit(const promeki::String &keyLabel);

/** @brief Prints every registered AudioCodec and exits. */
[[noreturn]] void listAudioCodecsAndExit(const promeki::String &keyLabel);

// --------------------------------------------------------------------------
// Value parser helpers
// --------------------------------------------------------------------------

/**
 * @brief Splits `Key:Value` into `(Key, Value)`.
 *
 * The first `:` is the delimiter; everything after is the value
 * verbatim.  Empty keys are rejected.
 */
bool splitKeyValue(const promeki::String &arg,
                   promeki::String &key,
                   promeki::String &val);

/**
 * @brief Applies the raw `Key:Value` strings collected for @p stage
 *        onto its MediaConfig, resolving each value against the
 *        backend's default config.
 */
promeki::Error applyStageConfig(StageSpec &stage,
                                const promeki::String &stageLabel);

/**
 * @brief Applies the raw `Key:Value` strings collected for @p stage
 *        onto its Metadata, resolving each value against the
 *        backend's @c defaultMetadata() schema (so Timecode, bool,
 *        and numeric keys are type-coerced, not just stored as strings).
 */
promeki::Error applyStageMetadata(StageSpec &stage,
                                  const promeki::String &stageLabel);

/**
 * @brief Classifies a stage-identifier argument.
 *
 * Priority order: synthetic names (`SDL`), then registered MediaIO
 * backends, then "filesystem path" as the fallback.
 */
promeki::Error classifyStageArg(const promeki::String &arg, StageSpec &stage);

// --------------------------------------------------------------------------
// Stage builders (StageSpec → MediaIO)
// --------------------------------------------------------------------------

/**
 * @brief Instantiates the source MediaIO from the parsed spec.
 *
 * Handles the `__file__` path (via `MediaIO::createForFileRead`) and
 * registered backends (via `MediaIO::create`), then applies
 * @c --ic overrides on top of the live config.
 */
promeki::MediaIO *buildSource(const StageSpec &spec);

/**
 * @brief Instantiates an intermediate pipeline stage (any
 *        registered MediaIO backend that supports InputAndOutput).
 *
 * Reads the backend name from @c spec.type (set by the @c -c
 * command-line flag) and applies the stage's @c --cc / @c --cm
 * overrides on top of that backend's default config.
 */
promeki::MediaIO *buildIntermediateStage(const StageSpec &spec);

/**
 * @brief Instantiates a single output-side MediaIO from a StageSpec.
 *
 * The SDL player is handled by the caller because it needs widget
 * pointers that live in main().  Everything else — registered
 * backends and file paths — is built here.
 */
promeki::MediaIO *buildFileSink(const StageSpec &spec,
                                const promeki::MediaDesc &srcDesc,
                                const promeki::AudioDesc &srcAudioDesc,
                                const promeki::Metadata &srcMetadata,
                                promeki::String &labelOut);

// --------------------------------------------------------------------------
// CLI → MediaPipelineConfig::Stage resolver
// --------------------------------------------------------------------------

/**
 * @brief Resolves a CLI-parsed @ref StageSpec into a
 *        @ref promeki::MediaPipelineConfig::Stage.
 *
 * Looks up the backend's default config and spec map (or the SDL
 * pseudo-backend's schema for @c SDL stages), applies every
 * @c --sc/@c --cc/@c --dc override via @ref applyStageConfig, applies
 * every @c --sm/@c --cm/@c --dm override via @ref applyStageMetadata,
 * and fills @p out with the resolved (@c type, @c path, @c mode,
 * @c config, @c metadata) tuple suitable for
 * @c MediaPipelineConfig::addStage.  File-path stages are emitted with
 * an empty @c type so the pipeline resolves the real backend via
 * @ref MediaIO::createForFileRead / @ref MediaIO::createForFileWrite
 * at @c build() time.
 *
 * @param rawSpec     CLI-parsed stage spec (type / path / rawKeyValues).
 * @param mode        Desired open mode (@ref MediaIO::Source /
 *                    @c Input / @c InputAndOutput).
 * @param stageName   Unique stage name for route targeting and errors.
 * @param scopeLabel  Label for @ref applyStageConfig diagnostics
 *                    (e.g. @c "--sc[TPG]").
 * @param out         Output — populated on success.
 * @return @c Error::Ok on success, or the first parsing error.
 */
promeki::Error resolveStagePlan(const StageSpec &rawSpec,
                                promeki::MediaIO::Mode mode,
                                const promeki::String &stageName,
                                const promeki::String &scopeLabel,
                                promeki::MediaPipelineConfig::Stage &out);

} // namespace mediaplay
