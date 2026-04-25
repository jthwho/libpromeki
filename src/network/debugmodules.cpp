/**
 * @file      debugmodules.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/debugmodules.h>
#include <promeki/httpserver.h>
#include <promeki/httpfilehandler.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpmethod.h>
#include <promeki/httpstatus.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/libraryoptions.h>
#include <promeki/buildinfo.h>
#include <promeki/env.h>
#include <promeki/memspace.h>
#include <promeki/eventloop.h>
#include <promeki/websocket.h>
#include <promeki/dir.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================
// Build info
// ============================================================

void installBuildInfoDebugRoutes(HttpServer &server, const String &apiPrefix) {
        const String route = apiPrefix + "/build";
        server.route(route, HttpMethod::Get,
                [](const HttpRequest &, HttpResponse &res) {
                        const BuildInfo *info = getBuildInfo();
                        JsonObject body;
                        if(info != nullptr) {
                                body.set("name",        info->name);
                                body.set("version",     info->version);
                                body.set("repoIdent",   info->repoident);
                                body.set("date",        info->date);
                                body.set("time",        info->time);
                                body.set("hostname",    info->hostname);
                                body.set("type",        info->type);
                                body.set("betaVersion", info->betaVersion);
                                body.set("rcVersion",   info->rcVersion);
                        }
                        body.set("platform", buildPlatformString());
                        body.set("features", buildFeatureString());
                        body.set("runtime",  runtimeInfoString());
                        body.set("debug",    debugStatusString());

                        // Mirror the human-readable banner under "lines"
                        // so a UI can display the same multi-line block
                        // the logger emits at startup.
                        const StringList lines = buildInfoStrings();
                        JsonArray arr;
                        for(const auto &line : lines) arr.add(line);
                        body.set("lines", arr);

                        res.setJson(body);
                });
}

// ============================================================
// Environment variables
// ============================================================

void installEnvDebugRoutes(HttpServer &server, const String &apiPrefix) {
        const String route = apiPrefix + "/env";
        server.route(route, HttpMethod::Get,
                [](const HttpRequest &, HttpResponse &res) {
                        JsonObject body;
                        const Map<String, String> entries = Env::list();
                        for(const auto &[name, value] : entries) {
                                body.set(name, value);
                        }
                        res.setJson(body);
                });
}

// ============================================================
// Library options (read-only via VariantDatabase)
// ============================================================

void installLibraryOptionsDebugRoutes(HttpServer &server, const String &apiPrefix) {
        const String mount = apiPrefix + "/options";
        server.exposeDatabase(mount, LibraryOptions::instance(), /*readOnly=*/true);
}

// ============================================================
// Memory stats
// ============================================================

namespace {

JsonObject memSpaceToJson(MemSpace::ID id) {
        MemSpace ms(id);
        const MemSpace::Stats::Snapshot snap = ms.stats().snapshot();
        JsonObject out;
        out.set("name",           ms.name());
        out.set("id",             static_cast<uint64_t>(id));
        out.set("allocCount",     snap.allocCount);
        out.set("allocBytes",     snap.allocBytes);
        out.set("allocFailCount", snap.allocFailCount);
        out.set("maxAllocBytes",  snap.maxAllocBytes);
        out.set("releaseCount",   snap.releaseCount);
        out.set("releaseBytes",   snap.releaseBytes);
        out.set("copyCount",      snap.copyCount);
        out.set("copyBytes",      snap.copyBytes);
        out.set("copyFailCount",  snap.copyFailCount);
        out.set("fillCount",      snap.fillCount);
        out.set("fillBytes",      snap.fillBytes);
        out.set("liveCount",      snap.liveCount);
        out.set("liveBytes",      snap.liveBytes);
        out.set("peakCount",      snap.peakCount);
        out.set("peakBytes",      snap.peakBytes);
        return out;
}

}  // namespace

void installMemoryDebugRoutes(HttpServer &server, const String &apiPrefix) {
        const String route = apiPrefix + "/memory";
        server.route(route, HttpMethod::Get,
                [](const HttpRequest &, HttpResponse &res) {
                        JsonObject body;
                        JsonArray spaces;
                        for(MemSpace::ID id : MemSpace::registeredIDs()) {
                                spaces.add(memSpaceToJson(id));
                        }
                        body.set("spaces", spaces);
                        res.setJson(body);
                });
}

