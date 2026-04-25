/**
 * @file      httpserver.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/socketaddress.h>
#include <promeki/list.h>
#include <promeki/uniqueptr.h>
#include <promeki/httprouter.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpconnection.h>
#include <promeki/variant.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantlookup.h>
#include <promeki/variantspec.h>
#include <promeki/json.h>
#include <promeki/stringlist.h>
#if PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#endif

PROMEKI_NAMESPACE_BEGIN

class TcpServer;
class EventLoop;
class WebSocket;

/**
 * @brief Top-level HTTP/1.x server bound to a TCP port.
 * @ingroup network
 *
 * @ref HttpServer is the public face of the HTTP stack: it owns a
 * @ref TcpServer that listens for connections, an @ref HttpRouter
 * that maps patterns to handlers, and the live @ref HttpConnection
 * instances that flow through it.  Everything happens on a single
 * @ref EventLoop — the one that's current at construction time, or
 * @ref Application::mainEventLoop when none is current — so handlers
 * run on a known thread without needing their own synchronization.
 *
 * The model intentionally mirrors Go's @c net/http: register routes
 * (or middleware) on the server, then call @ref listen.  Handlers
 * receive a request and write into a response object that the
 * connection serializes to the wire when the handler returns.
 *
 * @par Threading model
 * Construction captures @ref EventLoop::current as the *owning loop*.
 * If the server is constructed inside a worker @ref Thread, the
 * server uses that thread's loop; otherwise it falls back to
 * @ref Application::mainEventLoop.  Cross-thread handlers should
 * accept the request, snapshot what they need, do their work on a
 * worker, and call @ref HttpConnection::postResponse from the
 * owning loop (typically via @c EventLoop::postCallable).  Signal
 * connections to slots on other threads are dispatched
 * automatically by the existing signal/slot machinery.
 *
 * @par Example
 * @code
 * Application app(argc, argv);
 * HttpServer server;
 * server.route("/api/health", HttpMethod::Get,
 *     [](const auto &, auto &res) { res.setText("ok"); });
 * server.use([](const auto &req, auto &res, auto next) {
 *     promekiInfo("%s %s", req.method().wireName().cstr(), req.path().cstr());
 *     next();
 * });
 * server.listen(8080);
 * return app.exec();
 * @endcode
 */
