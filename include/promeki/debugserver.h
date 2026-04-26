/**
 * @file      debugserver.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/httpserver.h>
#include <promeki/httpapi.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Convenience wrapper that mounts the standard set of debug modules.
 * @ingroup network
 *
 * @ref DebugServer is intentionally thin: it owns one @ref HttpServer,
 * one @ref HttpApi attached to it, and exposes a one-call helper
 * (@ref installDefaultModules) that stitches together every installer
 * in @ref debugmodules.h under the canonical @c "/api" base prefix.
 * Applications that want to cherry-pick which diagnostics they expose
 * — or attach them to their own user-facing server — can skip this
 * class and call the installer functions directly on any
 * @ref HttpApi.
 *
 * Because the debug routes are registered through the embedded
 * @ref HttpApi, every endpoint shows up in the api explorer, the
 * @c /_catalog JSON list, and the @c /_openapi document
 * automatically — the debug API is itself self-describing.
 *
 * The base prefix defaults to @c "/api", which lays out the URL space
 * as follows:
 *
 * @verbatim
 * /                  → 302 redirect to /api/promeki/
 * /api/               → built-in interactive API explorer
 * /api/_catalog      → promeki-native API catalog
 * /api/_openapi      → OpenAPI 3.1 doc covering the registered API
 * /api/promeki/      → debug UI index.html
 * /api/promeki/build → build info JSON
 * /api/promeki/env   → environment snapshot
 * /api/promeki/log   → logger control
 * ...
 * @endverbatim
 *
 * @par Lifetime
 * Construct a @ref DebugServer on whatever @ref EventLoop should run
 * its handlers — typically the application's main loop.  Call
 * @ref listen with the address to bind, then leave it alive for the
 * lifetime of the process.  The destructor closes the listener and
 * tears down all live connections.
 *
 * @par Example
 * @code
 * Application app(argc, argv);
 * DebugServer dbg;
 * dbg.installDefaultModules();
 * if(Error err = dbg.listen(8085); err.isError()) {
 *         promekiWarn("debug server failed: %s", err.toString().cstr());
 * }
 * return app.exec();
 * @endcode
 */
class DebugServer : public ObjectBase {
        PROMEKI_OBJECT(DebugServer, ObjectBase)
        public:
                /** @brief Default base prefix for the API surface. */
                static const String DefaultApiPrefix;

                /** @brief Default loopback bind address ("127.0.0.1"). */
                static const String DefaultBindHost;

                /** @brief Constructs a debug server on the current EventLoop. */
                explicit DebugServer(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes the listening socket. */
                ~DebugServer() override;

                /**
                 * @brief Starts listening on @p address.
                 * @return @ref Error::Ok on success or a system error.
                 */
                Error listen(const SocketAddress &address);

                /**
                 * @brief Convenience: listen on @ref DefaultBindHost and @p port.
                 *
                 * Defaults to loopback so debug endpoints aren't
                 * accidentally exposed to a network — pass an explicit
                 * @ref SocketAddress to bind to other interfaces.
                 */
                Error listen(uint16_t port);

                /** @brief Stops listening and closes all connections. */
                void close();

                /** @brief True between successful @ref listen and @ref close. */
                bool isListening() const;

                /** @brief Address the server is bound to (post-listen). */
                SocketAddress serverAddress() const;

                /** @brief Returns the underlying @ref HttpServer for custom routes. */
                HttpServer &httpServer() { return _server; }

                /** @copydoc httpServer */
                const HttpServer &httpServer() const { return _server; }

                /**
                 * @brief Returns the embedded @ref HttpApi.
                 *
                 * Use this to register additional self-describing
                 * endpoints alongside the standard debug modules — they
                 * will share the same catalog, OpenAPI document, and
                 * explorer UI.
                 */
                HttpApi &httpApi() { return _api; }

                /** @copydoc httpApi */
                const HttpApi &httpApi() const { return _api; }

                /**
                 * @brief Mounts the full promeki debug surface.
                 *
                 * Calls @ref HttpApi::installPromekiAPI to install
                 * every debug installer under @c \<prefix>/promeki/,
                 * mounts the API's interactive explorer at
                 * @c \<prefix>/ alongside @c /_catalog and
                 * @c /_openapi, and adds a 302 redirect from @c "/"
                 * to the debug UI at @c \<prefix>/promeki/.
                 *
                 * The root redirect is exclusive to @ref DebugServer:
                 * applications that install the promeki API into
                 * their own server (via @ref HttpApi::installPromekiAPI)
                 * keep control of @c "/" themselves.
                 *
                 * Safe to call exactly once before @ref listen.
                 */
                void installDefaultModules();

                /**
                 * @brief Parses an environment-variable spec into a SocketAddress.
                 *
                 * Accepts the forms used by @c PROMEKI_DEBUG_SERVER:
                 *  - @c ":1234"          → @ref DefaultBindHost + port (loopback).
                 *  - @c "host:port"      → exactly that.
                 *  - @c "[::1]:port"     → IPv6 with brackets.
                 *
                 * Returns the parsed address on success, or a default
                 * @ref SocketAddress and @ref Error::Invalid on parse
                 * failure.
                 */
                static Result<SocketAddress> parseSpec(const String &spec);

        private:
                HttpServer      _server;
                HttpApi         _api;
};

PROMEKI_NAMESPACE_END
