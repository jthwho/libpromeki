/**
 * @file      filepath.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <filesystem>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/result.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Simple value type wrapping std::filesystem::path.
 * @ingroup io
 *
 * Provides a convenient interface for path decomposition, joining,
 * and filesystem queries. This is a simple data object — always
 * copied by value, no shared ownership.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 */
class FilePath {
        public:
                /** @brief Constructs an empty file path. */
                FilePath() = default;

                /**
                 * @brief Constructs a FilePath from a String.
                 * @param path The path string.
                 */
                FilePath(const String &path) : _path(path.str()) {}

                /**
                 * @brief Constructs a FilePath from a C string.
                 * @param path The path string.
                 */
                FilePath(const char *path) : _path(path) {}

                /**
                 * @brief Constructs a FilePath from a std::filesystem::path.
                 * @param path The filesystem path.
                 */
                FilePath(const std::filesystem::path &path) : _path(path) {}

                /**
                 * @brief Returns true if the path is empty.
                 * @return true if the path has no components.
                 */
                bool isEmpty() const { return _path.empty(); }

                /**
                 * @brief Returns the filename component (including extension).
                 * @return The filename as a String.
                 */
                String fileName() const { return _path.filename().string(); }

                /**
                 * @brief Returns the filename without its extension.
                 * @return The stem as a String.
                 */
                String baseName() const { return _path.stem().string(); }

                /**
                 * @brief Returns the file extension without the leading dot.
                 * @return The suffix as a String (e.g. "png", "wav").
                 */
                String suffix() const {
                        auto ext = _path.extension().string();
                        if (ext.size() > 1) return ext.substr(1);
                        return String();
                }

                /**
                 * @brief Returns the complete suffix (all extensions).
                 *
                 * For "archive.tar.gz", returns "tar.gz".
                 * @return The complete suffix as a String.
                 */
                String completeSuffix() const {
                        String fn = _path.filename().string();
                        auto   pos = fn.find('.');
                        if (pos == String::npos || pos == 0) return String();
                        return fn.substr(pos + 1);
                }

                /**
                 * @brief Returns the parent directory as a FilePath.
                 * @return The parent directory path.
                 */
                FilePath parent() const { return FilePath(_path.parent_path()); }

                /**
                 * @brief Joins this path with another path component.
                 * @param other The path component to append.
                 * @return A new FilePath with the joined result.
                 */
                FilePath join(const FilePath &other) const { return FilePath(_path / other._path); }

                /**
                 * @brief Joins this path with another path component.
                 * @param other The path component to append.
                 * @return A new FilePath with the joined result.
                 */
                FilePath operator/(const FilePath &other) const { return join(other); }

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
                FilePath operator/(const char *other) const { return FilePath(_path / std::filesystem::path(other)); }

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
                bool isAbsolute() const { return _path.is_absolute(); }

                /**
                 * @brief Returns true if the path is relative.
                 * @return true if the path is a relative path.
                 */
                bool isRelative() const { return _path.is_relative(); }

                /**
                 * @brief Returns the absolute form of this path.
                 * @return A FilePath with the absolute path.
                 */
                FilePath absolutePath() const {
                        std::error_code ec;
                        return FilePath(std::filesystem::absolute(_path, ec));
                }

                /**
                 * @brief Returns the canonical (real) path with symlinks resolved.
                 *
                 * Wraps @c std::filesystem::canonical.  The path must
                 * exist on the filesystem; if resolution fails (path
                 * missing, symlink loop, permission error, etc.)
                 * returns the original path with @c Error::Invalid.
                 *
                 * @return The canonical FilePath on success, or
                 *         @c Error::Invalid with the original path on
                 *         failure.
                 */
                Result<FilePath> canonicalPath() const {
                        std::error_code ec;
                        auto            r = std::filesystem::canonical(_path, ec);
                        if (ec) return Result<FilePath>(*this, Error::Invalid);
                        return Result<FilePath>(FilePath(r), Error::Ok);
                }

