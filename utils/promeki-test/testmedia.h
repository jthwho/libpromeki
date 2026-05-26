/**
 * @file      testmedia.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Helpers for discovering and loading the @c testmedia/ corpus that
 * the promeki-test runner uses to build data-driven test cases.
 *
 * The corpus is an external, version-controlled repository (see
 * @c testmedia/README.md) symlinked into the source tree at
 * @c testmedia/.  Every asset carries a JSON sidecar describing what
 * it is and (where applicable) the expected output of processing it
 * (e.g. the verbatim transcript for a speech clip).  The whole tree
 * is summarised in @c testmedia/index.json which the loader below
 * parses into a list of @ref TestMediaEntry records test suites then
 * iterate over to register one case per asset.
 *
 * Discovery order (first hit wins):
 *
 *   1. CLI argument @c --testmedia / @c -m on @c promeki-test.
 *   2. Environment variable @c PROMEKI_TESTMEDIA.
 *   3. The compile-time-pinned source tree location
 *      <tt>${PROMEKI_SOURCE_DIR}/testmedia</tt> — the symlink that
 *      lives in this checkout by default.
 *
 * The runner stamps the resolved root onto @ref TestParams under
 * @c TestParams::TestMediaRoot so suite registration code can read
 * it from the global params at register time and skip cleanly when
 * the corpus is absent.
 */

#pragma once

#include <promeki/filepath.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        /**
         * @brief One declared expected output for a testmedia asset.
         *
         * Mirrors the @c expectedOutputs entries in @c index.json.
         * @ref path is rewritten by the loader to an absolute path
         * (the on-disk index already rewrites the sidecar's relative
         * path to repo-relative; we apply the testmedia root prefix on
         * top so suites get a path they can hand straight to a
         * file reader).
         */
        struct TestMediaExpectedOutput {
                        String type;        ///< @brief Output kind (e.g. @c "transcript", @c "metadata", @c "thumbnail").
                        FilePath path;      ///< @brief Absolute path to the reference output file.
                        String description; ///< @brief Human-readable description (may be empty).
                        String tool;        ///< @brief Optional generator tool (e.g. @c "whisper"); may be empty.
        };

        /**
         * @brief One asset entry from the testmedia index.
         *
         * Mirrors the union of fields each suite typically wants from
         * a sidecar — see @c testmedia/docs/sidecar-format.md for the
         * authoritative schema.  Fields not declared in the sidecar
         * are left at their default-constructed values.
         */
        struct TestMediaEntry {
                        FilePath path;      ///< @brief Absolute path to the asset.
                        FilePath relPath;   ///< @brief Path relative to the testmedia root (handy for naming).
                        String mediaType;   ///< @brief @c "audio" | @c "video" | @c "image" | @c "font" | @c "document".
                        StringList useCases;///< @brief Kebab-case use-case slugs (e.g. @c "speech-to-text").
                        String title;       ///< @brief Short human name from the sidecar.
                        String description; ///< @brief Longer prose description (may be empty).
                        StringList tags;    ///< @brief Free-form descriptors.
                        bool inLfs = false; ///< @brief @c true if the asset is a Git-LFS object.
                        int64_t bytes = 0;  ///< @brief Asset size in bytes (logical, not on-disk).
                        List<TestMediaExpectedOutput> expectedOutputs;
        };

        /**
         * @brief Resolves the testmedia root the runner should use.
         *
         * Tries, in order:
         *
         *   1. @p cliOverride if non-empty.
         *   2. The @c PROMEKI_TESTMEDIA environment variable.
         *   3. @c PROMEKI_SOURCE_DIR / @c "testmedia" — the default
         *      symlink location inside this checkout.
         *
         * Each candidate is validated by checking that an
         * @c index.json file exists immediately under it.  The first
         * candidate that passes is returned; otherwise an empty
         * @ref FilePath is returned and the caller should treat the
         * corpus as absent (skipping data-driven cases cleanly).
         *
         * @param cliOverride The @c --testmedia / @c -m value or
         *                    empty when the option was not supplied.
         * @return The resolved root, or an empty FilePath when no
         *         candidate is usable.
         */
        FilePath resolveTestMediaRoot(const String &cliOverride);

        /**
         * @brief Loads every asset entry described by @c index.json.
         *
         * The on-disk index already carries every field tests need;
         * this helper only re-shapes it into a list of strongly-typed
         * @ref TestMediaEntry values and resolves each asset / expected-
         * output path against @p root so callers do not have to.
         *
         * Files that the on-disk index records but are not currently
         * present on the filesystem (lazy LFS pointers that the user
         * has not fetched yet) are still returned — the runner's
         * per-case logic checks for filesystem presence and emits a
         * @c Skip with a clear reason when the blob is missing.
         *
         * @param root        Absolute path to the testmedia root (as
         *                    returned by @ref resolveTestMediaRoot).
         * @param outEntries  Populated with one entry per indexed
         *                    asset on success.
         * @param outErrMsg   Optional out parameter: on failure, a
         *                    single-line human-readable diagnostic.
         * @return @c true on success, @c false on any read / parse
         *         error.
         */
        bool loadTestMediaIndex(const FilePath &root, List<TestMediaEntry> &outEntries,
                                String *outErrMsg = nullptr);

        /**
         * @brief Filters @p all by exact use-case slug match.
         *
         * Returns every entry whose @ref TestMediaEntry::useCases
         * contains @p useCase.  Convenience helper so suite
         * registration code does not have to inline the predicate.
         */
        List<TestMediaEntry> filterByUseCase(const List<TestMediaEntry> &all, const String &useCase);

        /**
         * @brief Returns the first expected-output of type @p type, or
         *        a default-constructed instance when @p entry has none.
         *
         * Matches by case-insensitive comparison on
         * @ref TestMediaExpectedOutput::type so callers can pass any
         * casing convention.
         */
        TestMediaExpectedOutput findExpectedOutput(const TestMediaEntry &entry, const String &type);

} // namespace promekitest

PROMEKI_NAMESPACE_END
