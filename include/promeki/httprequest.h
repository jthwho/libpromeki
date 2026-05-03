/**
 * @file      httprequest.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/url.h>
#include <promeki/hashmap.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/httpmethod.h>
#include <promeki/httpheaders.h>
#include <promeki/json.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A parsed HTTP request.
 * @ingroup network
 *
 * @ref HttpRequest is a Shareable data object: a value-with-COW
 * container that holds the method, URL, headers, body, and any
 * route-matched path parameters.  It is what @ref HttpServer hands
 * to a registered handler, and what @ref HttpClient sends.
 *
 * The body is held as a @ref Buffer, with text and JSON
 * convenience accessors.  Because the URL is a full @ref Url, all
 * scheme/host/path/query/fragment knowledge is delegated there;
 * @ref path and @ref queryValue are convenience forwards onto the
 * URL the request was constructed with.
 *
 * Path parameters are populated by the @ref HttpRouter when a
 * pattern containing @c "{name}" or @c "{name:*}" placeholders
 * matches the request path.  Handlers consume them via
 * @ref pathParam.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently — copies share the
 * underlying body @ref Buffer via atomic refcount.  A single
 * instance is conditionally thread-safe: const accessors are
 * safe, mutators (@c setUrl, @c setBody, @c headers().set, ...)
 * require external synchronization.
 *
 * @par Example
 * @code
 * void serveItem(const HttpRequest &req, HttpResponse &res) {
 *     String id = req.pathParam("id");
 *     bool wantJson = req.header("Accept").contains("json");
 *     // ...
 * }
 * @endcode
 */
class HttpRequest {
                PROMEKI_SHARED_FINAL(HttpRequest)
        public:
                /** @brief Shared pointer type for HttpRequest. */
                using Ptr = SharedPtr<HttpRequest>;

                /** @brief Plain value list (e.g. for queues of requests). */
                using List = ::promeki::List<HttpRequest>;

                /** @brief List of shared pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Default HTTP version reported when none was parsed. */
                static const String DefaultHttpVersion;

                /** @brief Constructs an empty request. */
                HttpRequest() = default;

                /** @brief Returns the request method. */
                const HttpMethod &method() const { return _method; }

                /** @brief Replaces the request method. */
                void setMethod(const HttpMethod &m) { _method = m; }

                /**
                 * @brief Returns the request URL.
                 *
                 * For server-side requests this is reconstructed from
                 * the request-target and the @c Host header during
                 * parsing.  For client-side requests this is the URL
                 * the caller asked to send to.
                 */
                const Url &url() const { return _url; }

                /** @brief Replaces the request URL. */
                void setUrl(const Url &u) { _url = u; }

                /** @brief Convenience for @c url().path(). */
                const String &path() const { return _url.path(); }

                /** @brief Convenience for @c url().queryValue(). */
                String queryValue(const String &key, const String &defaultValue = String()) const {
                        return _url.queryValue(key, defaultValue);
                }

                /** @brief Returns the headers collection. */
                const HttpHeaders &headers() const { return _headers; }

                /** @brief Mutable accessor for the headers collection. */
                HttpHeaders &headers() { return _headers; }

                /** @brief Replaces the headers wholesale. */
                void setHeaders(const HttpHeaders &h) { _headers = h; }

                /** @brief Single-value header convenience accessor. */
                String header(const String &name, const String &defaultValue = String()) const {
                        return _headers.value(name, defaultValue);
                }

                /** @brief Returns the request body buffer. */
                const Buffer &body() const { return _body; }

                /** @brief Sets the body to a copy of @p body. */
                void setBody(const Buffer &body) { _body = body; }

                /**
                 * @brief Sets the body from a string, copying its bytes.
                 *
                 * Does not set @c Content-Type; callers must do so
                 * explicitly when they care.
                 */
                void setBody(const String &text);

                /**
                 * @brief Returns the body interpreted as a UTF-8 String.
                 *
                 * No conversion is performed — the bytes are taken at
                 * face value.  Suitable for JSON, plain text, or any
                 * other Latin-1/ASCII-compatible body; binary callers
                 * should use @ref body directly.
                 */
                String bodyAsString() const;

                /**
                 * @brief Parses the body as a JSON object.
                 *
                 * @param err Optional error output; set to
                 *            @ref Error::Invalid if parsing fails or the
                 *            body is not a JSON object.
                 * @return The parsed object, or an empty object on failure.
                 */
                JsonObject bodyAsJson(Error *err = nullptr) const;

                /**
                 * @brief Parses the body as a JSON array.
                 *
                 * @param err Optional error output; set to
                 *            @ref Error::Invalid if parsing fails or the
                 *            body is not a JSON array.
                 * @return The parsed array, or an empty array on failure.
                 */
                JsonArray bodyAsJsonArray(Error *err = nullptr) const;

                /**
                 * @brief Sets the body from a JsonObject and the JSON content type.
                 *
                 * Compact serialization is used (no indentation) since
                 * this is going on the wire.
                 */
                void setBody(const JsonObject &obj);

                /** @brief As above, for arrays. */
                void setBody(const JsonArray &arr);

                /** @brief All matched path parameters. */
                const HashMap<String, String> &pathParams() const { return _pathParams; }

                /** @brief Mutator used by @ref HttpRouter. */
                void setPathParams(const HashMap<String, String> &params) { _pathParams = params; }

                /**
                 * @brief Returns a single path parameter by name.
                 * @param name         The placeholder name from the route pattern.
                 * @param defaultValue Fallback when the placeholder is absent.
                 */
                String pathParam(const String &name, const String &defaultValue = String()) const {
                        return _pathParams.value(name, defaultValue);
                }

                /**
                 * @brief Wire HTTP version, e.g. @c "HTTP/1.1".
                 *
                 * Defaults to @c "HTTP/1.1" when not yet parsed.
                 * Set by @ref HttpConnection from the major/minor
                 * version reported by llhttp.
                 */
                const String &httpVersion() const { return _httpVersion; }

                /** @brief Replaces the wire HTTP version string. */
                void setHttpVersion(const String &v) { _httpVersion = v; }

                /**
                 * @brief Address of the peer that originated this request.
                 *
                 * Empty by default; populated by @ref HttpConnection
                 * after accept().  Useful for logging and access
                 * control inside handlers.
                 */
                const String &peerAddress() const { return _peerAddress; }

                /** @brief Setter used by HttpConnection. */
                void setPeerAddress(const String &s) { _peerAddress = s; }

                /** @brief Equality on every member. */
                bool operator==(const HttpRequest &other) const;

                /** @brief Inverse of @ref operator==. */
                bool operator!=(const HttpRequest &other) const { return !(*this == other); }

        private:
                HttpMethod              _method;
                Url                     _url;
                HttpHeaders             _headers;
                Buffer                  _body;
                HashMap<String, String> _pathParams;
                String                  _httpVersion = DefaultHttpVersion;
                String                  _peerAddress;
};

PROMEKI_NAMESPACE_END
