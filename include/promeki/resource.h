/**
 * @file      resource.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/filepath.h>
#include <promeki/util.h>

// Forward declarations so this header does not pull in <cirf/types.h>
// — which keeps cirf an internal implementation detail of promeki
// and lets the cirf_runtime link stay PRIVATE. Consumers that want
// to dereference the pointers returned by findFile()/findFolder()
// can include <cirf/types.h> themselves; the header is installed
// alongside the libpromeki thirdparty headers.
struct cirf_file;
struct cirf_folder;
typedef struct cirf_file   cirf_file_t;
typedef struct cirf_folder cirf_folder_t;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Compiled-in resource filesystem (Qt-style ":/...").
 * @ingroup io
 *
 * Resources are baked into the binary at build time via the
 * [cirf](https://github.com/jthwho/cirf) code generator and accessed
 * at runtime through this class. Lookup is done by virtual path; the
 * same code paths in @c File, @c FileIODevice and @c Dir transparently
 * serve resources whenever they are given a path beginning with
 * @c ":/" — so most consumers do not need to call into this class
 * directly.
 *
 * @par Path scheme
 * Resource paths follow Qt's @c QResource convention: a leading
 * @c ":/" marker followed by the virtual path of the resource within
 * the registered cirf root. For example, libpromeki's built-in
 * FiraCode font is reachable at
 * @code
 * ":/.PROMEKI/fonts/FiraCodeNerdFontMono-Regular.ttf"
 * @endcode
 *
 * The @c .PROMEKI prefix marks built-in libpromeki resources as a
 * hidden directory and avoids collisions with paths that consuming
 * applications choose for their own resource sets.
 *
 * @par Multiple resource sets
 * Each call to @c cirf_add_resources() in CMake produces an
 * independent @c cirf_folder_t tree. Use @c registerRoot() to mount
 * additional roots from a consuming application; use the
 * @c PROMEKI_REGISTER_RESOURCES macro to mount one automatically at
 * static-init time.
 *
 * @par Lifetime and ownership
 * cirf data lives in the executable's @c .rodata segment for the
 * lifetime of the program. Pointers returned by @c findFile() /
 * @c findFolder() and @c Buffer views returned by @c data() are
 * non-owning and never need freeing.
 *
 * @par Thread Safety
 * All public methods are static and operate against a global
 * registry that is mutated only at static-init time (via
 * @c PROMEKI_REGISTER_RESOURCES) or via explicit @c registerRoot
 * calls.  Lookups (@c findFile, @c findFolder, @c data) are safe
 * to call concurrently from any thread once registration is done.
 * Calling @c registerRoot at runtime requires external
 * synchronization — typical usage registers all roots before
 * worker threads start.
 */
class Resource {
        public:
                /// @brief Marker prefix for resource paths (":/").
                static constexpr const char *Prefix = ":/";

                /**
                 * @brief Returns true if @p path begins with the resource prefix.
                 * @param path Any path string.
                 */
                static bool isResourcePath(const String &path);

                /**
                 * @brief Strips the leading @c ":/" from @p path.
                 *
                 * Returns an empty string if @p path is not a resource path.
                 * Does not collapse extra slashes or normalise the result.
                 *
                 * @param path A resource path beginning with @c ":/".
                 */
                static String stripPrefix(const String &path);

                /**
                 * @brief Looks up a resource file by virtual path.
                 *
                 * @p path may be a full @c ":/foo/bar" resource path or just
                 * the @c "foo/bar" suffix already without the marker.
                 * Returns @c nullptr if no registered root contains the
                 * file. The returned pointer is valid for the entire
                 * program lifetime — it points into @c .rodata.
                 */
                static const cirf_file_t *findFile(const String &path);

                /**
                 * @brief Looks up a resource folder by virtual path.
                 *
                 * Same path-form rules as @c findFile(). Returns @c nullptr
                 * if no folder matches in any registered root.
                 */
                static const cirf_folder_t *findFolder(const String &path);

                /**
                 * @brief Returns true if @p path resolves to a file or folder.
                 */
                static bool exists(const String &path);