// ============================================================
// Logger control
// ============================================================

namespace {

JsonObject loggerStatusJson() {
        Logger &logger = Logger::defaultLogger();
        JsonObject body;
        body.set("level",   logger.level());
        body.set("levelName",
                String(1, Logger::levelToChar(static_cast<Logger::LogLevel>(
                                logger.level()))));
        body.set("consoleLogging", logger.consoleLoggingEnabled());
        body.set("historySize",    static_cast<uint64_t>(logger.historySize()));

        JsonArray channels;
        for(const auto &ch : Logger::debugChannels()) {
                JsonObject entry;
                entry.set("name",    ch.name);
                entry.set("file",    ch.file);
                entry.set("line",    ch.line);
                entry.set("enabled", ch.enabled);
                channels.add(entry);
        }
        body.set("debugChannels", channels);
        return body;
}

}  // namespace

namespace {

JsonObject logEntryToJson(const Logger::LogEntry &entry, const String &threadName) {
        JsonObject body;
        body.set("ts",        entry.ts.toString("%FT%T.3"));
        body.set("level",     static_cast<int>(entry.level));
        body.set("levelChar", String(1, Logger::levelToChar(entry.level)));
        body.set("file",      entry.file != nullptr ? String(entry.file) : String());
        body.set("line",      entry.line);
        body.set("threadId",  entry.threadId);
        body.set("thread",    threadName);
        body.set("msg",       entry.msg);
        return body;
}

// Maximum number of replay entries a client may request via the
// `?replay=` query parameter.  Caps catastrophic flooding if a client
// asks for replay larger than the configured history.
constexpr size_t MaxReplayCount = 16384;

// Wires a single WebSocket connection up to the Logger as a streaming
// listener.  Uses a heap-allocated session shared between the worker-
// thread listener and the WebSocket-loop send path so the lifetime is
// independent of either stack frame.
void attachLogWebSocket(WebSocket *ws, size_t replayCount) {
        EventLoop *wsLoop = EventLoop::current();
        ObjectBasePtr<WebSocket> wsPtr(ws);

        // Install the listener.  The lambda body runs on the logger
        // worker thread; it serializes the entry once and posts the
        // formatted string to wsLoop for transmission.  Posting (not
        // sending directly) is required because WebSocket send paths
        // mutate event-loop state and must run on the owning loop.
        Logger::ListenerHandle handle =
                Logger::defaultLogger().installListener(
                        [wsPtr, wsLoop](const Logger::LogEntry &entry,
                                        const String &threadName) {
                                const String json =
                                        logEntryToJson(entry, threadName).toString();
                                wsLoop->postCallable([wsPtr, json]() mutable {
                                        if(!wsPtr.isValid()) return;
                                        WebSocket *ws = wsPtr.data();
                                        if(!ws->isConnected()) return;
                                        ws->sendTextMessage(json);
                                });
                        }, replayCount);

        // Tear down on disconnect.  removeListener() blocks until the
        // worker thread acknowledges removal — which is exactly when
        // the last in-flight listener invocation has completed and
        // posted its (final) callable to wsLoop.  Subsequent posts to
        // wsLoop find an invalid wsPtr (after delete) and skip.
        ws->disconnectedSignal.connect([handle, ws, wsLoop]() {
                Logger::defaultLogger().removeListener(handle);
                // Defer the delete onto wsLoop so any callable the
                // listener posted before removeListener returned drains
                // first.  postCallable preserves FIFO order on the loop.
                wsLoop->postCallable([ws]() { delete ws; });
        });
}

}  // namespace

