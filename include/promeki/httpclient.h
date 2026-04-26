/**
 * @file      httpclient.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

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
#if PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#endif

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
 * Inherits @ref ObjectBase: thread-affine.  All state mutates on
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

#if PROMEKI_ENABLE_TLS
                /**
                 * @brief Attaches the @ref SslContext used for @c https:// requests.
                 *
                 * Without a context the client falls back to the
                 * default-constructed @ref SslContext (TLS 1.2 / 1.3,
                 * peer verification on, no CA bundle) which will
                 * fail verification against any real server — set a
                 * proper context (with @ref SslContext::setSystemCaCertificates
                 * for the system trust store, for example) before
                 * making @c https:// requests.
                 */
                void setSslContext(SslContext::Ptr ctx) { _sslContext = std::move(ctx); }

                /** @brief Returns the attached SslContext, or null. */
                SslContext::Ptr sslContext() const { return _sslContext; }
#endif

                /** @brief Emitted when a request has finished (success or error). @signal */
                PROMEKI_SIGNAL(requestFinished, HttpRequest, HttpResponse);

                /** @brief Emitted on transport / parse / timeout failures. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

        private:
                struct Pending;
                using PendingPtr = SharedPtr<Pending, false>;

                EventLoop                       *_loop = nullptr;
                HttpHeaders                     _defaultHeaders;
                Url                             _baseUrl;
                unsigned int                    _timeoutMs = DefaultTimeoutMs;
                int64_t                         _maxBodyBytes = DefaultMaxBodyBytes;
                List<PendingPtr>                _active;
#if PROMEKI_ENABLE_TLS
                SslContext::Ptr                 _sslContext;
#endif

                Future<HttpResponse> dispatch(HttpRequest request);
                void resolveTargetUrl(HttpRequest &request) const;
                void applyDefaultHeaders(HttpRequest &request) const;
                void retire(const PendingPtr &p);

                // The per-request state machine lives in the cpp;
                // it needs to reach back into _active and emit signals.
                friend struct Pending;
};

PROMEKI_NAMESPACE_END
