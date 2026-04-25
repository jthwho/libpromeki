/**
 * @file      debugmodules.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class HttpServer;

/**
 * @defgroup debugmodules Debug HTTP modules
 * @ingroup network
 * @brief Reusable installers that mount diagnostic endpoints onto an @ref HttpServer.
 *
 * Each installer is intentionally a free function rather than a method
 * on @ref DebugServer so applications can pick and choose which
 * diagnostics to expose on their own user-facing @ref HttpServer.  The
 * @ref DebugServer convenience wrapper simply calls them all.
 *
 * The @p apiPrefix / @p uiPrefix arguments are the route prefixes the
 * module should mount under — they are concatenated verbatim with
 * each route's relative path, so callers control the full URL space.
 */

/**
 * @brief Mounts logger control endpoints onto @p server.
 * @ingroup debugmodules
 *
 * Registers (Phase 2 skeleton):
 *  - @c GET @c \<apiPrefix>/logger — returns a stub status JSON.
 *
 * Phase 3 fills in level get/set, debug-flag listing/toggle, and the
 * Phase 4 patch wires up the WebSocket log stream.
 */
void installLoggerDebugRoutes(HttpServer &server, const String &apiPrefix);

/**
 * @brief Mounts a read-only @ref LibraryOptions view onto @p server.
 * @ingroup debugmodules
 *
 * Phase 2 stub: registers @c GET @c \<apiPrefix>/options returning
 * @c {"module":"options","status":"stub"}.  Phase 3 replaces this with
 * a full @ref HttpServer::exposeDatabase mount in read-only mode.
 */
void installLibraryOptionsDebugRoutes(HttpServer &server, const String &apiPrefix);

/**
 * @brief Mounts an environment-variable snapshot endpoint onto @p server.
 * @ingroup debugmodules
 *
 * Phase 2 stub: registers @c GET @c \<apiPrefix>/env returning a stub
 * JSON.  Phase 3 returns the full process environment.
 */
void installEnvDebugRoutes(HttpServer &server, const String &apiPrefix);

/**
 * @brief Mounts a build-info endpoint onto @p server.
 * @ingroup debugmodules
 *
 * Phase 2 stub: registers @c GET @c \<apiPrefix>/build returning a stub
 * JSON.  Phase 3 fills in the real @ref buildInfoStrings payload.
 */
void installBuildInfoDebugRoutes(HttpServer &server, const String &apiPrefix);

/**
 * @brief Mounts a memory-statistics endpoint onto @p server.
 * @ingroup debugmodules
 *
 * Phase 2 stub: registers @c GET @c \<apiPrefix>/memory returning a stub
 * JSON.  Phase 3 fills in the @ref MemSpace registry snapshot.
 */
void installMemoryDebugRoutes(HttpServer &server, const String &apiPrefix);

/**
 * @brief Mounts the baked-in debug frontend onto @p server.
 * @ingroup debugmodules
 *
 * Phase 2 stub: returns 503 from @p uiPrefix until Phase 5 lands the
 * static asset bundle under @c :/.PROMEKI/debug/ and switches this to
 * an @ref HttpFileHandler-based mount.
 */
void installDebugFrontendRoutes(HttpServer &server, const String &uiPrefix);

/**
 * @brief Adds a 302 redirect from @c "/" to @p uiPrefix.
 * @ingroup debugmodules
 *
 * Useful when the debug server owns the entire URL space — pointing the
 * server's root at the frontend means a user that browses to
 * @c http://host:port/ lands on the debug UI.
 */
void installDebugRootRedirect(HttpServer &server, const String &uiPrefix);

PROMEKI_NAMESPACE_END
