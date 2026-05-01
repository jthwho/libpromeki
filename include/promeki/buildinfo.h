/**
 * @file      buildinfo.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class Error;

/**
 * @brief Pre-release stage of a libpromeki build.
 * @ingroup util
 *
 * Drives the optional pre-release suffix in the @c VERSION file
 * (`MAJOR.MINOR.PATCH-alpha1`, `-beta3`, `-rc2` …).  The four stages
 * progress monotonically: @c Alpha (active development) → @c Beta
 * (in testing) → @c RC (release candidate) → @c Release (no suffix).
 * @c Alpha, @c Beta, and @c RC always carry a @c stageNum of 1 or
 * larger; @c Release has @c stageNum 0.
 */
enum class BuildStage {
        Release = 0, ///< Plain release; no pre-release suffix in the VERSION file.
        Alpha   = 1, ///< Active development.  VERSION suffix `-alphaN`, N >= 1.
        Beta    = 2, ///< In testing.  VERSION suffix `-betaN`, N >= 1.
        RC      = 3  ///< Release candidate.  VERSION suffix `-rcN`, N >= 1.
};

/**
 * @brief Returns the lower-case short name for a BuildStage.
 * @ingroup util
 *
 * Matches the spelling used in the @c VERSION file pre-release suffix
 * and in the @c {stage} token of @c formatBuildInfo() — one of
 * @c "alpha", @c "beta", @c "rc", or @c "release".
 */
const char *buildStageName(BuildStage stage);

/**
 * @brief Holds compile-time build information for the library.
 * @ingroup util
 *
 * @par Thread Safety
 * Trivially thread-safe.  The BuildInfo struct is populated at
 * compile time and is immutable; the free helpers are pure
 * formatters that read only that immutable state.  All accessors
 * are safe to call from any thread.
 */
typedef struct {
                const char *name;        ///< Project name.
                const char *version;     ///< Version string (always MAJOR.MINOR.PATCH — never carries the stage suffix or CI build number).
                const char *repoident;   ///< Repository identifier (e.g. git commit hash).
                const char *ref;         ///< Most informative git ref label for "where this build came from": branch name, exact tag (e.g. @c "v1.2.3"), @c "describe" form (e.g. @c "v1.2.0-15-gabc1234"), or short hash. Falls back to @c "unknown". Not part of @c ident.
                const char *date;        ///< Build date string (__DATE__).
                const char *time;        ///< Build time string (__TIME__).
                const char *hostname;    ///< Hostname of the build machine.
                const char *type;        ///< Build type (e.g. "Release", "Debug").
                int         major;       ///< Major version component (matches the leading integer of @c version).
                int         minor;       ///< Minor version component.
                int         patch;       ///< Patch version component.
                int         build;       ///< CI build number from the repo-root @c BUILD file, or 0 for local developer builds.
                BuildStage  stage;       ///< Pre-release stage (Alpha/Beta/RC) or Release.
                int         stageNum;    ///< Stage number for Alpha/Beta/RC (always >= 1); 0 for Release.
                const char *ident;       ///< Opaque per-build identity (matches PROMEKI_BUILD_IDENT at library compile time).
} BuildInfo;

/** @brief Returns a pointer to the global BuildInfo structure. */
const BuildInfo *getBuildInfo();

/** @brief Writes all build information fields to the log output. */
void logBuildInfo();

/**
 * @brief Returns the build identity as a human-readable string.
 *
 * Includes the project name, version, repo ident, build type,
 * build date/time, and build hostname.
 */
String buildInfoString();

/**
 * @brief Returns the platform and compiler as a human-readable string.
 *
 * Example: @c "Platform: Linux | Compiler: GCC 13.2.0 | C++: 202002"
 */
String buildPlatformString();

/**
 * @brief Returns enabled library features as a space-separated string.
 *
 * Example: @c "Features: NETWORK PROAV MUSIC PNG JPEG AUDIO CSC"
 */
String buildFeatureString();

/**
 * @brief Returns hardware and runtime info for the current process.
 *
 * Includes the CPU count and process ID.
 */
String runtimeInfoString();

/**
 * @brief Returns whether promekiDebug() logging is compiled in.
 */
String debugStatusString();

/**
 * @brief Returns all build, platform, feature, runtime, and debug
 *        status strings as a list of lines.
 *
 * Convenience for logging or display — each entry is one logical
 * line suitable for printing or enqueuing to the Logger.
 */
StringList buildInfoStrings();

