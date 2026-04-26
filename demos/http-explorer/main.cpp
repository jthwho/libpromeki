/**
 * @file      http-explorer/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Walks through the typical layering an application uses to publish
 * a self-describing HTTP API:
 *
 *   1. Construct a plain @ref HttpServer.
 *   2. Construct an @ref HttpApi over the server with a chosen
 *      prefix — every endpoint registered through the api lives
 *      under it, and the catalog/OpenAPI/explorer trio mount there.
 *   3. Register the application's own endpoints (a VariantDatabase,
 *      a VariantLookup, and one RPC are demonstrated).  Endpoint
 *      paths are RELATIVE to the api's prefix.
 *   4. Mount the application's own static UI (index.html + app.js
 *      + style.css) at "/".  The assets are bundled into the
 *      demo binary as a cirf resource set under @c :/demo/explorer/.
 *   5. Opt in to the bundled promeki API surface (build/env/options/
 *      memspace/log/debug UI) — it nests under @c \<prefix>/promeki/.
 *   6. Mount the catalog/OpenAPI/explorer trio.
 *   7. Listen.
 *
 * @ref DebugServer wraps steps 1-2-5-6 into one call for cases where
 * the only thing you want is a quick debugging server.  This demo
 * deliberately spells the steps out so the layering is visible.
 *
 * Usage:
 *   http-explorer-demo [--port N] [--bind HOST]
 *     --port  TCP port to listen on (default 8085).
 *     --bind  Bind address (default 127.0.0.1).
 *
 * Once running, browse to:
 *   http://127.0.0.1:8085/api/
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <promeki/application.h>
#include <promeki/dir.h>
#include <promeki/error.h>
#include <promeki/httpapi.h>
#include <promeki/httpfilehandler.h>
#include <promeki/httpserver.h>
#include <promeki/logger.h>
#include <promeki/resource.h>
#include <promeki/result.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantlookup.h>
#include <promeki/variantspec.h>

using namespace promeki;

// ============================================================
// DemoConfig — a tiny VariantDatabase whose keys exercise a few
// of the VariantSpec features (range, default, enum, polymorphic).
// ============================================================

class DemoConfig : public VariantDatabase<"DemoConfig"> {
        public:
                using Base = VariantDatabase<"DemoConfig">;
                using Base::Base;

                /** @brief Process-wide instance the demo binds to. */
                static DemoConfig &instance() {
                        static DemoConfig db;
                        return db;
                }

                PROMEKI_DECLARE_ID(Greeting,
                        VariantSpec()
                                .setType(Variant::TypeString)
                                .setDefault(String("hello, promeki"))
                                .setDescription("Greeting string returned by /echo."));

                PROMEKI_DECLARE_ID(Quality,
                        VariantSpec()
                                .setType(Variant::TypeS32)
                                .setDefault(85)
                                .setRange(1, 100)
                                .setDescription("JPEG-style quality, 1..100."));

                PROMEKI_DECLARE_ID(Verbose,
                        VariantSpec()
                                .setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("If true, /echo includes a sequence "
                                                "number alongside the text."));

                PROMEKI_DECLARE_ID(MaxSize,
                        VariantSpec()
                                .setType(Variant::TypeU32)
                                .setDefault(uint32_t{1024})
                                .setRange(uint32_t{1}, uint32_t{1u << 20})
                                .setDescription("Upper bound on the echoed message "
                                                "length (bytes)."));
};

// ============================================================
// DemoState — a plain class with a registered VariantLookup so
// the /api/demo/state endpoint can resolve paths against it.
// ============================================================

class DemoState {
        public:
                String name      = "demo-1";
                int    counter   = 0;
                bool   running   = true;
                double lastValue = 0.0;
};

PROMEKI_LOOKUP_REGISTER(DemoState)
        .scalar("name",
                [](const DemoState &s) { return std::optional<Variant>(Variant(s.name)); })
        .scalar("counter",
                [](const DemoState &s) { return std::optional<Variant>(Variant(s.counter)); })
        .scalar("running",
                [](const DemoState &s) { return std::optional<Variant>(Variant(s.running)); })
        .scalar("lastValue",
                [](const DemoState &s) { return std::optional<Variant>(Variant(s.lastValue)); });

// ============================================================
// Argument parsing — tiny, no library deps.
// ============================================================

namespace {

struct Args {
        uint16_t port = 8085;
        String   bind = "127.0.0.1";
};

Args parseArgs(int argc, char **argv) {
        Args a;
        for(int i = 1; i < argc; ++i) {
                if(std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                        a.port = static_cast<uint16_t>(std::atoi(argv[++i]));
                } else if(std::strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
                        a.bind = String(argv[++i]);
                } else {
                        std::fprintf(stderr,
                                "usage: %s [--port N] [--bind HOST]\n",
                                argv[0]);
                        std::exit(2);
                }
        }
        return a;
}

}  // namespace

