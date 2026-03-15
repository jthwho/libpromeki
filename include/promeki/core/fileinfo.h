/**
 * @file      core/fileinfo.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/filepath.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Provides information about a file system entry.
 *
 * Wraps std::filesystem to query file metadata such as existence, type,
 * size, permissions, and path components. The file status is lazily
 * cached and can be force-refreshed.
 */
class FileInfo {
        public:
                /** @brief File status type from the standard filesystem library. */
                using Status = std::filesystem::file_status;

                /**
                 * @brief Constructs a FileInfo for the given file path.
                 * @param filePath The path to the file or directory.
                 */
                FileInfo(const String &filePath) : _path(filePath.str()) {

                }

                /**
                 * @brief Constructs a FileInfo from a C string.
                 * @param filePath The path string.
                 */
                FileInfo(const char *filePath) : _path(filePath) {

                }

                /**
                 * @brief Constructs a FileInfo from a FilePath.
                 * @param fp The file path.
                 */
                FileInfo(const FilePath &fp) : _path(fp.toStdPath()) {

                }

                /**
                 * @brief Returns the path as a FilePath.
                 * @return The file path.
                 */
                FilePath filePath() const {
                        return FilePath(_path);
                }

                /**
                 * @brief Returns true if the file or directory exists.
                 * @return true if the path refers to an existing file system entry.
                 */
                bool exists() const {
                        return std::filesystem::exists(status());
                }

                /**
                 * @brief Returns the filename component of the path (including extension).
                 * @return The filename as a String.
                 */
                String fileName() const {
                        return _path.filename().string();
                }

                /**
                 * @brief Returns the filename without its extension.
                 * @return The base name (stem) as a String.
                 */
                String baseName() const {
                        return _path.stem().string();
                }

                /**
                 * @brief Returns the file extension without the leading dot.
                 * @return The file suffix as a String (e.g. "png", "wav").
                 */
                String suffix() const {
                        return _path.extension().string().substr(1); // Remove leading '.'
                }

                /**
                 * @brief Returns the absolute path of the parent directory.
                 * @return The parent directory path as a String.
                 */
                String absolutePath() const {
                        return _path.parent_path().string();
                }

                /**
                 * @brief Returns the absolute path to the file, including the filename.
                 * @return The fully resolved absolute file path as a String.
                 */
                String absoluteFilePath() const {
                        return std::filesystem::absolute(_path).string();
                }

                /**
                 * @brief Returns true if the path refers to a regular file.
                 * @return true if the entry is a regular file.
                 */
                bool isFile() const {
                        return std::filesystem::is_regular_file(status());
                }

                /**
                 * @brief Returns true if the path refers to a directory.
                 * @return true if the entry is a directory.
                 */
                bool isDirectory() const {
                        return std::filesystem::is_directory(status());
                }

                /**
                 * @brief Updates the cached file status.
                 * @param force If true, refreshes the status even if already cached.
                 */
                void updateStatus(bool force = false) const {
                        if(!_status.has_value() || force) {
                                _status = std::filesystem::status(_path);
                        }
                        return;
                }

                /**
                 * @brief Returns the file status, updating the cache if needed.
                 * @param forceUpdate If true, forces a refresh of the cached status.
                 * @return The file status.
                 */
                Status status(bool forceUpdate = false) const {
                        updateStatus(forceUpdate);
                        return _status.value();
                }

                /**
                 * @brief Returns the file size in bytes.
                 * @return The size of the file, or 0 if the path is not a regular file.
                 */
                std::uintmax_t size() const {
                        return isFile() ? std::filesystem::file_size(_path) : 0;
                }

                /**
                 * @brief Returns true if the file is readable by the owner.
                 * @return true if the owner-read permission is set.
                 */
                bool isReadable() const {
                        std::error_code ec;
                        auto perms = std::filesystem::status(_path, ec).permissions();
                        return (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
                }

                /**
                 * @brief Returns true if the file is writable by the owner.
                 * @return true if the owner-write permission is set.
                 */
                bool isWritable() const {
                        std::error_code ec;
                        auto perms = std::filesystem::status(_path, ec).permissions();
                        return (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
                }

                /**
                 * @brief Returns true if the file is executable by the owner.
                 * @return true if the owner-exec permission is set.
                 */
                bool isExecutable() const {
                        std::error_code ec;
                        auto perms = std::filesystem::status(_path, ec).permissions();
                        return (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
                }

        private:
                std::filesystem::path           _path;
                mutable std::optional<Status>   _status;

};

PROMEKI_NAMESPACE_END

