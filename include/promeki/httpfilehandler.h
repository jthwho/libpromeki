/**
 * @file      httpfilehandler.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/dir.h>
#include <promeki/hashmap.h>
#include <promeki/httphandler.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Static-file-serving HTTP handler.
 * @ingroup network
 *
 * @ref HttpFileHandler maps a request path (relative to the route
 * mount point) onto a file under a configured root @ref Dir, then
 * streams the file back through the response.  Real on-disk paths
 * and compiled-in @c ":/.PROMEKI/" resource paths are both supported
 * because @ref File understands both.
 *
 * Behaviors:
 *  - Only @c GET (and @c HEAD, returning headers only) are honored;
 *    other methods produce 405 with the @c Allow header set.
 *  - Path-traversal hardening: @c "..", absolute, and percent-encoded
 *    @c "%2e%2e" segments resolve to 403.
 *  - MIME type lookup from a small built-in extension table; callers
 *    can register additional types via @ref addMimeType.
 *  - @c ETag generated from size + mtime; @c If-None-Match yields 304.
 *  - @c Last-Modified set from the file's mtime; @c If-Modified-Since
 *    yields 304 when no newer modification has happened.
 *  - Single-range @c Range requests served via 206 + @c Content-Range.
 *  - Bodies are streamed via the response's
 *    @ref HttpResponse::setBodyStream — the entire file is never
 *    held in memory at once.
 *  - Trailing-slash and missing-extension paths fall through to an
 *    optional index file (default @c "index.html").
 *
 * @par Example
 * @code
 * server.route("/static/{path:*}", HttpMethod::Get,
 *              HttpFileHandler::Ptr::takeOwnership(
 *                  new HttpFileHandler(":/PROMEKI/web")));
 * @endcode
 *
 * The @c "{path:*}" placeholder captures everything after the mount
 * prefix into the @c "path" path-param, which the handler resolves
 * relative to its root.  When mounted with no @c "{path}" param,
 * the handler treats the entire request path as the relative key.
 *
 * @par Thread Safety
 * Inherits @ref HttpHandler.  @c serve is intended to be called
 * from the @ref HttpServer's owning EventLoop thread.  The MIME
 * registry and root path are conceptually mutable but should be
 * configured at startup; runtime mutation requires external
 * synchronization.
 */
class HttpFileHandler : public HttpHandler {
                PROMEKI_SHARED_DERIVED(HttpHandler, HttpFileHandler)
        public:
                /** @brief Default index file served for directory-style requests. */
                static const String DefaultIndexFile;

                /** @brief Read chunk size used when streaming large files. */
                static constexpr int64_t StreamChunkBytes = 64 * 1024;

                /**
                 * @brief Constructs a handler rooted at @p root.
                 *
                 * @p root may be a real on-disk directory or a
                 * @c ":/..." resource path; @ref File transparently
                 * understands both.
                 */
                explicit HttpFileHandler(const Dir &root);

                /** @brief Convenience: construct from a path string. */
                explicit HttpFileHandler(const String &rootPath);

                /** @brief Returns the configured serving root. */
                const Dir &root() const { return _root; }

                /**
                 * @brief Sets the path-param name to consume as the
                 *        request-relative file path.
                 *
                 * Defaults to @c "path", matching the canonical
                 * @c "{path:*}" placeholder used in route patterns.
                 * Set to an empty string to instead use the entire
                 * @c HttpRequest::path() as the relative key.
                 */
                void setPathParamName(const String &name) { _pathParam = name; }

                /**
                 * @brief Sets the index file served for directory paths.
                 *
                 * Pass an empty string to disable the index lookup
                 * (directory requests then return 403 unless the
                 * caller has supplied an explicit file path).
                 */
                void setIndexFile(const String &name) { _indexFile = name; }

                /**
                 * @brief Registers an additional extension → MIME mapping.
                 *
                 * The built-in table covers the most common web
                 * formats; use this to add organization-specific
                 * extensions or override defaults.  @p ext is matched
                 * case-insensitively without a leading dot.
                 */
                void addMimeType(const String &ext, const String &mime);

                /**
                 * @brief Whether to allow directory listings when no index file matches.
                 *
                 * Default @c false.  When enabled, the handler emits
                 * a minimal HTML index for browser convenience.  Off
                 * by default because directory listings are easy to
                 * mis-deploy as an unintended information disclosure.
                 */
                void setListDirectories(bool enable) { _listDirs = enable; }

                /**
                 * @brief Resolves a request to its on-root file path.
                 *
                 * Public so route patterns can compose: e.g. a custom
                 * handler that wraps the file handler with auth checks
                 * can ask the same resolver to find the file before
                 * deciding whether to serve it.  Returns an empty
                 * String on bad/forbidden inputs.
                 */
                String resolveRequestPath(const HttpRequest &request) const;

                /** @brief Looks up the MIME type for a path's extension. */
                String mimeType(const String &filePath) const;

                /** @brief Implements @ref HttpHandler::serve. */
                void serve(const HttpRequest &request, HttpResponse &response) override;

        private:
                Dir                     _root;
                String                  _pathParam = "path";
                String                  _indexFile = DefaultIndexFile;
                bool                    _listDirs = false;
                HashMap<String, String> _extraMime;

                static String etagFor(int64_t size, int64_t mtimeEpoch);
                static String httpDateFor(int64_t mtimeEpoch);
                static bool   isPathSafe(const String &relPath);

                bool serveRange(const HttpRequest &request, HttpResponse &response, const String &fullPath,
                                int64_t fileSize, const String &mime);
};

PROMEKI_NAMESPACE_END
