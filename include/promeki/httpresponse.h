/**
 * @file      httpresponse.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/iodevice.h>
#include <promeki/httpstatus.h>
#include <promeki/httpheaders.h>
#include <promeki/json.h>

PROMEKI_NAMESPACE_BEGIN

class TcpSocket;

/**
 * @brief A built-up HTTP response.
 * @ingroup network
 *
 * @ref HttpResponse is the half of the request/response pair that
 * server handlers populate (and that clients receive).  It carries a
 * status, a header collection, and a body.
 *
 * The body has two backing forms, used in the obvious order of
 * preference:
 *
 *  1. An in-memory @ref Buffer (set via @ref setBody, @ref setText,
 *     @ref setJson, @ref setHtml, @ref setBinary).  Cheap, simple,
 *     and the right call for the vast majority of API responses.
 *
 *  2. A streamed @ref IODevice (set via @ref setBodyStream).  Used by
 *     @ref HttpFileHandler so the response can pour out a multi-GB
 *     file without loading it all at once.  When a stream is set, the
 *     in-memory body is ignored.
 *
 * Convenience factories (@ref ok, @ref notFound, @ref badRequest,
 * @ref internalError) cover the common quick-reply paths.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently — copies share the
 * underlying body @ref Buffer via atomic refcount.  A single
 * instance is conditionally thread-safe: const accessors are
 * safe, mutators (@c setStatus, @c setBody, @c setHeader, ...)
 * require external synchronization.
 *
 * @par Example
 * @code
 * HttpResponse res = HttpResponse::ok(makeJsonObject());
 * res.setHeader("Cache-Control", "no-store");
 *
 * // Or, hand-built:
 * HttpResponse r;
 * r.setStatus(HttpStatus::Created);
 * r.setHeader("Location", String::sprintf("/items/%d", id));
 * r.setJson(itemAsJson);
 * @endcode
 */
class HttpResponse {
                PROMEKI_SHARED_FINAL(HttpResponse)
        public:
                /** @brief Shared pointer type for HttpResponse. */
                using Ptr = SharedPtr<HttpResponse>;

                /** @brief Plain value list (e.g. for queues of responses). */
                using List = ::promeki::List<HttpResponse>;

                /** @brief List of shared pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Default HTTP version reported when none was set. */
                static const String DefaultHttpVersion;

                /** @brief Constructs an empty 200 response. */
                HttpResponse() = default;

                /** @brief Returns the response status. */
                const HttpStatus &status() const { return _status; }

                /** @brief Replaces the response status. */
                void setStatus(const HttpStatus &s) {
                        _status = s;
                        _customReason.clear();
                }

                /**
                 * @brief Replaces the status by raw integer.
                 *
                 * Useful for non-well-known codes; the corresponding
                 * @ref HttpStatus::reasonPhrase fallback ("Status NNN")
                 * is used unless @ref setReasonPhrase overrides it.
                 */
                void setStatus(int code) {
                        _status = HttpStatus{code};
                        _customReason.clear();
                }

                /**
                 * @brief Reason phrase that will be sent on the wire.
                 *
                 * Defaults to @ref HttpStatus::reasonPhrase for the
                 * current status.  Override via @ref setReasonPhrase
                 * for non-canonical phrases.
                 */
                String reasonPhrase() const;

                /** @brief Overrides the wire reason phrase. */
                void setReasonPhrase(const String &s) { _customReason = s; }

                /** @brief Returns the headers collection. */
                const HttpHeaders &headers() const { return _headers; }

                /** @brief Mutable accessor for the headers collection. */
                HttpHeaders &headers() { return _headers; }

                /** @brief Replaces the headers wholesale. */
                void setHeaders(const HttpHeaders &h) { _headers = h; }

                /** @brief Single-value header convenience. */
                void setHeader(const String &name, const String &value) { _headers.set(name, value); }

                /** @brief Append-style header convenience. */
                void addHeader(const String &name, const String &value) { _headers.add(name, value); }

                /** @brief Returns the in-memory body buffer. */
                const Buffer &body() const { return _body; }

                /**
                 * @brief Whether the response body is being streamed.
                 *
                 * True after @ref setBodyStream until reset by another
                 * @c setBody* call.
                 */
                bool hasBodyStream() const { return _bodyStream.isValid(); }

                /**
                 * @brief Returns the streamed body device.
                 *
                 * Non-owning view; ownership remains with the response
                 * until @ref takeBodyStream is called.  Returns nullptr
                 * if @ref hasBodyStream is false.
                 */
                IODevice *bodyStream() const {
                        return _bodyStream.isValid() ? const_cast<IODevice *>(_bodyStream.ptr()) : nullptr;
                }

                /**
                 * @brief Returns the shared stream pointer (refcount unchanged).
                 *
                 * Used by @ref HttpConnection when it wants to keep
                 * the device alive past the response's lifetime
                 * without taking sole ownership.
                 */
                const IODevice::Shared &bodyStreamShared() const { return _bodyStream; }

                /**
                 * @brief Reported length of a streamed body, or -1 if unknown.
                 *
                 * When known, the connection writes a @c Content-Length
                 * header and pours the device byte-for-byte.  When
                 * unknown (size() returned -1), the connection falls
                 * back to @c Transfer-Encoding: chunked.
                 */
                int64_t bodyStreamLength() const { return _bodyStreamLength; }