void installLoggerDebugRoutes(HttpServer &server, const String &apiPrefix) {
        const String statusRoute = apiPrefix + "/logger";
        const String levelRoute  = apiPrefix + "/logger/level";
        const String debugRoute  = apiPrefix + "/logger/debug/{name}";
        const String wsRoute     = apiPrefix + "/logger/stream";

        // GET /logger — full snapshot.
        server.route(statusRoute, HttpMethod::Get,
                [](const HttpRequest &, HttpResponse &res) {
                        res.setJson(loggerStatusJson());
                });

        // PUT /logger/level — body { "level": <int> } sets the
        // minimum log level.  Out-of-range values are rejected so the
        // caller doesn't accidentally clamp to Force or push above Err.
        server.route(levelRoute, HttpMethod::Put,
                [](const HttpRequest &req, HttpResponse &res) {
                        Error perr;
                        JsonObject body = req.bodyAsJson(&perr);
                        if(perr.isError()) {
                                res = HttpResponse::badRequest("Body must be JSON");
                                return;
                        }
                        Error gerr;
                        int64_t level = body.getInt("level", &gerr);
                        if(gerr.isError()) {
                                res = HttpResponse::badRequest(
                                        "Body must contain integer \"level\"");
                                return;
                        }
                        if(level < Logger::Force || level > Logger::Err) {
                                res = HttpResponse::badRequest(
                                        "level must be in [0,4]");
                                return;
                        }
                        Logger::defaultLogger().setLogLevel(
                                static_cast<Logger::LogLevel>(level));
                        res.setJson(loggerStatusJson());
                });

        // PUT /logger/debug/{name} — body { "enabled": <bool> }
        // toggles every PROMEKI_DEBUG site bearing that name.
        server.route(debugRoute, HttpMethod::Put,
                [](const HttpRequest &req, HttpResponse &res) {
                        const String name = req.pathParam("name");
                        if(name.isEmpty()) {
                                res = HttpResponse::badRequest("Missing channel name");
                                return;
                        }
                        Error perr;
                        JsonObject body = req.bodyAsJson(&perr);
                        if(perr.isError()) {
                                res = HttpResponse::badRequest("Body must be JSON");
                                return;
                        }
                        Error gerr;
                        bool enabled = body.getBool("enabled", &gerr);
                        if(gerr.isError()) {
                                res = HttpResponse::badRequest(
                                        "Body must contain boolean \"enabled\"");
                                return;
                        }
                        if(!Logger::setDebugChannel(name, enabled)) {
                                res = HttpResponse::notFound(
                                        String("Unknown debug channel: ") + name);
                                return;
                        }
                        res.setJson(loggerStatusJson());
                });

        // WS /logger/stream — streams every future log entry as a
        // JSON text frame.  Optional `?replay=<n>` delivers the last
        // n history entries before subscribing; absent or unparseable
        // values use a sane default and large values are clamped to
        // the configured history size by the listener itself.
        server.routeWebSocket(wsRoute,
                [](WebSocket *ws, const HttpRequest &req) {
                        const String replayStr = req.queryValue("replay");
                        size_t replay = 100;
                        if(!replayStr.isEmpty()) {
                                Error perr;
                                int64_t parsed = replayStr.toInt(&perr);
                                if(perr.isOk() && parsed >= 0) {
                                        replay = static_cast<size_t>(parsed);
                                }
                        }
                        if(replay > MaxReplayCount) replay = MaxReplayCount;
                        attachLogWebSocket(ws, replay);
                });
}

// ============================================================
// Frontend
// ============================================================

void installDebugFrontendRoutes(HttpServer &server, const String &uiPrefix) {
        // Mount the baked-in :/.PROMEKI/debug/ resource folder under
        // the UI prefix.  HttpFileHandler understands cirf-style
        // resource paths transparently, so this works whether the
        // running binary serves the assets from the compiled-in
        // resource set or from an on-disk override.
        const String greedy = uiPrefix + "/{path:*}";
        const String trailing = uiPrefix + "/";
        auto handler = HttpFileHandler::Ptr::takeOwnership(
                new HttpFileHandler(Dir(":/.PROMEKI/debug")));
        // Bare uiPrefix (no trailing slash) redirects to the
        // canonical trailing-slash form so relative asset URLs in
        // index.html resolve correctly in the browser.
        server.route(uiPrefix, HttpMethod::Get,
                [trailing](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        res.setHeader("Location", trailing);
                        res.setText("");
                });
        server.route(greedy, HttpMethod::Get, handler);
}

// ============================================================
// Root redirect
// ============================================================

void installDebugRootRedirect(HttpServer &server, const String &uiPrefix) {
        server.route("/", HttpMethod::Get,
                [uiPrefix](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        res.setHeader("Location", uiPrefix);
                        res.setText("");
                });
}

PROMEKI_NAMESPACE_END
