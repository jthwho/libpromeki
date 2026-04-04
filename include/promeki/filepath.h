/**
 * @file      filepath.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <filesystem>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Simple value type wrapping std::filesystem::path.
 * @ingroup io
 *
 * Provides a convenient interface for path decomposition, joining,
 * and filesystem queries. This is a simple data object — always
 * copied by value, no shared ownership.
 *
 * This class is not thread-safe. Concurrent access to a single
 * instance requires external synchronization.
 */
class FilePath {
        public:
                /** @brief Constructs an empty file path. */
                FilePath() = default;

                /**
                 * @brief Constructs a FilePath from a String.
                 * @param path The path string.
                 */
                FilePath(const String &path) : _path(path.str()) { }

                /**
                 * @brief Constructs a FilePath from a C string.
                 * @param path The path string.
                 */
                FilePath(const char *path) : _path(path) { }

                /**
                 * @brief Constructs a FilePath from a std::filesystem::path.
                 * @param path The filesystem path.
                 */
                FilePath(const std::filesystem::path &path) : _path(path) { }

                /**
                 * @brief Returns true if the path is empty.
                 * @return true if the path has no components.
                 */
                bool isEmpty() const {
                        return _path.empty();
                }

                /**
                 * @brief Returns the filename component (including extension).
                 * @return The filename as a String.
                 */
                String fileName() const {
                        return _path.filename().string();
                }

                /**
                 * @brief Returns the filename without its extension.
                 * @return The stem as a String.
                 */
                String baseName() const {
                        return _path.stem().string();
                }

                /**
                 * @brief Returns the file extension without the leading dot.
                 * @return The suffix as a String (e.g. "png", "wav").
                 */
                String suffix() const {
                        auto ext = _path.extension().string();
                        if(ext.size() > 1) return ext.substr(1);
                        return String();
                }

                /**
                 * @brief Returns the complete suffix (all extensions).
                 *
                 * For "archive.tar.gz", returns "tar.gz".
                 * @return The complete suffix as a String.
                 */
                String completeSuffix() const {
                        auto fn = _path.filename().string();
                        auto pos = fn.find('.');
                        if(pos == std::string::npos || pos == 0) return String();
                        return fn.substr(pos + 1);
                }

                /**
                 * @brief Returns the parent directory as a FilePath.
                 * @return The parent directory path.
                 */
                FilePath parent() const {
                        return FilePath(_path.parent_path());
                }

                /**
                 * @brief Joins this path with another path component.
                 * @param other The path component to append.
                 * @return A new FilePath with the joined result.
                 */
                FilePath join(const FilePath &other) const {
                        return FilePath(_path / other._path);
                }

                /**
                 * @brief Joins this path with another path component.
                 * @param other The path component to append.
                 * @return A new FilePath with the joined result.
                 */
                FilePath operator/(const FilePath &other) const {
                        return join(other);
                }

                /**
                 * @brief Joins this path with a string path component.
                 * @param other The path component to append.
                 * @return A new FilePath with the joined result.
                 */
                FilePath operator/(const String &other) const {
                        return FilePath(_path / std::filesystem::path(other.str()));
                }

                /**
                 * @brief Joins this path with a C string path component.
                 * @param other The path component to append.
                 * @return A new FilePath with the joined result.
                 */
                FilePath operator/(const char *other) const {
                        return FilePath(_path / std::filesystem::path(other));
                }

                /**
                 * @brief Returns true if the path exists on the filesystem.
                 * @return true if the path refers to an existing entry.
                 */
                bool exists() const {
                        std::error_code ec;
                        return std::filesystem::exists(_path, ec);
                }

                /**
                 * @brief Returns true if the path is absolute.
                 * @return true if the path is an absolute path.
                 */
                bool isAbsolute() const {
                        return _path.is_absolute();
                }

                /**
                 * @brief Returns true if the path is relative.
                 * @return true if the path is a relative path.
                 */
                bool isRelative() const {
                        return _path.is_relative();
                }

                /**
                 * @brief Returns the absolute form of this path.
                 * @return A FilePath with the absolute path.
                 */
                FilePath absolutePath() const {
                        std::error_code ec;
                        return FilePath(std::filesystem::absolute(_path, ec));
                }

                /**
                 * @brief Converts the path to a String.
                 * @return The path as a String.
                 */
                String toString() const {
                        return _path.string();
                }

                /**
                 * @brief Returns the underlying std::filesystem::path.
                 * @return A const reference to the internal path.
                 */
                const std::filesystem::path &toStdPath() const {
                        return _path;
                }

                /** @brief Equality comparison. */
                bool operator==(const FilePath &other) const {
                        return _path == other._path;
                }

                /** @brief Inequality comparison. */
                bool operator!=(const FilePath &other) const {
                        return _path != other._path;
                }

                /** @brief Less-than comparison for ordered containers. */
                bool operator<(const FilePath &other) const {
                        return _path < other._path;
                }

        private:
                std::filesystem::path _path;
};

PROMEKI_NAMESPACE_END
