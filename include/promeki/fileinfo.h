/**
 * @file      fileinfo.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class FileInfo {
        public:
                using Status = std::filesystem::file_status;

                FileInfo(const String &filePath) : _path(filePath.stds()) {

                }
                bool exists() const {
                        return std::filesystem::exists(status());
                }

                String fileName() const {
                        return _path.filename().string();
                }

                String baseName() const {
                        return _path.stem().string();
                }

                String suffix() const {
                        return _path.extension().string().substr(1); // Remove leading '.'
                }

                String absolutePath() const {
                        return _path.parent_path().string();
                }

                String absoluteFilePath() const {
                        return std::filesystem::absolute(_path).string();
                }

                bool isFile() const {
                        return std::filesystem::is_regular_file(status());
                }

                bool isDirectory() const {
                        return std::filesystem::is_directory(status());
                }

                void updateStatus(bool force = false) const {
                        if(!_status.has_value() || force) { 
                                _status = std::filesystem::status(_path);
                        }
                        return;
                }

                Status status(bool forceUpdate = false) const {
                        updateStatus(forceUpdate);
                        return _status.value();
                }

                std::uintmax_t size() const {
                        return isFile() ? std::filesystem::file_size(_path) : 0;
                }

                bool isReadable() const {
                        std::error_code ec;
                        auto perms = std::filesystem::status(_path, ec).permissions();
                        return (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
                }

                bool isWritable() const {
                        std::error_code ec;
                        auto perms = std::filesystem::status(_path, ec).permissions();
                        return (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
                }

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

