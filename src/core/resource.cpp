/**
 * @file      resource.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/resource.h>
#include <promeki/map.h>
#include <promeki/mutex.h>

#include <cirf/runtime.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /**
 * cirf_mount() does NOT copy the prefix string — it stores the
 * pointer directly. We therefore have to keep prefix strings alive
 * for the lifetime of the mount. The PrefixRegistry holds a stable
 * std::string per registered prefix and hands cirf_mount() a pointer
 * into that storage.
 *
 * Access to the registry is guarded by a mutex so that
 * registerRoot()/unregisterRoot() are thread-safe relative to one
 * another. The actual cirf_mounts list is also touched only under
 * this mutex; lookup paths (cirf_resolve_file etc.) read the list
 * lock-free, which is fine because nothing in the runtime mutates
 * the list during a lookup.
 */
        class PrefixRegistry {
                public:
                        /// Returns a stable C string for @p prefix, allocating
                        /// the storage on first use. The returned pointer
                        /// remains valid until @c remove() is called for the
                        /// same key.
                        const char *intern(const String &prefix) {
                                // std::string is used here (rather than
                                // promeki::String) because we need a container
                                // whose c_str() never moves and outlives the
                                // map's iterators across rehashes. std::map
                                // gives us iterator stability for both keys
                                // and values.
                                auto [it, inserted] = _entries.try_emplace(prefix.str(), prefix.str());
                                return it->second.c_str();
                        }

                        /// Drops the stored copy for @p prefix, if any.
                        void remove(const String &prefix) { _entries.erase(prefix.str()); }

                private:
                        std::map<std::string, std::string> _entries;
        };

        PrefixRegistry &prefixRegistry() {
                static PrefixRegistry r;
                return r;
        }

        Mutex &registryMutex() {
                static Mutex m;
                return m;
        }

        /// Walks the global cirf mount list and returns the first matching
        /// folder for @p path (which must already have the ":/" marker
        /// stripped). cirf has @c cirf_resolve_file() but no
        /// @c cirf_resolve_folder(), so we replicate the same prefix-walk
        /// logic here for folder lookups.
        const cirf_folder_t *resolveFolder(const char *path) {
                if (path == nullptr) return nullptr;
                for (cirf_mount_t *m = cirf_mounts; m != nullptr; m = m->next) {
                        size_t prefixLen = ::strlen(m->prefix);
                        if (::strncmp(path, m->prefix, prefixLen) == 0) {
                                const cirf_folder_t *f = cirf_find_folder(m->root, path + prefixLen);
                                if (f != nullptr) return f;
                        }
                }
                return nullptr;
        }

        /// Builds the full ":/<dirPath>/<name>" path for an entry inside a
        /// directory listing.
        FilePath makeChildPath(const String &resourceDirPath, const char *childName) {
                // resourceDirPath is the user-supplied directory path; preserve
                // it as-is so the result reads naturally in the caller's
                // context.
                String dir = resourceDirPath;
                // Trim a single trailing slash if present so the join below
                // produces exactly one separator.
                if (!dir.isEmpty() && dir.endsWith(String("/"))) {
                        dir = dir.left(dir.length() - 1);
                }
                return FilePath(dir + "/" + childName);
        }

} // namespace

bool Resource::isResourcePath(const String &path) {
        return path.startsWith(Prefix);
}

String Resource::stripPrefix(const String &path) {
        if (!isResourcePath(path)) return String();
        return path.mid(2);
}

const cirf_file_t *Resource::findFile(const String &path) {
        String virt = isResourcePath(path) ? stripPrefix(path) : path;
        return cirf_resolve_file(virt.cstr());
}

const cirf_folder_t *Resource::findFolder(const String &path) {
        String virt = isResourcePath(path) ? stripPrefix(path) : path;
        return resolveFolder(virt.cstr());
}

bool Resource::exists(const String &path) {
        return findFile(path) != nullptr || findFolder(path) != nullptr;
}

Buffer Resource::data(const String &path, Error *err) {
        const cirf_file_t *file = findFile(path);
        if (file == nullptr) {
                if (err != nullptr) *err = Error(Error::NotExist);
                return Buffer();
        }
        // Non-owning view over the .rodata bytes. Buffer::wrap leaves
        // _size at 0 — we set it explicitly to the resource size so
        // callers can read size() directly.
        Buffer buf = Buffer::wrap(const_cast<unsigned char *>(file->data), file->size, /*align*/ 0);
        buf.setSize(file->size);
        if (err != nullptr) *err = Error(Error::Ok);
        return buf;
}

String Resource::mime(const String &path) {
        const cirf_file_t *file = findFile(path);
        if (file == nullptr || file->mime == nullptr) return String();
        return String(file->mime);
}

size_t Resource::size(const String &path) {
        const cirf_file_t *file = findFile(path);
        return file != nullptr ? file->size : 0;
}

List<FilePath> Resource::listFiles(const String &path, Error *err) {
        List<FilePath>       result;
        const cirf_folder_t *folder = findFolder(path);
        if (folder == nullptr) {
                if (err != nullptr) *err = Error(Error::NotExist);
                return result;
        }
        for (size_t i = 0; i < folder->file_count; ++i) {
                result += makeChildPath(path, folder->files[i].name);
        }
        if (err != nullptr) *err = Error(Error::Ok);
        return result;
}

List<FilePath> Resource::listFolders(const String &path, Error *err) {
        List<FilePath>       result;
        const cirf_folder_t *folder = findFolder(path);
        if (folder == nullptr) {
                if (err != nullptr) *err = Error(Error::NotExist);
                return result;
        }
        for (size_t i = 0; i < folder->child_count; ++i) {
                result += makeChildPath(path, folder->children[i]->name);
        }
        if (err != nullptr) *err = Error(Error::Ok);
        return result;
}

List<FilePath> Resource::listEntries(const String &path, Error *err) {
        List<FilePath>       result;
        const cirf_folder_t *folder = findFolder(path);
        if (folder == nullptr) {
                if (err != nullptr) *err = Error(Error::NotExist);
                return result;
        }
        for (size_t i = 0; i < folder->child_count; ++i) {
                result += makeChildPath(path, folder->children[i]->name);
        }
        for (size_t i = 0; i < folder->file_count; ++i) {
                result += makeChildPath(path, folder->files[i].name);
        }
        if (err != nullptr) *err = Error(Error::Ok);
        return result;
}

Error Resource::registerRoot(const cirf_folder_t *root, const String &prefix) {
        if (root == nullptr) return Error(Error::InvalidArgument);
        Mutex::Locker lock(registryMutex());
        // If a mount with this prefix already exists pointing at the
        // same root, do nothing. If it exists pointing at a different
        // root, remove it first so the new registration replaces it.
        for (cirf_mount_t *m = cirf_mounts; m != nullptr; m = m->next) {
                if (::strcmp(m->prefix, prefix.cstr()) == 0) {
                        if (m->root == root) return Error(Error::Ok);
                        cirf_unmount(m->prefix);
                        prefixRegistry().remove(prefix);
                        break;
                }
        }
        const char *stablePrefix = prefixRegistry().intern(prefix);
        if (cirf_mount(stablePrefix, root) != 0) {
                prefixRegistry().remove(prefix);
                return Error(Error::NoMem);
        }
        return Error(Error::Ok);
}

void Resource::unregisterRoot(const String &prefix) {
        Mutex::Locker lock(registryMutex());
        cirf_unmount(prefix.cstr());
        prefixRegistry().remove(prefix);
}

PROMEKI_NAMESPACE_END
