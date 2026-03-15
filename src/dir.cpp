/**
 * @file      dir.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <filesystem>
#include <fnmatch.h>
#include <promeki/core/dir.h>

PROMEKI_NAMESPACE_BEGIN

bool Dir::exists() const {
        std::error_code ec;
        return std::filesystem::is_directory(_path.toStdPath(), ec);
}

bool Dir::isEmpty() const {
        std::error_code ec;
        if(!std::filesystem::is_directory(_path.toStdPath(), ec)) return true;
        auto it = std::filesystem::directory_iterator(_path.toStdPath(), ec);
        return it == std::filesystem::directory_iterator();
}

List<FilePath> Dir::entryList() const {
        List<FilePath> result;
        std::error_code ec;
        for(const auto &entry : std::filesystem::directory_iterator(_path.toStdPath(), ec)) {
                result += FilePath(entry.path());
        }
        return result;
}

List<FilePath> Dir::entryList(const String &filter) const {
        List<FilePath> result;
        std::error_code ec;
        for(const auto &entry : std::filesystem::directory_iterator(_path.toStdPath(), ec)) {
                std::string fn = entry.path().filename().string();
                if(fnmatch(filter.str().c_str(), fn.c_str(), 0) == 0) {
                        result += FilePath(entry.path());
                }
        }
        return result;
}

Error Dir::mkdir() const {
        std::error_code ec;
        if(std::filesystem::create_directory(_path.toStdPath(), ec)) return Error();
        if(ec) return Error::syserr(ec);
        return Error();
}

Error Dir::mkpath() const {
        std::error_code ec;
        std::filesystem::create_directories(_path.toStdPath(), ec);
        if(ec) return Error::syserr(ec);
        return Error();
}

Error Dir::remove() const {
        std::error_code ec;
        if(std::filesystem::remove(_path.toStdPath(), ec)) return Error();
        if(ec) return Error::syserr(ec);
        return Error();
}

Error Dir::removeRecursively() const {
        std::error_code ec;
        std::filesystem::remove_all(_path.toStdPath(), ec);
        if(ec) return Error::syserr(ec);
        return Error();
}

Dir Dir::current() {
        std::error_code ec;
        return Dir(FilePath(std::filesystem::current_path(ec)));
}

Dir Dir::home() {
        const char *h = std::getenv("HOME");
        if(h != nullptr) return Dir(FilePath(h));
        return Dir();
}

Dir Dir::temp() {
        std::error_code ec;
        return Dir(FilePath(std::filesystem::temp_directory_path(ec)));
}

Error Dir::setCurrent(const FilePath &path) {
        std::error_code ec;
        std::filesystem::current_path(path.toStdPath(), ec);
        if(ec) return Error::syserr(ec);
        return Error();
}

PROMEKI_NAMESPACE_END
