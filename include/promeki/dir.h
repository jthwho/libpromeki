/**
 * @file      dir.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/filepath.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/numnameseq.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Provides directory operations using std::filesystem.
 * @ingroup io
 *
 * Dir is a simple utility class (not ObjectBase) that wraps
 * std::filesystem directory operations with the promeki API
 *
 * @par Example
 * @code
 * Dir dir("/tmp/output");
 * if(!dir.exists()) dir.mkpath();
 * StringList files = dir.entryList();
 * for(const String &f : files) { ... }
 * @endcode
 * conventions.
 */
class Dir {
        public:
                /** @brief Constructs a Dir with an empty path. */
                Dir() = default;

                /**
                 * @brief Constructs a Dir for the given path.
                 * @param path The directory path.
                 */
                Dir(const FilePath &path) : _path(path) { }

                /**
                 * @brief Constructs a Dir from a String path.
                 * @param path The directory path string.
                 */
                Dir(const String &path) : _path(path) { }

                /**
                 * @brief Constructs a Dir from a C string path.
                 * @param path The directory path string.
                 */
                Dir(const char *path) : _path(path) { }

                /**
                 * @brief Returns the directory path.
                 * @return The path as a FilePath.
                 */
                FilePath path() const { return _path; }

                /**
                 * @brief Returns true if the directory exists.
                 * @return true if the path refers to an existing directory.
                 */
                bool exists() const;

                /**
                 * @brief Returns true if the directory is empty.
                 *
                 * A non-existent directory is considered empty.
                 * @return true if the directory has no entries.
                 */
                bool isEmpty() const;

                /**
                 * @brief Returns a list of all entries in the directory.
                 * @return A List of FilePath entries.
                 */
                List<FilePath> entryList() const;

                /**
                 * @brief Returns a filtered list of entries in the directory.
                 *
                 * Uses fnmatch() for glob-style pattern matching.
                 * @param filter The glob pattern to match filenames against.
                 * @return A List of matching FilePath entries.
                 */
                List<FilePath> entryList(const String &filter) const;

                /**
                 * @brief Returns all numbered file sequences found in the directory.
                 *
                 * Performs a single directory scan and runs every regular
                 * filename through @c NumNameSeq::parseList to group
                 * matching files into sequences.  A sequence may consist
                 * of a single file.  Files whose names contain no numeric
                 * run are not returned in any sequence.  Ordering within
                 * each sequence is determined by @c NumNameSeq — the
                 * returned head and tail are the minimum and maximum
                 * numeric values observed, regardless of scan order.
                 *
                 * This is the fast path for detecting image sequences in
                 * a folder: @code
                 * Dir d("/project/shot01");
                 * NumNameSeq::List seqs = d.numberedSequences();
                 * @endcode
                 *
                 * Sub-directories are ignored — only regular files are
                 * considered.  Files that cannot be parsed (no digit run)
                 * are silently dropped.
                 *
                 * @return A list of detected sequences (possibly empty).
                 */
                NumNameSeq::List numberedSequences() const;

                /**
                 * @brief Creates this directory.
                 *
                 * The parent directory must already exist.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error mkdir() const;

                /**
                 * @brief Creates this directory and all parent directories.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error mkpath() const;

                /**
                 * @brief Removes this directory (must be empty).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error remove() const;

                /**
                 * @brief Removes this directory and all its contents.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error removeRecursively() const;

                /**
                 * @brief Returns a Dir for the current working directory.
                 * @return A Dir wrapping the current directory.
                 */
                static Dir current();

                /**
                 * @brief Returns a Dir for the user's home directory.
                 * @return A Dir wrapping the home directory.
                 */
                static Dir home();

                /**
                 * @brief Returns a Dir for the library's temp directory.
                 *
                 * Resolves the path in this order:
                 *  1. @ref LibraryOptions::TempDir — when the option is
                 *     non-empty (set programmatically or via the
                 *     @c PROMEKI_OPT_TempDir environment variable), its
                 *     value is returned verbatim.  The directory is @em
                 *     not auto-created; callers that need it to exist
                 *     should invoke @ref mkpath on the returned @ref Dir.
                 *  2. @c std::filesystem::temp_directory_path — the
                 *     platform default (@c /tmp on Linux / macOS,
                 *     @c %TEMP% on Windows, and anything @c TMPDIR
                 *     points at when set).
                 *
                 * Use @ref LibraryOptions::TempDir to redirect every
                 * consumer of @ref temp (crash logs, scratch JSON,
                 * ad-hoc test output, ...) to a dedicated location —
                 * most commonly a disk-backed mount to keep large
                 * scratch files off tmpfs.
                 *
                 * @return A Dir wrapping the effective temp directory.
                 */
                static Dir temp();

                /**
                 * @brief Returns a Dir for the library's IPC directory.
                 *
                 * The IPC directory is where cross-process primitives
                 * — shared memory regions, local-socket files, lock
                 * files, etc. — live by convention.  Use this rather
                 * than hard-coding paths so IPC traffic can be moved
                 * in one place via @ref LibraryOptions::IpcDir.
                 *
                 * Resolves in this order:
                 *  1. @ref LibraryOptions::IpcDir — non-empty override
                 *     (set programmatically or via the
                 *     @c PROMEKI_OPT_IpcDir env var).
                 *  2. On Linux: @c /dev/shm/promeki — tmpfs-backed,
                 *     fast, and well suited to both @c shm_open objects
                 *     and path-named @c AF_UNIX sockets.  The @c promeki
                 *     sub-directory is @em not auto-created; callers
                 *     that need it should @ref mkpath the returned
                 *     @ref Dir.
                 *  3. Other platforms: the result of @ref Dir::temp.
                 *
                 * @return A Dir wrapping the effective IPC directory.
                 */
                static Dir ipc();

                /**
                 * @brief Sets the current working directory.
                 * @param path The new working directory.
                 * @return Error::Ok on success, or an error on failure.
                 */
                static Error setCurrent(const FilePath &path);

        private:
                FilePath _path;
};

PROMEKI_NAMESPACE_END