                /**
                 * @brief Hands the stream to a consumer and clears it
                 *        from the response.
                 *
                 * Used by @ref HttpConnection when it begins reading
                 * from the device.  Returns an empty SharedPtr if no
                 * stream was set.  After this call @ref hasBodyStream
                 * is false.  Sharing semantics: copies of the response
                 * made before this call still see the device.
                 */
                IODevice::Shared takeBodyStream();

                /** @brief Sets the in-memory body to a copy of @p body. */
                void setBody(const Buffer &body);

                /** @brief Sets the in-memory body from a string. */
                void setBody(const String &text);

                /**
                 * @brief Sets the body and @c Content-Type to @c text/plain.
                 *
                 * Adds @c "; charset=utf-8" so clients pick the right
                 * decoder; override later via @ref setHeader if a
                 * different charset is needed.
                 */
                void setText(const String &text);

                /**
                 * @brief Sets the body and @c Content-Type to @c text/html.
                 */
                void setHtml(const String &html);

                /**
                 * @brief Sets a binary body with the supplied content type.
                 */
                void setBinary(const Buffer &body, const String &mimeType);

                /**
                 * @brief Sets the body to a JSON object and the JSON content type.
                 *
                 * Compact serialization.  Use @ref setJsonPretty for
                 * indented output.
                 */
                void setJson(const JsonObject &obj);

                /** @brief As above, for arrays. */
                void setJson(const JsonArray &arr);

                /** @brief Pretty-printed variant of @ref setJson. */
                void setJsonPretty(const JsonObject &obj, unsigned int indent = 2);

                /** @brief Pretty-printed variant of @ref setJson for arrays. */
                void setJsonPretty(const JsonArray &arr, unsigned int indent = 2);

                /**
                 * @brief Streams a body from an IODevice.
                 *
                 * Takes ownership of @p device.  When @p length is
                 * non-negative, sets @c Content-Length and writes
                 * exactly that many bytes; when -1, switches to
                 * @c Transfer-Encoding: chunked and writes until the
                 * device's @c read returns 0.
                 *
                 * If @p mimeType is non-empty, sets @c Content-Type
                 * to it.  Otherwise the caller is responsible for
                 * setting the header.
                 */
                void setBodyStream(IODevice::Shared device, int64_t length = -1, const String &mimeType = String());

                /** @brief Convenience: 2xx status. */
                bool isSuccess() const { return _status.isSuccess(); }
                /** @brief Convenience: 3xx status. */
                bool isRedirect() const { return _status.isRedirect(); }
                /** @brief Convenience: 4xx or 5xx status. */
                bool isError() const { return _status.isError(); }

                /** @brief Wire HTTP version (defaults to @c "HTTP/1.1"). */
                const String &httpVersion() const { return _httpVersion; }

                /** @brief Override the wire HTTP version. */
                void setHttpVersion(const String &v) { _httpVersion = v; }

                // ============================================================
                // Convenience factories
                // ============================================================

                /** @brief 200 OK with a JSON object body. */
                static HttpResponse ok(const JsonObject &obj);

                /** @brief 200 OK with a text body. */
                static HttpResponse ok(const String &text);

                /**
                 * @brief 404 Not Found, optionally with a plain-text body.
                 *
                 * The default body is the canonical reason phrase so
                 * even un-customized 404s have a non-empty payload for
                 * curl-based debugging.
                 */
                static HttpResponse notFound(const String &message = String());

                /** @brief 400 Bad Request. */
                static HttpResponse badRequest(const String &message = String());

                /** @brief 405 Method Not Allowed.  Sets the @c Allow header. */
                static HttpResponse methodNotAllowed(const String &allow);

                /** @brief 500 Internal Server Error. */
                static HttpResponse internalError(const String &message = String());

                /** @brief 204 No Content. */
                static HttpResponse noContent();

                // ============================================================
                // Protocol upgrade hook
                // ============================================================

                /**
                 * @brief Callback invoked after a @c 101 response finishes writing.
                 *
                 * Used by @ref HttpServer::routeWebSocket to detach the
                 * underlying socket from the HTTP layer once the
                 * @c "Switching Protocols" response has drained.  The
                 * callback receives the open, non-blocking
                 * @ref TcpSocket (or @ref SslSocket); ownership
                 * transfers to the callback, and the originating
                 * @ref HttpConnection self-destructs immediately
                 * afterwards.
                 *
                 * Set on a 101-status response by application or
                 * library handlers; ignored on non-101 responses.
                 */
                using UpgradeHook = std::function<void(TcpSocket *socket)>;

                /** @brief Installs the upgrade hook (see @ref UpgradeHook). */
                void setUpgradeHook(UpgradeHook hook) { _upgradeHook = std::move(hook); }

                /** @brief Returns the installed upgrade hook (may be empty). */
                const UpgradeHook &upgradeHook() const { return _upgradeHook; }

        private:
                HttpStatus       _status = HttpStatus::Ok;
                String           _customReason;
                HttpHeaders      _headers;
                Buffer           _body;
                IODevice::Shared _bodyStream;
                int64_t          _bodyStreamLength = -1;
                String           _httpVersion = DefaultHttpVersion;
                UpgradeHook      _upgradeHook;
};

PROMEKI_NAMESPACE_END
