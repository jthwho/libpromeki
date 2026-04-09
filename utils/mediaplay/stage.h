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

/** @brief Returns the SDL player's metadata schema (currently empty). */
promeki::Metadata sdlDefaultMetadata();

/** @brief Human-readable description for the SDL entry in listings. */
const char *sdlDescription();

// --------------------------------------------------------------------------
// Listing helpers (value == "list" sentinel, and --in list)
// --------------------------------------------------------------------------

/** @brief Dumps every registered MediaIO backend and exits. */
[[noreturn]] void listMediaIOBackendsAndExit();

/** @brief Prints every value of an Enum::Type and exits. */
[[noreturn]] void listEnumTypeAndExit(const promeki::String &keyLabel,
                                      promeki::Enum::Type type);

/** @brief Prints every registered PixelDesc and exits. */
[[noreturn]] void listPixelFormatsAndExit(const promeki::String &keyLabel);

// --------------------------------------------------------------------------
// Value parser
// --------------------------------------------------------------------------

/**
 * @brief Coerces a string into a Variant whose held type matches
 *        @p templateValue.
 *
 * The backend's default config is the canonical source of truth for
 * every key's target type — this function reads @c templateValue.type()
 * and dispatches to the appropriate typed constructor, leaning on
 * library @c fromString() helpers where they exist and on
 * @c Variant::get<T>() for scalar conversions.
 *
 * The special value `"list"` is intercepted up-front for types that
 * have a natural enumeration (Enum, PixelDesc) and surfaces valid
 * values to stdout before exiting.
 */
promeki::Variant parseConfigValue(const promeki::String &keyLabel,
                                  const promeki::String &str,
                                  const promeki::Variant &templateValue,
                                  promeki::Error *err);

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
 *        onto its MediaIOConfig, resolving each value against the
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
 * @brief Instantiates the optional Converter MediaIO from a spec
 *        collected via `-c / --convert` plus `--cc` overrides.
 */
promeki::MediaIO *buildConverter(const StageSpec &spec);

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

} // namespace mediaplay