                /**
                 * @brief Returns this path expressed relative to @p base.
                 *
                 * Wraps @c std::filesystem::relative.  Returns
                 * @c Error::Invalid when the computation fails (different
                 * roots, permission error, etc.) or yields an empty path,
                 * with the failed @c FilePath value carried in the
                 * Result's first slot for callers that want a fallback.
                 *
                 * @param base The directory to compute a relative path from.
                 * @return The relative FilePath on success, or
                 *         @c Error::Invalid with the original path on failure.
                 */
                Result<FilePath> relativeTo(const FilePath &base) const {
                        std::error_code ec;
                        auto            r = std::filesystem::relative(_path, base._path, ec);
                        if (ec || r.empty()) return Result<FilePath>(*this, Error::Invalid);
                        return Result<FilePath>(FilePath(r), Error::Ok);
                }

                /// @name Symbolic / pseudo-symbolic links
                ///
                /// libpromeki supports two kinds of "link" pointers:
                ///
                ///   - **OS symlink** — a real filesystem symlink, as
                ///     exposed by @c std::filesystem::is_symlink and
                ///     @c read_symlink.  Available on POSIX and on
                ///     modern Windows with the right privileges.
                ///   - **Pseudo-symlink** — a small, regular text file
                ///     whose first line is exactly the magic marker
                ///     @c "#!/promeki/symlink" and whose remaining
                ///     content (trimmed of ASCII whitespace) is the
                ///     link target path.  This lets the library carry
                ///     a "name → real file" indirection on hosts /
                ///     filesystems that don't support real symlinks
                ///     (FAT on a USB stick, a CI artifact bundle
                ///     unpacked as regular files, an LFS-backed
                ///     repository where symlinks are not preserved).
                ///
                /// Pseudo-symlinks require an explicit magic header
                /// rather than a heuristic content check so they
                /// can't be confused with unrelated short text files
                /// that happen to look path-shaped (a @c VERSION
                /// file, a tag, etc.).  Use @ref writePseudoSymlink
                /// to create one without having to remember the
                /// header text.
                /// @{

                /**
                 * @brief Maximum byte length of a pseudo-symlink file.
                 *
                 * Files larger than this are not considered pseudo-
                 * symlinks even if they carry the magic header.
                 */
                static constexpr size_t kPseudoSymlinkMaxBytes = 4096;

                /**
                 * @brief Magic first line marking a file as a
                 *        pseudo-symlink.
                 *
                 * The first line of a pseudo-symlink file must be
                 * exactly this string, optionally followed by a
                 * trailing CR / LF.  Shaped like a shebang so a
                 * stray @c cat on the file is obviously a pointer
                 * rather than an opaque blob.
                 */
                static constexpr const char *kPseudoSymlinkMagic = "#!/promeki/symlink";

                /**
                 * @brief Returns @c true if this path is an OS-level
                 *        symbolic link.
                 *
                 * Uses @c std::filesystem::symlink_status so a broken
                 * symlink (target does not exist) still returns @c true.
                 * Returns @c false on any stat error or for paths that
                 * do not exist at all.
                 */
                bool isSymlink() const {
                        std::error_code ec;
                        auto            st = std::filesystem::symlink_status(_path, ec);
                        if (ec) return false;
                        return std::filesystem::is_symlink(st);
                }

                /**
                 * @brief Returns @c true if this path is a pseudo-symlink.
                 *
                 * A pseudo-symlink is a regular (non-symlink, non-
                 * directory) file that satisfies every condition below:
                 *
                 *   - Size is in @c [1, @ref kPseudoSymlinkMaxBytes] bytes.
                 *   - The first line is exactly @ref kPseudoSymlinkMagic
                 *     (followed by a newline or end-of-file).
                 *   - The remaining payload, trimmed of leading /
                 *     trailing ASCII whitespace, is non-empty and
                 *     contains no NUL or ASCII control characters.
                 *
                 * Implementation lives in @c filepath.cpp because it
                 * has to read the file body.
                 */
                bool isPseudoSymlink() const;

                /**
                 * @brief Returns @c true if this path is either an OS
                 *        symlink or a pseudo-symlink.
                 */
                bool isLink() const { return isSymlink() || isPseudoSymlink(); }

