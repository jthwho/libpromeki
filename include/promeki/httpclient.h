/**
 * @file      httpclient.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_HTTP
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/url.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>
#include <promeki/future.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpheaders.h>
#include <promeki/sslcontext.h>

PROMEKI_NAMESPACE_BEGIN

class EventLoop;

/**
 * @brief Async HTTP/1.x client built on top of @ref TcpSocket and llhttp.
 * @ingroup network
 *
 * The client mirrors @ref HttpServer's threading and EventLoop story:
 * the calling thread's loop (or @ref Application::mainEventLoop when
 * none is current) drives all socket I/O.  Each call to @ref send
 * returns immediately with a @ref Future ; the response (or any
 * error) is delivered when the EventLoop has had a chance to drive
 * the request to completion.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase &mdash; thread-affine.  All state mutates on
 * the client's owning EventLoop thread.  Callers that block-wait
 * on the @ref Future must be running on a different thread than
 * the one driving the EventLoop, otherwise the loop will never
 * get a chance to advance the request — the canonical pattern is
 * "client lives on the main loop, worker threads issue requests
 * and block on their Futures".
 *
 * @par Lifetime model
 * The client owns no persistent connection state — every request
 * opens a fresh @ref TcpSocket, sends one request, reads the
 * response, and closes.  Connection pooling and HTTP/1.1 keep-alive
 * reuse are deliberate non-goals for v1; the surface stays small and
 * predictable, and a follow-up can layer pooling on without changing
 * the public API.
 *
 * @par TLS
 * @c https:// URLs are rejected until @ref SslSocket is wired in;
 * the client returns @ref Error::NotImplemented from @ref send when
 * the request URL's scheme is @c https.
 *
 * @par Example
 * @code
 * HttpClient client;
 * Future<HttpResponse> fut = client.get("http://localhost:8080/api/status");
 * auto [response, err] = fut.result(2000);   // 2-second timeout
 * if(err.isOk() && response.isSuccess()) {
 *     promekiInfo("got %d bytes", (int)response.body().size());
 * }
 * @endcode
 */
class HttpClient : public ObjectBase {
                PROMEKI_OBJECT(HttpClient, ObjectBase)
        public:
                /** @brief Default per-request timeout in milliseconds. */
                static constexpr unsigned int DefaultTimeoutMs = 30'000;

                /** @brief Default upper limit on a single response body. */
                static constexpr int64_t DefaultMaxBodyBytes = 64 * 1024 * 1024;

                /**
                 * @brief Default redirect-follow limit.
                 *
                 * Ten by default, matching the default in Go's
                 * @c net/http and within the range every mainstream
                 * client uses (curl: 50, Chromium: 20).  Following
                 * redirects is the transparent behavior callers
                 * expect — a signed-CDN GET (Hugging Face, S3,
                 * etc.) chains two or three 3xx hops before reaching
                 * the bytes, and a buffered downloader that didn't
                 * follow them would silently land an HTML redirect
                 * stub in the destination.  Callers that need to
                 * inspect the redirect chain set this to @c 0 with
                 * @ref setMaxRedirects to disable automatic
                 * following.
                 */
                static constexpr int DefaultMaxRedirects = 10;

