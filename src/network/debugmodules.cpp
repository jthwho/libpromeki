/**
 * @file      debugmodules.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Internal-only implementation of the @c installPromekiDebugModules
 * helper called by @ref HttpApi::installPromekiAPI.  The individual
 * per-module installers are file-static here because the promeki API
 * is intentionally all-or-nothing — applications that want
 * fine-grained control over what they expose should register their
 * own bespoke endpoints instead.
 *
 * URL layout (every endpoint sits under @c \<api.prefix()>/promeki/):
 *
 * @verbatim
 *   /promeki/                ← debug UI index.html
 *   /promeki/build           ← build info JSON
 *   /promeki/env             ← environment snapshot
 *   /promeki/options         ← LibraryOptions VariantDatabase
 *   /promeki/memspace        ← MemSpace stats
 *   /promeki/log             ← logger status / level / channels
 *   /promeki/log/stream      ← live log WebSocket
 * @endverbatim
 *
 * Each JSON installer registers its routes through @ref HttpApi::route
 * with paths relative to the api's prefix (e.g. @c "/promeki/build")
 * — the api resolves them to the absolute path on the underlying
 * server.  The static debug-frontend mount goes directly through the
 * underlying @ref HttpServer because it isn't a JSON endpoint and
 * doesn't belong in the catalog.
 */

#include <promeki/httpapi.h>
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

namespace {

        // Every promeki-side route nests under "/promeki/" relative to the
        // HttpApi's prefix.  Centralising the suffix keeps the URL layout
        // consistent across modules and makes it trivial to rename later.
        constexpr const char *kPromekiSubpath = "/promeki";

        String promekiPath(const String &leaf) {
                // leaf is expected to start with "/" — empty leaf yields the
                // bare "/promeki" path used by the static UI.
                return String(kPromekiSubpath) + leaf;
        }

        // ============================================================
        // Build info
        // ============================================================

        void installBuildInfoDebugRoutes(HttpApi &api) {
                HttpApi::Endpoint ep;
                ep.path = promekiPath("/build");
                ep.method = HttpMethod::Get;
                ep.title = "Build info";
                ep.summary = "Compiled-in build identification, runtime info, "
                             "feature flags, and the human-readable banner.";
                ep.tags = {"promeki/debug"};
                ep.response = VariantSpec().setDescription("Object with name/version/major/minor/patch/build/"
                                                           "stage/stageNum/repoIdent/ref/ident/date/time/"
                                                           "hostname/type/platform/features/runtime/debug/lines "
                                                           "fields.");

                api.route(ep, [](const HttpRequest &, HttpResponse &res) {
                        const BuildInfo *info = getBuildInfo();
                        JsonObject       body;
                        if (info != nullptr) {
                                body.set("name", info->name);
                                body.set("version", info->version);
                                body.set("major", info->major);
                                body.set("minor", info->minor);
                                body.set("patch", info->patch);
                                body.set("build", info->build);
                                body.set("stage", buildStageName(info->stage));
                                body.set("stageNum", info->stageNum);
                                body.set("repoIdent", info->repoident);
                                body.set("ref", info->ref);
                                body.set("ident", info->ident);
                                body.set("date", info->date);
                                body.set("time", info->time);
                                body.set("hostname", info->hostname);
                                body.set("type", info->type);
                        }
                        body.set("platform", buildPlatformString());
                        body.set("features", buildFeatureString());
                        body.set("runtime", runtimeInfoString());
                        body.set("debug", debugStatusString());

                        const StringList lines = buildInfoStrings();
                        JsonArray        arr;
                        for (const auto &line : lines) arr.add(line);
                        body.set("lines", arr);

                        res.setJson(body);
                });
        }

        // ============================================================
        // Environment variables
        // ============================================================