int main(int argc, char **argv) {
        Application app(argc, argv);
        const Args args = parseArgs(argc, argv);

        // Single source of truth for where the API surface lives.
        // Changing this prefix moves the entire API (catalog, openapi,
        // explorer, debug surface, application endpoints) — the home
        // page picks it up via template substitution and the JS
        // resolves all fetches relative to the document <base>.
        const String apiPrefix = "/api";

        // ----------------------------------------------------------
        // Step 1 — plain HttpServer.  This is what an application
        // already has when it serves any HTTP at all; nothing about
        // the explorer machinery requires a special server.
        // ----------------------------------------------------------
        HttpServer server;

        // ----------------------------------------------------------
        // Step 2 — HttpApi attached to the server.  The prefix
        // passed at construction owns the entire api URL space:
        // every endpoint registered through this object lives under
        // it, the catalog/OpenAPI/explorer trio mounts at it, and
        // the bundled promeki diagnostics nest under
        // <prefix>/promeki/.
        //
        // Routes registered straight on the underlying server
        // (server.route()) stay outside the api and don't show up
        // in the catalog — useful for static-asset or upgrade
        // routes that don't fit the JSON-API model.
        // ----------------------------------------------------------
        HttpApi api(server, apiPrefix);
        api.setTitle("HTTP Explorer Demo");
        api.setVersion("1.0.0");
        api.setDescription(
                "Sample API surface that mounts a custom VariantDatabase, "
                "a VariantLookup over a live object, and an echo RPC "
                "endpoint — all rendered into the same /_catalog and "
                "/_openapi the explorer consumes.");

        // ----------------------------------------------------------
        // Step 3 — application endpoints.
        //
        // 3a) VariantDatabase exposure.  HttpApi::exposeDatabase
        //     installs the usual five-route CRUD shape on the
        //     server *and* publishes catalog entries for each so
        //     the explorer can render per-key forms straight from
        //     the registered VariantSpec.
        // ----------------------------------------------------------
        // Endpoint paths registered through the api are RELATIVE
        // to the api's prefix — "/demo/config" lands at
        // "<prefix>/demo/config" on the underlying server.
        if(Error err = api.exposeDatabase("/demo/config",
                                          "Demo config",
                                          DemoConfig::instance(),
                                          /*readOnly=*/false);
                        err.isError()) {
                promekiErr("exposeDatabase failed: %s", err.name().cstr());
                return 1;
        }

        // 3b) VariantLookup exposure.  Read-only path resolver
        //     over a live in-process object — mutations to the
        //     object are visible immediately on the next GET.
        static DemoState state;
        if(Error err = api.exposeLookup("/demo/state",
                                        "Demo state",
                                        state);
                        err.isError()) {
                promekiErr("exposeLookup failed: %s", err.name().cstr());
                return 1;
        }

        // 3c) Bespoke RPC endpoint.  HttpApi::rpc() takes an
        //     Endpoint descriptor (path/method/params/response)
        //     plus a callable that receives the typed/validated
        //     args and returns a Result<Variant>.  No manual JSON
        //     parsing or status-code wrangling at the call site.
        HttpApi::Endpoint echoEp;
        echoEp.path    = "/demo/echo";   // relative to api prefix
        echoEp.method  = HttpMethod::Post;
        echoEp.title   = "Echo";
        echoEp.summary = "Echoes the supplied text, optionally with a "
                         "sequence number from the live counter.  Reads "
                         "Greeting/Verbose from DemoConfig as defaults.";
        echoEp.tags    = {"demo"};
        echoEp.params  = {
                HttpApi::Param{
                        .name = "text",
                        .in = HttpApi::ParamIn::Body,
                        .required = false,
                        .spec = VariantSpec()
                                .setType(Variant::TypeString)
                                .setDescription("Text to echo back; defaults "
                                                "to DemoConfig.Greeting."),
                },
                HttpApi::Param{
                        .name = "verbose",
                        .in = HttpApi::ParamIn::Body,
                        .required = false,
                        .spec = VariantSpec()
                                .setType(Variant::TypeBool)
                                .setDescription("If true, prepend the live "
                                                "counter; defaults to "
                                                "DemoConfig.Verbose."),
                },
        };
        echoEp.response = VariantSpec()
                .setType(Variant::TypeString)
                .setDescription("Echoed message.");

        // Helper that falls back to the registered spec default
        // when no explicit value has been stored in the database.
        auto specDefault = [](const char *name) {
                const VariantSpec *s = DemoConfig::specFor(name);
                return s != nullptr ? s->defaultValue() : Variant();
        };

        if(Error err = api.rpc(echoEp,
                [specDefault](const VariantMap &args) -> Result<Variant> {
                        DemoConfig &cfg = DemoConfig::instance();
                        const String defaultText = cfg.get(
                                DemoConfig::Greeting,
                                specDefault("Greeting")).get<String>();
                        const bool defaultVerbose = cfg.get(
                                DemoConfig::Verbose,
                                specDefault("Verbose")).get<bool>();

                        const String text =
                                args.contains("text")
                                        ? args.value("text").get<String>()
                                        : defaultText;
                        const bool verbose =
                                args.contains("verbose")
                                        ? args.value("verbose").get<bool>()
                                        : defaultVerbose;

                        ++state.counter;
                        state.lastValue = static_cast<double>(state.counter);

                        String out;
                        if(verbose) {
                                out = String("[") + String::number(state.counter) +
                                      "] " + text;
                        } else {
                                out = text;
                        }
                        return makeResult<Variant>(Variant(out));
                });
                        err.isError()) {
                promekiErr("rpc registration failed: %s", err.name().cstr());
                return 1;
        }

        // ----------------------------------------------------------
        // Step 4 — application's own static UI.  The asset bundle
        // is baked into this binary as a cirf resource at
        // ":/demo/explorer/" (see resources.json + the
        // PROMEKI_REGISTER_RESOURCES call in builtinresources.cpp),
        // and HttpFileHandler understands cirf paths transparently.
        //
        // The home page contains a single template placeholder —
        // __API_BASE__ — that we substitute with the runtime
        // apiPrefix.  That placeholder feeds a <base href="..."> tag
        // in the HTML, so all relative URLs in the page (link hrefs
        // and JS fetch() calls) are anchored to the API prefix.
        // Move the API to a different mount and the page follows.
        //
        // Static UI is registered straight onto the server (not
        // through `api`) because it isn't a JSON endpoint and
        // doesn't belong in the catalog.  The router's longest-
        // literal scoring guarantees explicit /api/... and
        // /api/promeki/... routes still win over the catch-all
        // "/{path:*}" file handler below.
        // ----------------------------------------------------------
        Error resErr;
        const Buffer indexBuf = Resource::data(":/demo/explorer/index.html",
                                               &resErr);
        if(resErr.isError()) {
                promekiErr("missing baked-in index.html: %s",
                        resErr.name().cstr());
                return 1;
        }
        const String indexTemplate(static_cast<const char *>(indexBuf.data()),
                                   indexBuf.size());
        const String indexHtml = indexTemplate.replace("__API_BASE__",
                                                       apiPrefix);

        auto staticHandler = HttpHandler::Ptr::takeOwnership(
                new HttpFileHandler(Dir(":/demo/explorer")));
        const auto homePage = [indexHtml](const HttpRequest &,
                                          HttpResponse &res) {
                res.setHtml(indexHtml);
        };
        server.route("/",           HttpMethod::Get, homePage);
        server.route("/index.html", HttpMethod::Get, homePage);
        server.route("/{path:*}",   HttpMethod::Get, staticHandler);

        // ----------------------------------------------------------
        // Step 5 — opt in to the bundled promeki API surface.
        //
        // installPromekiAPI() is intentionally all-or-nothing: it
        // mounts build/env/options/memspace/log JSON endpoints plus
        // the static debug UI at "<prefix>/promeki/...".  The
        // bundled UI assumes the entire surface is present, so
        // there's no fine-grained selector.  Apps that want
        // partial diagnostics should register their own bespoke
        // endpoints instead.
        // ----------------------------------------------------------
        if(Error err = api.installPromekiAPI(); err.isError()) {
                promekiErr("installPromekiAPI failed: %s", err.name().cstr());
                return 1;
        }

        // ----------------------------------------------------------
        // Step 6 — mount the catalog/OpenAPI/explorer trio at the
        // api's prefix.  After this point the API surface is
        // browsable.  With apiPrefix == "/api" the three URLs land
        // at:
        //
        //   /api/_catalog
        //   /api/_openapi
        //   /api/_explorer/
        // ----------------------------------------------------------
        if(Error err = api.mount(); err.isError()) {
                promekiErr("mount failed: %s", err.name().cstr());
                return 1;
        }

        // ----------------------------------------------------------
        // Step 7 — listen.
        // ----------------------------------------------------------
        SocketAddress bindAddr = SocketAddress::fromString(
                args.bind + ":" + String::number(args.port)).first();
        if(Error lerr = server.listen(bindAddr); lerr.isError()) {
                promekiErr("listen failed on %s: %s",
                        bindAddr.toString().cstr(), lerr.name().cstr());
                return 1;
        }

        const String host   = server.serverAddress().toString();
        const String prefix = apiPrefix;
        std::printf("HTTP Explorer demo listening on %s\n", host.cstr());
        std::printf("\n");
        std::printf("  Home:       http://%s/\n",
                host.cstr());
        std::printf("  Explorer:   http://%s%s/\n",
                host.cstr(), prefix.cstr());
        std::printf("  OpenAPI:    http://%s%s/_openapi\n",
                host.cstr(), prefix.cstr());
        std::printf("  Catalog:    http://%s%s/_catalog\n",
                host.cstr(), prefix.cstr());
        std::printf("  Demo echo:  POST http://%s%s/demo/echo\n",
                host.cstr(), prefix.cstr());
        std::printf("  Demo cfg:   http://%s%s/demo/config\n",
                host.cstr(), prefix.cstr());
        std::printf("  Demo state: http://%s%s/demo/state/counter\n",
                host.cstr(), prefix.cstr());
        std::printf("  Debug UI:   http://%s%s/promeki/\n",
                host.cstr(), prefix.cstr());
        std::printf("\n");
        std::fflush(stdout);

        return app.exec();
}