/**
 * @brief Expand @c {token}-style placeholders against a BuildInfo.
 * @ingroup util
 *
 * Lightweight named-token formatter for composing build-info strings
 * without forcing every call site to handcraft printf format strings.
 * Use this when the *shape* of the output depends on context (e.g. a
 * banner versus a one-line status line); for the standard banners,
 * call @c buildInfoString() / @c buildPlatformString() etc., which
 * are themselves implemented as preset templates over this function.
 *
 * @par Recognized tokens
 * - @c {name}      — project name (e.g. @c "libpromeki").
 * - @c {version}   — bare MAJOR.MINOR.PATCH (no stage, no build).
 * - @c {major}, @c {minor}, @c {patch} — integer version components.
 * - @c {build}     — CI build number, or @c 0 for local builds.
 * - @c {stage}     — @c "alpha", @c "beta", @c "rc", or @c "release".
 * - @c {stageNum}  — stage number (>=1 for alpha/beta/rc; 0 for release).
 * - @c {extra}     — auto-composed pre-release suffix: @c "-alpha1",
 *                    @c "-beta3", @c "-rc2", or empty for release.
 *                    Does *not* include the CI build number.
 * - @c {date}, @c {time}, @c {type}, @c {hostname} — build environment.
 * - @c {repoident} — full git commit hash.
 * - @c {ref}       — git ref label (branch / tag / describe / short hash); see @c BuildInfo::ref.
 * - @c {ident}     — opaque per-build identity (see @c verifyBuildIdent()).
 *
 * Unknown tokens are emitted verbatim (e.g. @c "{foo}") so typos are
 * visible in the output rather than silently dropped.  Use @c "{{" and
 * @c "}}" to emit literal braces.
 *
 * @param fmt  Format string with @c {token} placeholders.
 * @param bi   BuildInfo to format against.  If @c nullptr, the library's
 *             compiled-in BuildInfo (@c getBuildInfo()) is used.
 * @return The expanded String.
 */
String formatBuildInfo(const String &fmt, const BuildInfo *bi = nullptr);

/**
 * @brief Verify that the library's runtime build identity matches the
 *        caller's compile-time identity (staleness check).
 * @ingroup util
 *
 * @par What this is for
 * Detects that the calling binary was compiled against a *different*
 * libpromeki build than the one currently loaded at runtime.  An
 * application captures @c PROMEKI_BUILD_IDENT (a @c \#define from
 * @c <promeki/buildident.h> regenerated on every library build) into
 * one of its translation units at *its* compile time, then passes that
 * captured value here.  The function compares it byte-for-byte against
 * @c getBuildInfo()->ident, which the library compiled into its own
 * binary.  Any divergence means the .so was rebuilt without rebuilding
 * the caller — i.e., the caller is stale.
 *
 * @par What this is *not* for
 * This is *only* a stale-binary detector.  Do not use it for version
 * compatibility, feature gating, or any human-visible reporting.  For
 * normal version / build queries, use @c getBuildInfo() and the
 * @c buildInfoString() / @c buildPlatformString() / @c buildFeatureString()
 * helpers — those are the supported path for inspecting library build
 * details.  The ident format is opaque and may change at any time.
 *
 * @param expected  The PROMEKI_BUILD_IDENT value captured at the
 *                  caller's compile time (typically supplied via the
 *                  @c PROMEKI_VERIFY_BUILD_IDENT() macro).
 * @param err       Optional Error to populate on mismatch
 *                  (@c Error::BuildIdentMismatch).
 *
 * @return @c true if the idents match, @c false otherwise.  Mismatch
 *         is also logged via @c promekiErr() with both strings so the
 *         failure is diagnosable from logs alone.
 */
bool verifyBuildIdent(const char *expected, Error *err = nullptr);

/**
 * @brief Fatal variant of @c verifyBuildIdent().
 * @ingroup util
 *
 * Calls @c std::abort() (after logging both ident strings via
 * @c promekiErr()) if the library's runtime identity does not match
 * the caller's compile-time identity.  Drop one call into your
 * application's @c main() — typically via the
 * @c PROMEKI_VERIFY_BUILD_IDENT_OR_ABORT() macro — to make a stale
 * binary fail loudly at startup rather than silently executing
 * against changed library code.
 *
 * Same scope rules as @c verifyBuildIdent(): use *only* for staleness
 * detection.  For normal build-info queries, use the BuildInfo
 * framework.
 *
 * @param expected  The PROMEKI_BUILD_IDENT value captured at the
 *                  caller's compile time.
 */
void verifyBuildIdentOrAbort(const char *expected);

PROMEKI_NAMESPACE_END

