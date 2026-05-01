/**
 * @file      httpserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpserver.h>
#include <promeki/tcpserver.h>
#include <promeki/tcpsocket.h>
#include <promeki/eventloop.h>
#include <promeki/application.h>
#include <promeki/logger.h>
#include <promeki/objectbase.tpp>
#include <promeki/websocket.h>
#if PROMEKI_ENABLE_TLS
#include <promeki/sslsocket.h>
#endif

#include <promeki/variant.h>
#include <promeki/variantspec.h>
#include <promeki/json.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(HttpServer);

// ============================================================
// Reflection-adapter helpers (non-template, called from the
// template definitions in the header).
// ============================================================

JsonObject HttpServer::specToJson(const VariantSpec &spec) {
        JsonObject out;

        // Accepted type list.  Even when the spec only accepts one
        // type we render an array for shape consistency — clients
        // can always look at index 0.
        JsonArray types;
        for (size_t i = 0; i < spec.types().size(); ++i) {
                types.add(String(Variant::typeName(spec.types()[i])));
        }
        out.set("types", types);

        if (spec.defaultValue().isValid()) {
                out.setFromVariant("default", spec.defaultValue());
        }
        if (spec.hasMin()) out.setFromVariant("min", spec.rangeMin());
        if (spec.hasMax()) out.setFromVariant("max", spec.rangeMax());
        if (!spec.description().isEmpty()) {
                out.set("description", spec.description());
        }
        return out;
}

String HttpServer::lookupPathToKey(const String &slashPath) {
        // VariantLookup keys are dot-separated with optional [N]
        // index suffixes.  HTTP paths use slashes and bare integer
        // segments — we convert "foo/bar/3/baz" into "foo.bar[3].baz".
        if (slashPath.isEmpty()) return String();
        StringList parts = slashPath.split("/");
        String     out;
        for (size_t i = 0; i < parts.size(); ++i) {
                const String &p = parts[i];
                if (p.isEmpty()) continue;
                // Index segments are pure decimal digits.  Anything
                // else is a name segment.
                bool allDigits = true;
                for (size_t j = 0; j < p.byteCount(); ++j) {
                        const char c = p.cstr()[j];
                        if (c < '0' || c > '9') {
                                allDigits = false;
                                break;
                        }
                }
                if (allDigits && !out.isEmpty()) {
                        out += "[";
                        out += p;
                        out += "]";
                } else {
                        if (!out.isEmpty()) out += ".";
                        out += p;
                }
        }
        return out;
}

HttpServer::HttpServer(ObjectBase *parent) : ObjectBase(parent), _tcpServer(UniquePtr<TcpServer>::create(this)) {
        // Capture the owning loop at construction time.  When invoked
        // before any EventLoop has been started on this thread, fall
        // back to the application's main loop so listen() succeeds in
        // the typical "construct before exec()" pattern.
        _loop = EventLoop::current();
        if (_loop == nullptr) _loop = Application::mainEventLoop();
}

HttpServer::~HttpServer() {
        close();
}

// ============================================================
// Listen / close
// ============================================================

Error HttpServer::listen(const SocketAddress &address, int backlog) {
        if (_tcpServer->isListening()) return Error::AlreadyOpen;

        if (_loop == nullptr) _loop = EventLoop::current();
        if (_loop == nullptr) _loop = Application::mainEventLoop();
        if (_loop == nullptr) return Error::Invalid;

        Error err = _tcpServer->listen(address, backlog);
        if (err.isError()) return err;

        // Register the listening socket with the EventLoop so
        // accept() runs whenever the kernel reports the fd readable.
        // We drive nextPendingConnection() in a loop inside
        // onNewConnection() so a thundering herd of simultaneous
        // accepts gets fully drained on a single wake.
        const int fd = _tcpServer->socketDescriptor();
        if (fd < 0) return Error::Invalid;

        // Drive accept from the loop, so the listening socket needs
        // to be non-blocking — otherwise nextPendingConnection()
        // would stall once the kernel queue is drained.
        _tcpServer->setNonBlocking(true);

        _acceptHandle = _loop->addIoSource(fd, EventLoop::IoRead, [this](int, uint32_t) { onNewConnection(); });
        if (_acceptHandle < 0) {
                _tcpServer->close();
                return Error::Invalid;
        }
        return Error::Ok;
}

Error HttpServer::listen(uint16_t port, int backlog) {
        return listen(SocketAddress::any(port), backlog);
}

void HttpServer::close() {
        if (_loop != nullptr && _acceptHandle >= 0) {
                _loop->removeIoSource(_acceptHandle);
                _acceptHandle = -1;
        }
        if (_tcpServer->isListening()) _tcpServer->close();

        // Closing connections triggers their `closed` signal which
        // we use to reap them — but closing while iterating mutates
        // the list.  Snapshot first, then close each.
        HttpConnection::List snapshot = _connections;
        _connections.clear();
        for (size_t i = 0; i < snapshot.size(); ++i) {
                if (snapshot[i] != nullptr) {
                        snapshot[i]->close();
                        delete snapshot[i];
                }
        }
}

bool HttpServer::isListening() const {
        return _tcpServer->isListening();
}

SocketAddress HttpServer::serverAddress() const {
        return _tcpServer->serverAddress();
}

// ============================================================
// Routing convenience forwards
// ============================================================

void HttpServer::route(const String &pattern, const HttpMethod &method, HttpHandlerFunc handler) {
        _router.route(pattern, method, std::move(handler));
}

void HttpServer::route(const String &pattern, const HttpMethod &method, HttpHandler::Ptr handler) {
        _router.route(pattern, method, std::move(handler));
}

void HttpServer::any(const String &pattern, HttpHandlerFunc handler) {
        _router.any(pattern, std::move(handler));
}

void HttpServer::use(HttpMiddleware middleware) {
        _router.use(std::move(middleware));
}

void HttpServer::routeWebSocket(const String &pattern, WebSocketHandler handler) {
        // Inner handler runs on the server's EventLoop thread.  We
        // capture the user's WebSocketHandler by value so multiple
        // concurrent connections each get their own copy of the
        // callback.
        WebSocketHandler userHandler = std::move(handler);
        _router.route(pattern, HttpMethod::Get, [userHandler](const HttpRequest &req, HttpResponse &res) {
                // Validate the upgrade preamble.  Both the
                // Upgrade and Connection headers MUST be set
                // (RFC 6455 §4.2.1) and the version must be
                // 13.  We lower-case the incoming header
                // values for comparison.
                const String upgrade = req.header("Upgrade").toLower();
                const String connection = req.header("Connection").toLower();
                const String key = req.header("Sec-WebSocket-Key");
                const String ver = req.header("Sec-WebSocket-Version");
                if (upgrade != "websocket" || !connection.contains("upgrade") || key.isEmpty() || ver != "13") {
                        res = HttpResponse::badRequest("Invalid WebSocket upgrade request");
                        return;
                }

                // Build the 101 response.
                res.setStatus(HttpStatus::SwitchingProtocols);
                res.setHeader("Upgrade", "websocket");
                res.setHeader("Connection", "Upgrade");
                res.setHeader("Sec-WebSocket-Accept", WebSocket::computeAcceptValue(key));

                // Echo the first requested subprotocol if any.
                const String wantedProto = req.header("Sec-WebSocket-Protocol");
                if (!wantedProto.isEmpty()) {
                        StringList parts = wantedProto.split(",");
                        if (parts.size() > 0) {
                                res.setHeader("Sec-WebSocket-Protocol", parts[0].trim());
                        }
                }

                // Install the upgrade hook.  Captured by
                // value so it stays alive on the response
                // copy that HttpConnection serializes.  We
                // also capture a snapshot of the request so
                // the user handler can read the query string,
                // headers, and path-params it was routed by.
                HttpRequest reqCopy = req;
                res.setUpgradeHook([userHandler, reqCopy](TcpSocket *sock) {
                        if (sock == nullptr) return;
                        WebSocket *ws = new WebSocket();
                        ws->adoptUpgradedSocket(sock);
                        userHandler(ws, reqCopy);
                });
        });
}

int HttpServer::connectionCount() const {
        return static_cast<int>(_connections.size());
}

// ============================================================
// Accept + dispatch
// ============================================================

void HttpServer::onNewConnection() {
        // Drain whatever the TcpServer has queued.  In the steady
        // state this loop runs once per signal, but if multiple
        // accepts piled up between wakes we want to take all of
        // them in one pass.  The listening socket is non-blocking
        // (set in listen()) so this terminates on EAGAIN.
        for (;;) {
                const int fd = _tcpServer->nextPendingDescriptor();
                if (fd < 0) break;

                // Wrap the descriptor in either a plain TcpSocket
                // or an SslSocket (when TLS is configured).  The
                // sockets share an inheritance hierarchy so the
                // rest of the connection-handling code is unaware
                // of the difference apart from the explicit
                // setNeedsServerHandshake() hint below.
                TcpSocket *sock = nullptr;
                bool       needsHandshake = false;
#if PROMEKI_ENABLE_TLS
                if (_sslContext.isValid()) {
                        SslSocket *ssl = new SslSocket();
                        ssl->setSslContext(_sslContext);
                        sock = ssl;
                        needsHandshake = true;
                } else {
                        sock = new TcpSocket();
                }
#else
                sock = new TcpSocket();
#endif
                sock->setSocketDescriptor(fd);

                HttpConnection *conn = new HttpConnection(sock, this);
                conn->setIdleTimeoutMs(_idleTimeoutMs);
                conn->setMaxBodyBytes(_maxBodyBytes);
                if (needsHandshake) conn->setNeedsServerHandshake();
                conn->setRequestHandler([this](HttpRequest &req, HttpResponse &res) { dispatchRequest(req, res); });

                // Forward per-connection signals up to the server-
                // level signal, then reap the connection on close.
                conn->requestReceivedSignal.connect([this](HttpRequest req) { requestReceivedSignal.emit(req); }, this);
                conn->responseSentSignal.connect(
                        [this](HttpRequest req, HttpResponse res) { responseSentSignal.emit(req, res); }, this);
                conn->errorOccurredSignal.connect([this](Error err) { errorOccurredSignal.emit(err); }, this);
                conn->closedSignal.connect([this, conn]() { reapClosedConnection(conn); }, this);

                Error err = conn->start();
                if (err.isError()) {
                        promekiWarn("HttpServer: failed to start connection: %s", err.name().cstr());
                        delete conn;
                        continue;
                }
                _connections.pushToBack(conn);
        }
}

void HttpServer::dispatchRequest(HttpRequest &request, HttpResponse &response) {
        _router.dispatch(request, response);
}

void HttpServer::reapClosedConnection(HttpConnection *conn) {
        for (size_t i = 0; i < _connections.size(); ++i) {
                if (_connections[i] == conn) {
                        // Schedule deletion onto the loop rather than
                        // delete inline — the closed signal we're
                        // responding to was emitted from inside the
                        // connection's own machinery.  @c deleteLater
                        // also detaches @p conn from its parent so the
                        // deferred delete cannot race with
                        // @c ObjectBase::destroyChildren when
                        // @c ~HttpServer runs before the callable is
                        // dispatched.
                        if (_loop != nullptr) {
                                conn->deleteLater();
                        } else {
                                delete conn;
                        }
                        // O(n) erase — fine for small live counts;
                        // typical real-world HTTP servers see hundreds
                        // not millions of concurrent connections.
                        for (size_t j = i + 1; j < _connections.size(); ++j) {
                                _connections[j - 1] = _connections[j];
                        }
                        _connections.popFromBack();
                        return;
                }
        }
}

PROMEKI_NAMESPACE_END