class HttpServer : public ObjectBase {
        PROMEKI_OBJECT(HttpServer, ObjectBase)
        public:
                /**
                 * @brief Constructs an HTTP server bound to the current EventLoop.
                 *
                 * Falls back to @ref Application::mainEventLoop when
                 * no loop is current (the typical case in single-
                 * threaded programs that haven't yet entered exec()).
                 */
                explicit HttpServer(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes the listening socket and all live connections. */
                ~HttpServer() override;

                // ----------------------------------------------------
                // Listening
                // ----------------------------------------------------

                /**
                 * @brief Starts listening on @p address.
                 *
                 * @param address Address to bind to (use
                 *                @ref SocketAddress::any "::any(port)"
                 *                to listen on all interfaces).
                 * @param backlog Maximum pending-connection queue length.
                 * @return @ref Error::Ok on success, or a system error.
                 */
                Error listen(const SocketAddress &address, int backlog = 50);

                /**
                 * @brief Convenience: listen on every interface, port @p port.
                 */
                Error listen(uint16_t port, int backlog = 50);

                /** @brief Stops listening and closes all live connections. */
                void close();

                /** @brief True between successful @ref listen and @ref close. */
                bool isListening() const;

                /** @brief Address the server is bound to (post-listen). */
                SocketAddress serverAddress() const;

                // ----------------------------------------------------
                // Routing
                // ----------------------------------------------------

                /** @brief Mutable router accessor. */
                HttpRouter &router() { return _router; }

                /** @brief Const router accessor. */
                const HttpRouter &router() const { return _router; }

                /** @brief Convenience forwarding to @ref HttpRouter::route. */
                void route(const String &pattern,
                           const HttpMethod &method,
                           HttpHandlerFunc handler);

                /** @copydoc route */
                void route(const String &pattern,
                           const HttpMethod &method,
                           HttpHandler::Ptr handler);

                /** @brief Convenience forwarding to @ref HttpRouter::any. */
                void any(const String &pattern, HttpHandlerFunc handler);

                /** @brief Convenience forwarding to @ref HttpRouter::use. */
                void use(HttpMiddleware middleware);

                /**
                 * @brief Callback invoked once a WebSocket upgrade succeeds.
                 *
                 * Receives the freshly-constructed @ref WebSocket
                 * (already attached to the upgraded socket and in the
                 * @ref WebSocket::Connected state) along with the
                 * @ref HttpRequest that triggered the upgrade — useful
                 * for reading query parameters or path placeholders
                 * negotiated during routing.  The request is a
                 * snapshot taken at handshake time, so its body and
                 * headers are stable for the duration of the call.
                 *
                 * Lifetime of the WebSocket is owned by the
                 * application — the callback is responsible for
                 * re-parenting or storing it somewhere that outlives
                 * the connection.
                 */
                using WebSocketHandler =
                        std::function<void(WebSocket *socket, const HttpRequest &request)>;

                /**
                 * @brief Registers a route that upgrades to WebSocket on @p pattern.
                 *
                 * Validates the @c Upgrade / @c Connection /
                 * @c Sec-WebSocket-Key / @c Sec-WebSocket-Version
                 * headers, computes @c Sec-WebSocket-Accept, sends a
                 * @c 101 @c Switching @c Protocols response, and
                 * once that response has finished writing, hands the
                 * underlying socket off to a new @ref WebSocket.  The
                 * @p handler is invoked with the WebSocket on the
                 * server's EventLoop thread.
                 *
                 * Subprotocol negotiation: when the client sends a
                 * @c Sec-WebSocket-Protocol header, the first listed
                 * subprotocol is echoed back verbatim.  Applications
                 * needing finer control should fall back to
                 * @ref route + a hand-written upgrade handler.
                 */
                void routeWebSocket(const String &pattern, WebSocketHandler handler);

                // ----------------------------------------------------
                // Reflection adapters
                // ----------------------------------------------------

                /**
                 * @brief Mounts a CRUD HTTP API over a @ref VariantDatabase.
                 *
                 * Adds five routes under @p mountPath:
                 *  - @c GET   @c <mountPath>            — full snapshot as JSON object
                 *  - @c GET   @c <mountPath>/_schema    — registered specs as JSON
                 *  - @c GET   @c <mountPath>/{key}      — one value as JSON
                 *  - @c PUT   @c <mountPath>/{key}      — accepts JSON, validates, stores
                 *  - @c DELETE @c <mountPath>/{key}     — clears the entry
                 *
                 * When @p readOnly is true, the @c PUT / @c DELETE
                 * routes are skipped — useful for exposing a runtime
                 * configuration as a read-only introspection endpoint.
                 *
                 * The database is captured by reference; its lifetime
                 * must outlive the server.
                 *
                 * @tparam N       The compile-time database name.
                 * @param mountPath Route prefix (no trailing slash).
                 * @param db        The database instance to expose.
                 * @param readOnly  Skip mutating routes when true.
                 */
                template <CompiledString N>
                void exposeDatabase(const String &mountPath,
                                    VariantDatabase<N> &db,
                                    bool readOnly = false);

                /**
                 * @brief Mounts a read-only HTTP view over a @ref VariantLookup target.
                 *
                 * Adds one route:
                 *  - @c GET @c <mountPath>/{path:*}  — resolves the
                 *      path-style key against @c VariantLookup<T>
                 *      (slashes mapped to dots) and returns the
                 *      resulting Variant as JSON.
                 *
                 * The target is captured by reference; its lifetime
                 * must outlive the server.
                 *
                 * @tparam T       Any type with a registered VariantLookup.
                 * @param mountPath Route prefix (no trailing slash).
                 * @param target   The instance to resolve against.
                 */
                template <typename T>
                void exposeLookup(const String &mountPath, T &target);

                // ----------------------------------------------------
                // Per-connection tuning (applied to every new connection)
                // ----------------------------------------------------

                /** @brief Sets the per-connection idle timeout. */
                void setIdleTimeoutMs(unsigned int ms) { _idleTimeoutMs = ms; }

                /** @brief Sets the upper bound on a single request body. */
                void setMaxBodyBytes(int64_t bytes) { _maxBodyBytes = bytes; }

                /**
                 * @brief Returns the number of currently live connections.
                 *
                 * Useful as a backpressure heuristic: if this number
                 * is climbing without bound, the application is
                 * dispatching faster than handlers can complete.
                 */
                int connectionCount() const;

#if PROMEKI_ENABLE_TLS
                /**
                 * @brief Attaches an @ref SslContext for TLS termination.
                 *
                 * When set, every accepted connection is wrapped in
                 * an @ref SslSocket configured with @p ctx and the
                 * server-side TLS handshake is driven inside
                 * @ref HttpConnection before any HTTP bytes flow.
                 * The supplied context must already carry a server
                 * certificate + private key.
                 *
                 * Pass an empty pointer to disable TLS again.
                 */
                void setSslContext(SslContext::Ptr ctx) { _sslContext = std::move(ctx); }

                /** @brief Returns the attached SslContext, or null. */
                SslContext::Ptr sslContext() const { return _sslContext; }
#endif

                /** @brief Emitted when a request finishes parsing. @signal */
                PROMEKI_SIGNAL(requestReceived, HttpRequest);

                /** @brief Emitted when a response finishes sending. @signal */
                PROMEKI_SIGNAL(responseSent, HttpRequest, HttpResponse);

                /** @brief Emitted on per-connection or accept errors. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

        private:
                void onNewConnection();
                void dispatchRequest(HttpRequest &request, HttpResponse &response);
                void reapClosedConnection(HttpConnection *conn);

                EventLoop                       *_loop = nullptr;
                UniquePtr<TcpServer>            _tcpServer;
                int                             _acceptHandle = -1;
                HttpRouter                      _router;
                HttpConnection::List            _connections;

                unsigned int                    _idleTimeoutMs = HttpConnection::DefaultIdleTimeoutMs;
                int64_t                         _maxBodyBytes  = HttpConnection::DefaultMaxBodyBytes;
#if PROMEKI_ENABLE_TLS
                SslContext::Ptr                 _sslContext;
#endif

                // Helpers for the reflection adapters; non-template
                // because the per-key plumbing doesn't depend on the
                // database's compile-time Name tag.
                static JsonObject specToJson(const VariantSpec &spec);
                static String     lookupPathToKey(const String &slashPath);
};

// ============================================================
// Reflection adapter template definitions
// ============================================================

template <CompiledString N>
void HttpServer::exposeDatabase(const String &mountPath,
                                VariantDatabase<N> &db,
                                bool readOnly) {
        using DB = VariantDatabase<N>;
        using ID = typename DB::ID;

        // Full snapshot.
        route(mountPath, HttpMethod::Get,
                [&db](const HttpRequest &, HttpResponse &res) {
                        res.setJson(db.toJson());
                });

        // Schema introspection.  Keys not yet present in the
        // database still appear here when a spec was declared at
        // static-init time.
        const String schemaPath = mountPath + "/_schema";
        route(schemaPath, HttpMethod::Get,
                [](const HttpRequest &, HttpResponse &res) {
                        JsonObject out;
                        const auto specs = DB::registeredSpecs();
                        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                                const String name = ID::fromId(it->first).name();
                                out.set(name, HttpServer::specToJson(it->second));
                        }
                        res.setJson(out);
                });

        // Per-key get.
        const String keyPath = mountPath + "/{key}";
        route(keyPath, HttpMethod::Get,
                [&db](const HttpRequest &req, HttpResponse &res) {
                        const String name = req.pathParam("key");
                        ID id = ID::find(name);
                        if(!id.isValid() || !db.contains(id)) {
                                res = HttpResponse::notFound(
                                        String("Unknown key: ") + name);
                                return;
                        }
                        JsonObject out;
                        out.setFromVariant(name, db.get(id));
                        const VariantSpec *sp = DB::spec(id);
                        if(sp != nullptr) {
                                out.set("_spec", HttpServer::specToJson(*sp));
                        }
                        res.setJson(out);
                });

        if(readOnly) return;

        // Per-key set.
        route(keyPath, HttpMethod::Put,
                [&db](const HttpRequest &req, HttpResponse &res) {
                        const String name = req.pathParam("key");
                        Error perr;
                        JsonObject body = req.bodyAsJson(&perr);
                        if(perr.isError() || !body.contains("value")) {
                                res = HttpResponse::badRequest(
                                        "Body must be JSON object with a \"value\" field");
                                return;
                        }
                        // Snapshot the value via JsonObject::forEach,
                        // which converts the underlying nlohmann json
                        // into a typed @ref Variant — that's the path
                        // that setFromJson knows how to coerce
                        // against the registered spec.
                        ID id(name);
                        Variant captured;
                        body.forEach([&](const String &k, const Variant &val) {
                                if(k == "value") captured = val;
                        });
                        if(!db.setFromJson(id, captured)) {
                                res = HttpResponse::badRequest(
                                        "Validation failed for key: " + name);
                                return;
                        }
                        JsonObject out;
                        out.setFromVariant(name, db.get(id));
                        res.setJson(out);
                });

        // Per-key delete.
        route(keyPath, HttpMethod::Delete,
                [&db](const HttpRequest &req, HttpResponse &res) {
                        const String name = req.pathParam("key");
                        ID id = ID::find(name);
                        if(!id.isValid() || !db.contains(id)) {
                                res = HttpResponse::notFound(
                                        String("Unknown key: ") + name);
                                return;
                        }
                        db.remove(id);
                        res = HttpResponse::noContent();
                });
}

template <typename T>
void HttpServer::exposeLookup(const String &mountPath, T &target) {
        const String greedy = mountPath + "/{path:*}";
        route(greedy, HttpMethod::Get,
                [&target](const HttpRequest &req, HttpResponse &res) {
                        const String key =
                                HttpServer::lookupPathToKey(req.pathParam("path"));
                        if(key.isEmpty()) {
                                res = HttpResponse::badRequest("Empty lookup key");
                                return;
                        }
                        Error err;
                        auto v = VariantLookup<T>::resolve(target, key, &err);
                        if(!v.has_value() || err.isError()) {
                                res = HttpResponse::notFound(
                                        String("Unknown lookup: ") + key);
                                return;
                        }
                        JsonObject out;
                        out.setFromVariant("value", *v);
                        res.setJson(out);
                });
}

PROMEKI_NAMESPACE_END
