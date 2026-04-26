/**
 * @file      httpfilehandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpfilehandler.h>
#include <promeki/file.h>
#include <promeki/fileinfo.h>
#include <promeki/url.h>
#include <promeki/datetime.h>
#include <promeki/resource.h>
#include <promeki/stringlist.h>
#include <promeki/iodevice.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

PROMEKI_NAMESPACE_BEGIN

const String HttpFileHandler::DefaultIndexFile{"index.html"};

namespace {

// Tiny well-known MIME table.  Kept short on purpose: the goal is
// "no surprises in the browser" not "complete IANA registry".
struct MimeRow {
        const char *ext;
        const char *mime;
};

static constexpr MimeRow kMimeTable[] = {
        { "html", "text/html; charset=utf-8" },
        { "htm",  "text/html; charset=utf-8" },
        { "css",  "text/css; charset=utf-8" },
        { "js",   "application/javascript; charset=utf-8" },
        { "mjs",  "application/javascript; charset=utf-8" },
        { "json", "application/json" },
        { "txt",  "text/plain; charset=utf-8" },
        { "md",   "text/markdown; charset=utf-8" },
        { "xml",  "application/xml; charset=utf-8" },
        { "svg",  "image/svg+xml" },
        { "png",  "image/png" },
        { "jpg",  "image/jpeg" },
        { "jpeg", "image/jpeg" },
        { "gif",  "image/gif" },
        { "webp", "image/webp" },
        { "ico",  "image/x-icon" },
        { "bmp",  "image/bmp" },
        { "wav",  "audio/wav" },
        { "mp3",  "audio/mpeg" },
        { "ogg",  "audio/ogg" },
        { "flac", "audio/flac" },
        { "mp4",  "video/mp4" },
        { "webm", "video/webm" },
        { "mov",  "video/quicktime" },
        { "pdf",  "application/pdf" },
        { "zip",  "application/zip" },
        { "gz",   "application/gzip" },
        { "wasm", "application/wasm" },
};

// Return the canonical extension for @p path, lowercased and without
// the leading dot.  Empty string when no extension.
static String extOf(const String &path) {
        const size_t dot = path.rfind('.');
        const size_t slash = path.rfind('/');
        if(dot == String::npos) return String();
        if(slash != String::npos && slash > dot) return String();
        return path.mid(dot + 1).toLower();
}

} // anonymous namespace

// ============================================================
// Construction
// ============================================================

HttpFileHandler::HttpFileHandler(const Dir &root) : _root(root) {}

HttpFileHandler::HttpFileHandler(const String &rootPath) :
        _root(FilePath(rootPath)) {}

void HttpFileHandler::addMimeType(const String &ext, const String &mime) {
        _extraMime.insert(ext.toLower(), mime);
}

String HttpFileHandler::mimeType(const String &filePath) const {
        const String ext = extOf(filePath);
        if(ext.isEmpty()) return "application/octet-stream";

        const String custom = _extraMime.value(ext);
        if(!custom.isEmpty()) return custom;

        for(const MimeRow &row : kMimeTable) {
                if(ext == row.ext) return String(row.mime);
        }
        return "application/octet-stream";
}

// ============================================================
// Path resolution and safety
// ============================================================

bool HttpFileHandler::isPathSafe(const String &relPath) {
        // Reject absolute paths and any segment that's ".." or
        // empty after percent-decoding (the request URL has
        // already been decoded by Url, but defense-in-depth).
        if(relPath.isEmpty()) return true;
        if(relPath.cstr()[0] == '/') return false;

        StringList parts = relPath.split("/");
        for(size_t i = 0; i < parts.size(); ++i) {
                const String &p = parts[i];
                if(p == "..") return false;
                if(p == ".")  return false;
                // Empty segments (from "//" runs) are tolerated for
                // simplicity — the filesystem layer ignores them.
        }
        return true;
}

String HttpFileHandler::resolveRequestPath(const HttpRequest &request) const {
        // Pick the relative-path source: the named path-param if
        // configured, else the entire request path.
        String rel;
        if(!_pathParam.isEmpty() && request.pathParams().contains(_pathParam)) {
                rel = request.pathParam(_pathParam);
        } else {
                rel = request.path();
                if(!rel.isEmpty() && rel.cstr()[0] == '/') {
                        rel = rel.mid(1);
                }
        }

        // Decode any leftover percent-escapes — Url::path() may not
        // have been touched after parsing depending on the codec
        // path; an extra decode is idempotent because '%' itself is
        // safe to leave alone when no two valid hex digits follow.
        Error decodeErr;
        rel = Url::percentDecode(rel, &decodeErr);
        if(decodeErr.isError()) return String();

        if(!isPathSafe(rel)) return String();

        FilePath joined = _root.path() / rel;
        return joined.toString();
}

// ============================================================
// ETag / Last-Modified helpers
// ============================================================

// Stable epoch for cirf-baked resources, which carry no real mtime.
// Captured once at process start: a running server keeps issuing 304s
// for unchanged resources, but a restart (which is when bundled
// resources can have actually changed) invalidates client caches so
// browsers re-fetch.  Without this, both Last-Modified and ETag would
// be a fixed `0`/`1970-01-01` and a stale browser cache would survive
// across rebuilds — exactly the case bitten in the debug UI.
static int64_t resourceMtimeEpoch() {
        static const int64_t kStart = static_cast<int64_t>(std::time(nullptr));
        return kStart;
}

String HttpFileHandler::etagFor(int64_t size, int64_t mtimeEpoch) {
        // Weak ETag: stable across whole-file replaces but not
        // across same-second writes that change content without
        // changing size (an acceptable false-cache-hit risk for
        // static files).  Format mirrors nginx's default.
        return String::sprintf("W/\"%llx-%llx\"",
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(mtimeEpoch));
}

String HttpFileHandler::httpDateFor(int64_t mtimeEpoch) {
        // RFC 7231: IMF-fixdate.  strftime in the C locale produces
        // the right thing on every libc we support.
        const time_t t = static_cast<time_t>(mtimeEpoch);
        std::tm gm{};
#if defined(_WIN32)
        gmtime_s(&gm, &t);
#else
        gmtime_r(&t, &gm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf),
                      "%a, %d %b %Y %H:%M:%S GMT", &gm);
        return String(buf);
}

// ============================================================
// Range support
// ============================================================

namespace {

// Parse a 64-bit non-negative integer; returns true and writes the
// value when the entire string parses, false otherwise.  Handles
// the >2GB file offsets that String::toInt (returning int) cannot.
static bool parseInt64(const String &s, int64_t &out) {
        if(s.isEmpty()) return false;
        const char *cs = s.cstr();
        char *endp = nullptr;
        errno = 0;
        const long long v = std::strtoll(cs, &endp, 10);
        if(errno != 0 || endp == cs || *endp != '\0') return false;
        if(v < 0) return false;
        out = static_cast<int64_t>(v);
        return true;
}

// Parse "bytes=START-END" (single range; multipart byteranges are
// out of scope here — the rare client that asks for them is happy
// to fall back to 200).  Returns false on any deviation.
static bool parseSingleRange(const String &header, int64_t fileSize,
                             int64_t &startOut, int64_t &endOut) {
        const char *prefix = "bytes=";
        if(header.find(prefix) != 0) return false;
        const String spec = header.mid(std::strlen(prefix));
        if(spec.contains(",")) return false;
        const size_t dash = spec.find('-');
        if(dash == String::npos) return false;
        const String startStr = spec.left(dash);
        const String endStr   = spec.mid(dash + 1);

        int64_t start = 0, end = fileSize - 1;
        if(startStr.isEmpty()) {
                // Suffix range: "bytes=-N" -> last N bytes.
                int64_t n = 0;
                if(!parseInt64(endStr, n) || n <= 0) return false;
                if(n > fileSize) n = fileSize;
                start = fileSize - n;
                end   = fileSize - 1;
        } else {
                if(!parseInt64(startStr, start)) return false;
                if(!endStr.isEmpty()) {
                        if(!parseInt64(endStr, end) || end < start) return false;
                }
        }
        if(start >= fileSize) return false;
        if(end >= fileSize) end = fileSize - 1;
        startOut = start;
        endOut   = end;
        return true;
}

} // anonymous namespace

bool HttpFileHandler::serveRange(const HttpRequest &request,
                                 HttpResponse &response,
                                 const String &fullPath,
                                 int64_t fileSize,
                                 const String &mime) {
        const String range = request.header("Range");
        if(range.isEmpty()) return false;

        int64_t start = 0, end = 0;
        if(!parseSingleRange(range, fileSize, start, end)) {
                response.setStatus(HttpStatus::RangeNotSatisfiable);
                response.setHeader("Content-Range",
                        String::sprintf("bytes */%lld",
                                static_cast<long long>(fileSize)));
                return true;
        }

        IODevice::Shared dev = IODevice::Shared::takeOwnership(
                new File(fullPath));
        File *f = static_cast<File *>(const_cast<IODevice *>(dev.ptr()));
        Error err = f->open(IODevice::ReadOnly);
        if(err.isError()) {
                response = HttpResponse::internalError("Could not open file");
                return true;
        }
        // Seek to the range start; the connection will then drain
        // exactly (end - start + 1) bytes through the streamed body.
        f->seek(start);

        const int64_t length = end - start + 1;
        response.setStatus(HttpStatus::PartialContent);
        response.setHeader("Content-Range",
                String::sprintf("bytes %lld-%lld/%lld",
                        static_cast<long long>(start),
                        static_cast<long long>(end),
                        static_cast<long long>(fileSize)));
        response.setHeader("Accept-Ranges", "bytes");
        response.setBodyStream(dev, length, mime);
        return true;
}