                /**
                 * @brief Reads an OS symlink's target (verbatim).
                 *
                 * Wraps @c std::filesystem::read_symlink.  The returned
                 * FilePath is exactly what the symlink stores — which
                 * is often a relative path.  Use @ref resolveLink (or
                 * @ref canonicalPath) when you want the fully-resolved
                 * absolute target.
                 *
                 * @return The link's stored target on success, or
                 *         @c Error::Invalid (with @c *this) on any
                 *         failure (not a symlink, permission error,
                 *         broken link with an unreadable target).
                 */
                Result<FilePath> readSymlink() const {
                        std::error_code ec;
                        auto            t = std::filesystem::read_symlink(_path, ec);
                        if (ec) return Result<FilePath>(*this, Error::Invalid);
                        return Result<FilePath>(FilePath(t), Error::Ok);
                }

                /**
                 * @brief Reads a pseudo-symlink's target.
                 *
                 * Verifies the magic header, then returns the
                 * remaining trimmed payload as a FilePath.  The path
                 * is returned verbatim — relative paths are @em not
                 * resolved against this file's parent directory.
                 * See @ref resolveLink for that.
                 *
                 * @return The stored target on success, or
                 *         @c Error::Invalid (with @c *this) when the
                 *         file is missing, unreadable, larger than
                 *         @ref kPseudoSymlinkMaxBytes, missing the
                 *         magic header, or empty after the header.
                 */
                Result<FilePath> readPseudoSymlink() const;

                /**
                 * @brief Writes @p target as a pseudo-symlink at @c *this.
                 *
                 * Creates a regular file containing the magic header
                 * line followed by @p target.  Overwrites any
                 * existing file at @c *this.  Parent directory must
                 * already exist — use @ref Dir::mkpath to stage it
                 * first if needed.
                 *
                 * @param target Relative or absolute path the
                 *               pseudo-symlink should point at.
                 * @return @c Error::Ok on success, or @c Error::Invalid
                 *         on any I/O failure (parent missing,
                 *         permission denied, oversize target).
                 */
                Error writePseudoSymlink(const FilePath &target) const;

                /**
                 * @brief Reads either kind of link's target.
                 *
                 * Tries @ref readSymlink first; if @c *this is not an
                 * OS symlink, tries @ref readPseudoSymlink.  Returns
                 * the first hit verbatim (no relative-path resolution).
                 *
                 * @return The stored target on success, or
                 *         @c Error::Invalid (with @c *this) when the
                 *         path is neither link kind.
                 */
                Result<FilePath> readLink() const {
                        if (isSymlink()) return readSymlink();
                        if (isPseudoSymlink()) return readPseudoSymlink();
                        return Result<FilePath>(*this, Error::Invalid);
                }

                /**
                 * @brief Follows a chain of OS / pseudo-symlinks to its
                 *        final target.
                 *
                 * At each hop, relative targets are resolved against
                 * the link file's parent directory (mirroring OS
                 * symlink semantics).  The walk stops when the
                 * current path is neither an OS symlink nor a
                 * pseudo-symlink, or after @p maxHops iterations.
                 * Visited paths are tracked to break loops.
                 *
                 * @param maxHops Maximum number of indirection hops
                 *                to follow before giving up.  Default
                 *                @c 16 mirrors the Linux kernel's
                 *                @c MAXSYMLINKS.
                 * @return The resolved FilePath on success (which may
                 *         not exist — a broken link still returns
                 *         what it points at), or @c Error::Invalid
                 *         when the loop guard trips or a hop fails
                 *         to resolve.  When @c *this is not a link
                 *         to start with, returns @c *this with
                 *         @c Error::Ok.
                 */
                Result<FilePath> resolveLink(int maxHops = 16) const;

                /// @}

                /**
                 * @brief Converts the path to a String.
                 * @return The path as a String.
                 */
                String toString() const { return _path.string(); }

                /**
                 * @brief Returns the underlying std::filesystem::path.
                 * @return A const reference to the internal path.
                 */
                const std::filesystem::path &toStdPath() const { return _path; }

                /** @brief Equality comparison. */
                bool operator==(const FilePath &other) const { return _path == other._path; }

                /** @brief Inequality comparison. */
                bool operator!=(const FilePath &other) const { return _path != other._path; }

                /** @brief Less-than comparison for ordered containers. */
                bool operator<(const FilePath &other) const { return _path < other._path; }

        private:
                std::filesystem::path _path;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::FilePath);

#endif // PROMEKI_ENABLE_CORE