        void installEnvDebugRoutes(HttpApi &api) {
                HttpApi::Endpoint ep;
                ep.path = promekiPath("/env");
                ep.method = HttpMethod::Get;
                ep.title = "Environment";
                ep.summary = "Snapshot of every environment variable visible to "
                             "the running process.";
                ep.tags = {"promeki/debug"};
                ep.response = VariantSpec().setDescription("Free-form object: keys are environment variable names, "
                                                           "values are their string contents.");

                api.route(ep, [](const HttpRequest &, HttpResponse &res) {
                        JsonObject                body;
                        const Map<String, String> entries = Env::list();
                        for (const auto &[name, value] : entries) {
                                body.set(name, value);
                        }
                        res.setJson(body);
                });
        }

        // ============================================================
        // Library options (read-only via VariantDatabase)
        // ============================================================

        void installLibraryOptionsDebugRoutes(HttpApi &api) {
                api.exposeDatabase(promekiPath("/options"), "Library options", LibraryOptions::instance(),
                                   /*readOnly=*/true);
        }

        // ============================================================
        // Memory stats
        // ============================================================

        JsonObject memSpaceToJson(MemSpace::ID id) {
                MemSpace                        ms(id);
                const MemSpace::Stats::Snapshot snap = ms.stats().snapshot();
                JsonObject                      out;
                out.set("name", ms.name());
                out.set("id", static_cast<uint64_t>(id));
                out.set("allocCount", snap.allocCount);
                out.set("allocBytes", snap.allocBytes);
                out.set("allocFailCount", snap.allocFailCount);
                out.set("maxAllocBytes", snap.maxAllocBytes);
                out.set("releaseCount", snap.releaseCount);
                out.set("releaseBytes", snap.releaseBytes);
                out.set("copyCount", snap.copyCount);
                out.set("copyBytes", snap.copyBytes);
                out.set("copyFailCount", snap.copyFailCount);
                out.set("fillCount", snap.fillCount);
                out.set("fillBytes", snap.fillBytes);
                out.set("liveCount", snap.liveCount);
                out.set("liveBytes", snap.liveBytes);
                out.set("peakCount", snap.peakCount);
                out.set("peakBytes", snap.peakBytes);
                return out;
        }

        void installMemSpaceDebugRoutes(HttpApi &api) {
                HttpApi::Endpoint ep;
                ep.path = promekiPath("/memspace");
                ep.method = HttpMethod::Get;
                ep.title = "Memory stats";
                ep.summary = "Per-MemSpace allocation statistics: alloc/release "
                             "counts and bytes, plus live/peak tallies.";
                ep.tags = {"promeki/debug"};
                ep.response = VariantSpec().setDescription("Object with a single \"spaces\" array; each entry covers "
                                                           "one registered MemSpace.");

                api.route(ep, [](const HttpRequest &, HttpResponse &res) {
                        JsonObject body;
                        JsonArray  spaces;
                        for (MemSpace::ID id : MemSpace::registeredIDs()) {
                                spaces.add(memSpaceToJson(id));
                        }
                        body.set("spaces", spaces);
                        res.setJson(body);
                });
        }

        // ============================================================
        // Logger control
        // ============================================================

        JsonObject loggerStatusJson() {
                Logger    &logger = Logger::defaultLogger();
                JsonObject body;
                body.set("level", logger.level());
                body.set("levelName", String(1, Logger::levelToChar(static_cast<Logger::LogLevel>(logger.level()))));
                body.set("consoleLogging", logger.consoleLoggingEnabled());
                body.set("historySize", static_cast<uint64_t>(logger.historySize()));

                JsonArray channels;
                for (const auto &ch : Logger::debugChannels()) {
                        JsonObject entry;
                        entry.set("name", ch.name);
                        entry.set("file", ch.file);
                        entry.set("line", ch.line);
                        entry.set("enabled", ch.enabled);
                        channels.add(entry);
                }
                body.set("debugChannels", channels);
                return body;
        }