// ============================================================
// Main entry point
// ============================================================

void HttpFileHandler::serve(const HttpRequest &request,
                            HttpResponse &response) {
        // Method gate: only GET / HEAD.
        const HttpMethod &m = request.method();
        if(!(m == HttpMethod::Get) && !(m == HttpMethod::Head)) {
                response = HttpResponse::methodNotAllowed("GET, HEAD");
                return;
        }

        String full = resolveRequestPath(request);
        if(full.isEmpty()) {
                response.setStatus(HttpStatus::Forbidden);
                response.setText("Forbidden");
                return;
        }

        // Resource paths bypass the std::filesystem-backed FileInfo
        // (which only knows about real on-disk entries) and consult
        // the cirf-backed Resource registry.  Both kinds of input
        // converge below at the same metadata variables (fileSize,
        // mtimeEpoch).  Resource paths have no meaningful mtime, so
        // we emit a stable zero — the etag still varies with size
        // and content (the size is part of the cirf descriptor).
        const bool isResource = Resource::isResourcePath(full);

        if(isResource) {
                // Strip any trailing slash that the FilePath join may
                // have produced when the request was for a directory.
                while(full.length() > 2 && full.endsWith(String("/"))) {
                        full = full.left(full.length() - 1);
                }
                // Try the literal path first.  If it isn't a file but
                // an index name is configured, retry with the index
                // appended — that's the canonical "GET /dir/" → serve
                // /dir/index.html behaviour.
                if(Resource::findFile(full) == nullptr && !_indexFile.isEmpty()) {
                        const String withIndex = full + "/" + _indexFile;
                        if(Resource::findFile(withIndex) != nullptr) {
                                full = withIndex;
                        }
                }
                if(Resource::findFile(full) == nullptr) {
                        response = HttpResponse::notFound();
                        return;
                }
                const int64_t fileSize   = static_cast<int64_t>(Resource::size(full));
                const int64_t mtimeEpoch = resourceMtimeEpoch();
                const String  etag       = etagFor(fileSize, mtimeEpoch);
                const String  lastMod    = httpDateFor(mtimeEpoch);
                const String  mime       = mimeType(full);

                if(request.header("If-None-Match") == etag) {
                        response.setStatus(HttpStatus::NotModified);
                        response.setHeader("ETag", etag);
                        response.setHeader("Last-Modified", lastMod);
                        return;
                }

                response.setHeader("ETag", etag);
                response.setHeader("Last-Modified", lastMod);
                response.setHeader("Accept-Ranges", "bytes");
                if(serveRange(request, response, full, fileSize, mime)) return;

                IODevice::Shared dev = IODevice::Shared::takeOwnership(new File(full));
                File *f = static_cast<File *>(const_cast<IODevice *>(dev.ptr()));
                Error err = f->open(IODevice::ReadOnly);
                if(err.isError()) {
                        response = HttpResponse::internalError("Could not open resource");
                        return;
                }
                response.setStatus(HttpStatus::Ok);
                if(m == HttpMethod::Head) {
                        response.setHeader("Content-Type", mime);
                        response.setHeader("Content-Length", String::number(fileSize));
                        return;
                }
                response.setBodyStream(dev, fileSize, mime);
                return;
        }

        // Directory request → fall through to index file.
        FileInfo info{full};
        if(info.exists() && info.isDirectory()) {
                if(!_indexFile.isEmpty()) {
                        full = (FilePath(full) / _indexFile).toString();
                        info = FileInfo{full};
                } else if(_listDirs) {
                        // Minimal HTML directory listing.  Intentionally
                        // bare — anyone who needs polished output should
                        // wire a templating engine ahead of this handler.
                        Dir dir{FilePath(full)};
                        if(!dir.exists()) {
                                response = HttpResponse::notFound();
                                return;
                        }
                        String html = "<html><body><ul>";
                        const auto entries = dir.entryList();
                        for(size_t i = 0; i < entries.size(); ++i) {
                                const String name = FileInfo(entries[i]).fileName();
                                html += "<li><a href=\"";
                                html += Url::percentEncode(name, "/");
                                html += "\">";
                                html += name;
                                html += "</a></li>";
                        }
                        html += "</ul></body></html>";
                        response.setHtml(html);
                        return;
                } else {
                        response.setStatus(HttpStatus::Forbidden);
                        response.setText("Directory listings disabled");
                        return;
                }
        }

        if(!info.exists() || !info.isFile()) {
                response = HttpResponse::notFound();
                return;
        }

        auto [fileSize, fileSizeErr] = info.size();
        if(fileSizeErr != Error::Ok) {
                response = HttpResponse::notFound();
                return;
        }
        const int64_t mtimeEpoch  = info.lastModified().toTimeT();
        const String  etag        = etagFor(fileSize, mtimeEpoch);
        const String  lastMod     = httpDateFor(mtimeEpoch);
        const String  mime        = mimeType(full);

        // Conditional GET: short-circuit with 304 when the client's
        // cache copy is still fresh.
        if(request.header("If-None-Match") == etag) {
                response.setStatus(HttpStatus::NotModified);
                response.setHeader("ETag", etag);
                response.setHeader("Last-Modified", lastMod);
                return;
        }
        const String ims = request.header("If-Modified-Since");
        if(!ims.isEmpty() && ims == lastMod) {
                response.setStatus(HttpStatus::NotModified);
                response.setHeader("ETag", etag);
                response.setHeader("Last-Modified", lastMod);
                return;
        }

        // Range request?  Headers populate inside serveRange.
        response.setHeader("ETag", etag);
        response.setHeader("Last-Modified", lastMod);
        response.setHeader("Accept-Ranges", "bytes");
        if(serveRange(request, response, full, fileSize, mime)) return;

        // Vanilla 200 with streamed body.
        IODevice::Shared dev = IODevice::Shared::takeOwnership(new File(full));
        File *f = static_cast<File *>(const_cast<IODevice *>(dev.ptr()));
        Error err = f->open(IODevice::ReadOnly);
        if(err.isError()) {
                response = HttpResponse::internalError("Could not open file");
                return;
        }

        response.setStatus(HttpStatus::Ok);
        if(m == HttpMethod::Head) {
                // Headers only — set Content-Length but no body so
                // the connection writes just the framing.  Re-using
                // setBodyStream with a length-0 device would send
                // empty chunked encoding; instead we set an empty
                // in-memory body and override Content-Length below.
                response.setHeader("Content-Type", mime);
                response.setHeader("Content-Length", String::number(fileSize));
                return;
        }
        response.setBodyStream(dev, fileSize, mime);
}

PROMEKI_NAMESPACE_END
