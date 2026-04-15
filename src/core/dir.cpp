/**
 * @file      dir.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <filesystem>
#include <fnmatch.h>
#include <promeki/dir.h>
#include <promeki/libraryoptions.h>
#include <promeki/stringlist.h>
#include <promeki/resource.h>

#include <cirf/types.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

/// Builds the full ":/<dir>/<name>" path used for entries returned
/// from a resource-mode Dir listing.
FilePath resourceChild(const String &dirPath, const char *childName) {
        String dir = dirPath;
        if(!dir.isEmpty() && dir.endsWith(String("/"))) {
                dir = dir.left(dir.length() - 1);
        }
        return FilePath(dir + "/" + childName);
}

} // namespace

bool Dir::exists() const {
        const String pathStr = _path.toString();
        if(Resource::isResourcePath(pathStr)) {
                return Resource::findFolder(pathStr) != nullptr;
        }
        std::error_code ec;
        return std::filesystem::is_directory(_path.toStdPath(), ec);
}

bool Dir::isEmpty() const {
        const String pathStr = _path.toString();
        if(Resource::isResourcePath(pathStr)) {
                const cirf_folder_t *f = Resource::findFolder(pathStr);
                if(f == nullptr) return true;
                return f->child_count == 0 && f->file_count == 0;
        }
        std::error_code ec;
        if(!std::filesystem::is_directory(_path.toStdPath(), ec)) return true;
        auto it = std::filesystem::directory_iterator(_path.toStdPath(), ec);
        return it == std::filesystem::directory_iterator();
}

List<FilePath> Dir::entryList() const {
        List<FilePath> result;
        const String pathStr = _path.toString();
        if(Resource::isResourcePath(pathStr)) {
                const cirf_folder_t *f = Resource::findFolder(pathStr);
                if(f == nullptr) return result;
                for(size_t i = 0; i < f->child_count; ++i) {
                        result += resourceChild(pathStr, f->children[i].name);
                }
                for(size_t i = 0; i < f->file_count; ++i) {
                        result += resourceChild(pathStr, f->files[i].name);
                }
                return result;
        }
        std::error_code ec;
        for(const auto &entry : std::filesystem::directory_iterator(_path.toStdPath(), ec)) {
                result += FilePath(entry.path());
        }
        return result;
}

List<FilePath> Dir::entryList(const String &filter) const {
        List<FilePath> result;
        const String pathStr = _path.toString();
        if(Resource::isResourcePath(pathStr)) {
                const cirf_folder_t *f = Resource::findFolder(pathStr);
                if(f == nullptr) return result;
                const char *filterCstr = filter.str().c_str();
                for(size_t i = 0; i < f->child_count; ++i) {
                        if(fnmatch(filterCstr, f->children[i].name, 0) == 0) {
                                result += resourceChild(pathStr,
                                                        f->children[i].name);
                        }
                }
                for(size_t i = 0; i < f->file_count; ++i) {
                        if(fnmatch(filterCstr, f->files[i].name, 0) == 0) {
                                result += resourceChild(pathStr,
                                                        f->files[i].name);
                        }
                }
                return result;
        }
        std::error_code ec;
        for(const auto &entry : std::filesystem::directory_iterator(_path.toStdPath(), ec)) {
                std::string fn = entry.path().filename().string();
                if(fnmatch(filter.str().c_str(), fn.c_str(), 0) == 0) {
                        result += FilePath(entry.path());
                }
        }
        return result;
}

NumNameSeq::List Dir::numberedSequences() const {
        StringList names;
        const String pathStr = _path.toString();
        if(Resource::isResourcePath(pathStr)) {
                const cirf_folder_t *f = Resource::findFolder(pathStr);
                if(f == nullptr) return NumNameSeq::List();
                for(size_t i = 0; i < f->file_count; ++i) {
                        names.pushToBack(String(f->files[i].name));
                }
                return NumNameSeq::parseList(names);
        }
        std::error_code ec;
        auto it = std::filesystem::directory_iterator(_path.toStdPath(), ec);
        if(ec) return NumNameSeq::List();
        for(const auto &entry : it) {
                std::error_code fec;
                if(!entry.is_regular_file(fec)) continue;
                names.pushToBack(String(entry.path().filename().string()));
        }
        return NumNameSeq::parseList(names);
}

Error Dir::mkdir() const {
        if(Resource::isResourcePath(_path.toString())) return Error(Error::ReadOnly);
        std::error_code ec;
        if(std::filesystem::create_directory(_path.toStdPath(), ec)) return Error();
        if(ec) return Error::syserr(ec);
        return Error();
}

Error Dir::mkpath() const {
        if(Resource::isResourcePath(_path.toString())) return Error(Error::ReadOnly);
        std::error_code ec;
        std::filesystem::create_directories(_path.toStdPath(), ec);
        if(ec) return Error::syserr(ec);
        return Error();
}

Error Dir::remove() const {
        if(Resource::isResourcePath(_path.toString())) return Error(Error::ReadOnly);
        std::error_code ec;
        if(std::filesystem::remove(_path.toStdPath(), ec)) return Error();
        if(ec) return Error::syserr(ec);
        return Error();
}

Error Dir::removeRecursively() const {
        if(Resource::isResourcePath(_path.toString())) return Error(Error::ReadOnly);
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
        // Honor the library-wide override first so callers (and the
        // PROMEKI_OPT_TempDir env var that feeds it) can pin temp
        // traffic to a specific directory — typically a disk-backed
        // mount instead of a tmpfs /tmp.  An empty value falls
        // through to the platform default.
        const String override = LibraryOptions::instance()
                .getAs<String>(LibraryOptions::TempDir, String());
        if(!override.isEmpty()) {
                return Dir(FilePath(override));
        }
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