        JsonObject logEntryToJson(const Logger::LogEntry &entry, const String &threadName) {
                JsonObject body;
                body.set("ts", entry.ts.toString("%FT%T.3"));
                body.set("level", static_cast<int>(entry.level));
                body.set("levelChar", String(1, Logger::levelToChar(entry.level)));
                body.set("file", entry.file != nullptr ? String(entry.file) : String());
                body.set("line", entry.line);
                body.set("threadId", entry.threadId);
                body.set("thread", threadName);
                body.set("msg", entry.msg);
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
                EventLoop               *wsLoop = EventLoop::current();
                ObjectBasePtr<WebSocket> wsPtr(ws);

                Logger::ListenerHandle handle = Logger::defaultLogger().installListener(
                        [wsPtr, wsLoop](const Logger::LogEntry &entry, const String &threadName) {
                                const String json = logEntryToJson(entry, threadName).toString();
                                wsLoop->postCallable([wsPtr, json]() mutable {
                                        if (!wsPtr.isValid()) return;
                                        WebSocket *ws = wsPtr.data();
                                        if (!ws->isConnected()) return;
                                        ws->sendTextMessage(json);
                                });
                        },
                        replayCount);

                ws->disconnectedSignal.connect([handle, ws, wsLoop]() {
                        Logger::defaultLogger().removeListener(handle);
                        wsLoop->postCallable([ws]() { delete ws; });
                });
        }

        void installLogDebugRoutes(HttpApi &api) {
                const String base = promekiPath("/log");
                const String levelRel = base + "/level";
                const String debugRel = base + "/debug/{name}";
                const String wsRel = base + "/stream";

                // GET /log — full snapshot.
                {
                        HttpApi::Endpoint ep;
                        ep.path = base;
                        ep.method = HttpMethod::Get;
                        ep.title = "Logger status";
                        ep.summary = "Current log level, console logging flag, "
                                     "history size, and all registered debug "
                                     "channels.";
                        ep.tags = {"promeki/debug", "log"};
                        ep.response = VariantSpec().setDescription("Logger status object.");
                        api.route(ep, [](const HttpRequest &, HttpResponse &res) { res.setJson(loggerStatusJson()); });
                }

                // PUT /log/level — body { "level": <int> }.
                {
                        HttpApi::Endpoint ep;
                        ep.path = levelRel;
                        ep.method = HttpMethod::Put;
                        ep.title = "Set log level";
                        ep.summary = "Updates the minimum log level "
                                     "(0=Force..4=Err).";
                        ep.tags = {"promeki/debug", "log"};
                        ep.params = {HttpApi::Param{
                                .name = "level",
                                .in = HttpApi::ParamIn::Body,
                                .required = true,
                                .spec = VariantSpec()
                                                .setType(Variant::TypeS32)
                                                .setRange(static_cast<int>(Logger::Force),
                                                          static_cast<int>(Logger::Err))
                                                .setDescription("New minimum log level."),
                        }};
                        ep.response = VariantSpec().setDescription("Updated logger status object.");
                        api.route(ep, [](const HttpRequest &req, HttpResponse &res) {
                                Error      perr;
                                JsonObject body = req.bodyAsJson(&perr);
                                if (perr.isError()) {
                                        res = HttpResponse::badRequest("Body must be JSON");
                                        return;
                                }
                                Error   gerr;
                                int64_t level = body.getInt("level", &gerr);
                                if (gerr.isError()) {
                                        res = HttpResponse::badRequest("Body must contain integer \"level\"");
                                        return;
                                }
                                if (level < Logger::Force || level > Logger::Err) {
                                        res = HttpResponse::badRequest("level must be in [0,4]");
                                        return;
                                }
                                Logger::defaultLogger().setLogLevel(static_cast<Logger::LogLevel>(level));
                                res.setJson(loggerStatusJson());
                        });
                }

                // PUT /log/debug/{name} — body { "enabled": <bool> }.
                {
                        HttpApi::Endpoint ep;
                        ep.path = debugRel;
                        ep.method = HttpMethod::Put;
                        ep.title = "Toggle debug channel";
                        ep.summary = "Enables or disables every PROMEKI_DEBUG site "
                                     "with the given name.";
                        ep.tags = {"promeki/debug", "log"};
                        ep.params = {
                                HttpApi::Param{
                                        .name = "name",
                                        .in = HttpApi::ParamIn::Path,
                                        .required = true,
                                        .spec = VariantSpec()
                                                        .setType(Variant::TypeString)
                                                        .setDescription("Channel name."),
                                },
                                HttpApi::Param{
                                        .name = "enabled",
                                        .in = HttpApi::ParamIn::Body,
                                        .required = true,
                                        .spec = VariantSpec()
                                                        .setType(Variant::TypeBool)
                                                        .setDescription("New enabled flag."),
                                },
                        };
                        ep.response = VariantSpec().setDescription("Updated logger status object.");
                        api.route(ep, [](const HttpRequest &req, HttpResponse &res) {
                                const String name = req.pathParam("name");
                                if (name.isEmpty()) {
                                        res = HttpResponse::badRequest("Missing channel name");
                                        return;
                                }
                                Error      perr;
                                JsonObject body = req.bodyAsJson(&perr);
                                if (perr.isError()) {
                                        res = HttpResponse::badRequest("Body must be JSON");
                                        return;
                                }
                                Error gerr;
                                bool  enabled = body.getBool("enabled", &gerr);
                                if (gerr.isError()) {
                                        res = HttpResponse::badRequest("Body must contain boolean \"enabled\"");
                                        return;
                                }
                                if (!Logger::setDebugChannel(name, enabled)) {
                                        res = HttpResponse::notFound(String("Unknown debug channel: ") + name);
                                        return;
                                }
                                res.setJson(loggerStatusJson());
                        });
                }

                // WS /log/stream — WebSocket upgrade.  Not catalogued as a
                // GET endpoint because the catalog/OpenAPI surfaces don't
                // model WebSocket upgrades — clients learn about it via the
                // module documentation.
                api.server().routeWebSocket(api.resolve(wsRel), [](WebSocket *ws, const HttpRequest &req) {
                        const String replayStr = req.queryValue("replay");
                        size_t       replay = 100;
                        if (!replayStr.isEmpty()) {
                                Error   perr;
                                int64_t parsed = replayStr.toInt(&perr);
                                if (perr.isOk() && parsed >= 0) {
                                        replay = static_cast<size_t>(parsed);
                                }
                        }
                        if (replay > MaxReplayCount) replay = MaxReplayCount;
                        attachLogWebSocket(ws, replay);
                });
        }