                /**
                 * @brief Returns a non-owning Buffer view over the file's bytes.
                 *
                 * The view points directly into the compiled-in resource
                 * data — no allocation, no copy. The buffer's logical
                 * size is set to the resource size.
                 *
                 * @param path Resource path.
                 * @param err  Optional error output. Set to
                 *             @c Error::NotExist if the path is not found.
                 * @return A valid Buffer view on success, or an invalid
                 *         (default-constructed) Buffer on failure.
                 */
                static Buffer data(const String &path, Error *err = nullptr);

                /**
                 * @brief Returns the MIME type set by cirf at generation time.
                 *
                 * cirf guesses the MIME type from the file extension; the
                 * value can also be overridden in the resource JSON config.
                 * Returns an empty string if @p path is not found.
                 */
                static String mime(const String &path);

                /**
                 * @brief Returns the size in bytes of a resource file.
                 *
                 * Returns 0 if @p path is not found.
                 */
                static size_t size(const String &path);

                /**
                 * @brief Lists all files in a resource directory (non-recursive).
                 *
                 * The returned @c FilePath entries are full @c ":/..."
                 * paths suitable for passing back to @c File or any other
                 * resource-aware API.
                 *
                 * @param path Resource directory path.
                 * @param err  Optional error output. Set to
                 *             @c Error::NotExist if the directory is
                 *             not found.
                 */
                static List<FilePath> listFiles(const String &path, Error *err = nullptr);

                /**
                 * @brief Lists all subfolders in a resource directory (non-recursive).
                 */
                static List<FilePath> listFolders(const String &path, Error *err = nullptr);

                /**
                 * @brief Lists all entries (files + folders) in a resource directory.
                 */
                static List<FilePath> listEntries(const String &path, Error *err = nullptr);

                /**
                 * @brief Registers a generated cirf root under an optional prefix.
                 *
                 * With an empty prefix the root's contents are merged
                 * directly under @c ":/" — file lookups walk the root
                 * with their full virtual paths. With a prefix like
                 * @c "myapp" a file inside the root with virtual path
                 * @c "fonts/foo.ttf" becomes reachable at
                 * @c ":/myapp/fonts/foo.ttf" — the prefix is stripped
                 * from the lookup path before walking the root.
                 *
                 * Backed by @c cirf_mount(). The prefix string is
                 * copied internally so the caller does not need to
                 * keep the original alive. Calling @c registerRoot()
                 * twice with the same prefix and the same root is a
                 * no-op; calling it with the same prefix and a
                 * different root replaces the previous mount.
                 *
                 * @return @c Error::Ok on success, otherwise an error.
                 */
                static Error registerRoot(const cirf_folder_t *root, const String &prefix = String());

                /**
                 * @brief Unregisters a previously-registered root by prefix.
                 *
                 * If no mount with the given prefix exists, this is a
                 * no-op.
                 */
                static void unregisterRoot(const String &prefix);
};

/**
 * @def PROMEKI_REGISTER_RESOURCES(NAME, PREFIX)
 * @brief Registers a cirf-generated resource set at static-init time.
 *
 * Expands to a file-scope object whose constructor calls
 * @c Resource::registerRoot(&NAME##_root, PREFIX). Use it AFTER
 * including the cirf-generated header so that @c <NAME>_root is in
 * scope. The macro must appear at namespace scope.
 *
 * @par Example
 * @code
 * #include <promeki/resource.h>
 * #include "my_resources.h"
 *
 * PROMEKI_REGISTER_RESOURCES(my_resources, "myapp")
 * // After this, ":/myapp/foo/bar.txt" resolves to my_resources_root's
 * // "foo/bar.txt".
 * @endcode
 */
#define PROMEKI_REGISTER_RESOURCES(NAME, PREFIX)                                                                       \
        namespace {                                                                                                    \
                struct PROMEKI_CONCAT(_promeki_resreg_, NAME) {                                                        \
                                PROMEKI_CONCAT(_promeki_resreg_, NAME)() {                                             \
                                        ::promeki::Resource::registerRoot(&NAME##_root, PREFIX);                       \
                                }                                                                                      \
                };                                                                                                     \
                static PROMEKI_CONCAT(_promeki_resreg_, NAME) PROMEKI_CONCAT(_promeki_resreg_inst_, NAME);             \
        }

PROMEKI_NAMESPACE_END
