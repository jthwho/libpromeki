/*****************************************************************************
 * fileinfo.h
 * April 28, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <filesystem>
#include <optional>
#include <promeki/string.h>

namespace promeki {


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

} // namespace promeki

