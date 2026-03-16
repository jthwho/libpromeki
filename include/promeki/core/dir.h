/**
 * @file      core/dir.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/filepath.h>
#include <promeki/core/string.h>
#include <promeki/core/list.h>
#include <promeki/core/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Provides directory operations using std::filesystem.
 * @ingroup core_io
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
                 * @brief Returns a Dir for the system temp directory.
                 * @return A Dir wrapping the temp directory.
                 */
                static Dir temp();

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