                /**
                 * @brief Constructs a client bound to the current EventLoop.
                 *
                 * Falls back to @ref Application::mainEventLoop when
                 * no loop is current (the typical case in single-
                 * threaded programs that haven't yet entered exec()).
                 */
                explicit HttpClient(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Cancels any in-flight requests. */
                ~HttpClient() override;

                /**
                 * @brief Sends @p request and returns a future for the response.
                 *
                 * The request is dispatched onto the owning EventLoop;
                 * the @ref Future fulfills when the response is fully
                 * parsed or an error occurs.  Errors include
                 * @ref Error::Timeout, @ref Error::ConnectionRefused,
                 * @ref Error::Invalid (parse failure), and
                 * @ref Error::NotImplemented for unsupported schemes.
                 */
                Future<HttpResponse> send(const HttpRequest &request);

                /** @brief Convenience: GET @p url. */
                Future<HttpResponse> get(const String &url);

                /** @brief Convenience: POST @p url with @p body. */
                Future<HttpResponse> post(const String &url, const Buffer &body,
                                          const String &contentType = String("application/octet-stream"));

                /** @brief Convenience: PUT @p url with @p body. */
                Future<HttpResponse> put(const String &url, const Buffer &body,
                                         const String &contentType = String("application/octet-stream"));

                /**
                 * @brief Convenience: DELETE @p url.
                 *
                 * Spelled @c del because @c delete is a C++ reserved word.
                 */
                Future<HttpResponse> del(const String &url);

                // ----------------------------------------------------
                // Defaults applied to every request
                // ----------------------------------------------------

                /**
                 * @brief Sets a header that's added to every outgoing request.
                 *
                 * Per-request headers in the @ref HttpRequest passed to
                 * @ref send override defaults of the same name.
                 */
                void setDefaultHeader(const String &name, const String &value);

                /**
                 * @brief Removes a previously-installed default header.
                 */
                void removeDefaultHeader(const String &name);

                /**
                 * @brief Returns the currently-set default headers.
                 */
                const HttpHeaders &defaultHeaders() const { return _defaultHeaders; }

                /**
                 * @brief Sets a base URL prepended to relative request URLs.
                 *
                 * The convenience accessors (@ref get, @ref post, ...)
                 * resolve their @p url against this base.  Pass an
                 * empty Url to clear.
                 */
                void setBaseUrl(const Url &url) { _baseUrl = url; }

                /** @brief Returns the configured base URL. */
                const Url &baseUrl() const { return _baseUrl; }

                /**
                 * @brief Sets the per-request timeout in milliseconds.
                 *
                 * Applies to subsequent calls to @ref send; in-flight
                 * requests keep their timer.  Set to @c 0 to disable.
                 */
                void setTimeoutMs(unsigned int ms) { _timeoutMs = ms; }

                /** @brief Sets the upper bound on a response body. */
                void setMaxBodyBytes(int64_t bytes) { _maxBodyBytes = bytes; }

                /**
                 * @brief Sets the maximum number of redirects to follow automatically.
                 *
                 * When set to a positive value, @ref HttpClient
                 * intercepts any 3xx response that carries a @c Location
                 * header and silently re-dispatches the request to that
                 * URL, up to @p max times.  The body sink and progress
                 * callback installed on the request are invoked only
                 * for the final non-redirect response, so a caller
                 * streaming a large download to disk does not have to
                 * worry about a tiny redirect-body landing in the
                 * destination file ahead of the real bytes.
                 *
                 * Only absolute redirect URLs (starting with @c http://
                 * or @c https://) are followed; relative redirects fail
                 * the request with @ref Error::NotSupported.  Cross-
                 * scheme redirects are permitted (e.g. http→https for
                 * the common CDN upgrade), but require this client to
                 * be TLS-capable for any https leg.
                 *
                 * Default is @ref DefaultMaxRedirects (10), matching the
                 * default in Go's @c net/http.  Following redirects is
                 * the transparent behavior callers expect — a signed-
                 * CDN GET (Hugging Face, S3, etc.) chains two or three
                 * 3xx hops before reaching the bytes, and a buffered
                 * downloader that didn't follow them would silently
                 * land an HTML redirect stub in the destination file.
                 *
                 * @param max Maximum hops to follow.  @c 0 disables
                 *            automatic following — the first 3xx is
                 *            returned to the caller for manual
                 *            inspection.
                 */
                void setMaxRedirects(int max) { _maxRedirects = max; }

                /** @brief Returns the current redirect-follow limit. */
                int  maxRedirects() const { return _maxRedirects; }

                /**
                 * @brief Attaches the @ref SslContext used for @c https:// requests.
                 *
                 * Optional — a default-constructed @ref SslContext
                 * already auto-loads the system CA bundle, so
                 * @c https requests against publicly-trusted servers
                 * "just work" without any setup.  Configure a custom
                 * context when you need a private CA, client
                 * certificates, or want to disable verification for
                 * development (@c setVerifyPeer(false)).
                 *
                 * If no system CA bundle is available the handshake
                 * fails-closed (returns @ref Error::Invalid) rather
                 * than silently skipping verification.
                 *
                 * Always available regardless of @c PROMEKI_ENABLE_TLS;
                 * in a TLS-disabled build the supplied context is
                 * stored but its operations are inert (see
                 * @ref SslContext) and an actual @c https request is
                 * later rejected with @ref Error::NotSupported.
                 */
                void setSslContext(SslContext ctx) { _sslContext = std::move(ctx); }

                /** @brief Returns the attached SslContext, or null. */
                SslContext sslContext() const { return _sslContext; }

                /**
                 * @brief Reports whether this build can speak TLS.
                 *
                 * Forwards to @ref SslContext::hasTlsSupport — the single
                 * source of truth for the @c PROMEKI_ENABLE_TLS feature
                 * flag.
                 */
                static bool hasTlsSupport() { return SslContext::hasTlsSupport(); }

                /** @brief Emitted when a request has finished (success or error). @signal */
                PROMEKI_SIGNAL(requestFinished, HttpRequest, HttpResponse);

                /** @brief Emitted on transport / parse / timeout failures. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

        private:
                struct Pending;
                using PendingPtr = SharedPtr<Pending, false>;

                EventLoop       *_loop = nullptr;
                HttpHeaders      _defaultHeaders;
                Url              _baseUrl;
                unsigned int     _timeoutMs = DefaultTimeoutMs;
                int64_t          _maxBodyBytes = DefaultMaxBodyBytes;
                int              _maxRedirects = DefaultMaxRedirects;
                List<PendingPtr> _active;
                SslContext  _sslContext;

                Future<HttpResponse> dispatch(HttpRequest request);
                void                 resolveTargetUrl(HttpRequest &request) const;
                void                 applyDefaultHeaders(HttpRequest &request) const;
                void                 retire(const PendingPtr &p);

                // The per-request state machine lives in the cpp;
                // it needs to reach back into _active and emit signals.
                friend struct Pending;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_HTTP
