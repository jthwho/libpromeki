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

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Convenience wrapper that mounts the standard set of debug modules.
 * @ingroup network
 *
 * @ref DebugServer is intentionally thin: it owns one @ref HttpServer
 * and exposes a one-call helper (@ref installDefaultModules) that
 * stitches together every installer in @ref debugmodules.h under a
 * canonical @c "/promeki/debug" mount.  Applications that want to
 * cherry-pick which diagnostics they expose — or attach them to their
 * own user-facing server — can skip this class and call the installer
 * functions directly on any @ref HttpServer.
 *
 * The default API prefix is @c "/promeki/debug/api" and the default UI
 * prefix is @c "/promeki/debug" so the URL space looks like:
 *
 * @verbatim
 * /                        → 302 redirect to /promeki/debug
 * /promeki/debug           → debug UI (Phase 5)
 * /promeki/debug/api/build → build info (Phase 3)
 * /promeki/debug/api/env   → environment snapshot (Phase 3)
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
                /** @brief Default mount path for the JSON API. */
                static const String DefaultApiPrefix;

                /** @brief Default mount path for the debug frontend. */
                static const String DefaultUiPrefix;

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
                 * @brief Mounts every standard debug module.
                 *
                 * Wires the API installers under @p apiPrefix, the
                 * frontend installer under @p uiPrefix, and a 302
                 * redirect from @c "/" to @p uiPrefix.  Safe to call
                 * exactly once before @ref listen — calling it twice
                 * registers duplicate routes.
                 */
                void installDefaultModules(const String &apiPrefix = DefaultApiPrefix,
                                           const String &uiPrefix  = DefaultUiPrefix);

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
};

PROMEKI_NAMESPACE_END