        // ============================================================
        // Frontend
        // ============================================================

        void installDebugFrontendRoutes(HttpApi &api) {
                // Static UI bundle baked into cirf at :/.PROMEKI/debug/.
                // Goes onto the underlying HttpServer (not the api) because
                // it isn't a JSON endpoint — it shouldn't appear in the
                // catalog.  The router's longest-literal scoring guarantees
                // explicit JSON routes (e.g. /promeki/build) win over this
                // static catch-all.
                HttpServer  &server = api.server();
                const String base = api.resolve(promekiPath(""));
                const String greedy = base + "/{path:*}";
                const String trailing = base + "/";
                auto handler = HttpFileHandler::Ptr::takeOwnership(new HttpFileHandler(Dir(":/.PROMEKI/debug")));
                // Bare base path (no trailing slash) redirects to the
                // canonical trailing-slash form so relative asset URLs in
                // index.html resolve correctly in the browser.
                server.route(base, HttpMethod::Get, [trailing](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        res.setHeader("Location", trailing);
                        res.setText("");
                });
                server.route(greedy, HttpMethod::Get, handler);
        }

} // namespace

// ============================================================
// Single public entry point.  Declared (not in any header) at the
// top of httpapi.cpp.  All-or-nothing on purpose — the bundled
// debug UI assumes the full surface is present.
// ============================================================

void installPromekiDebugModules(HttpApi &api) {
        installBuildInfoDebugRoutes(api);
        installEnvDebugRoutes(api);
        installLibraryOptionsDebugRoutes(api);
        installMemSpaceDebugRoutes(api);
        installLogDebugRoutes(api);
        installDebugFrontendRoutes(api);
}

PROMEKI_NAMESPACE_END
