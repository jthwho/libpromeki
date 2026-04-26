/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Entry point for the @c promeki-pipeline demo.  Phase C only stands
 * up the HTTP server, the pipeline manager, and the placeholder
 * frontend served from the embedded cirf bundle.  Phase D fills in
 * the REST + WebSocket API; Phase E replaces the placeholder
 * @c index.html with the real Vue UI.
 */

#include <cstdio>

#include <promeki/application.h>
#include <promeki/buildinfo.h>
#include <promeki/cmdlineparser.h>
#include <promeki/error.h>
#include <promeki/httpfilehandler.h>
#include <promeki/httphandler.h>
#include <promeki/httpmethod.h>
#include <promeki/httpserver.h>
#include <promeki/logger.h>
#include <promeki/objectbase.tpp>
#include <promeki/resource.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/thread.h>

#include "promeki_pipeline_resources.h"

#include "apiroutes.h"
#include "eventbroadcaster.h"
#include "pipelinemanager.h"

PROMEKI_REGISTER_RESOURCES(promeki_pipeline_resources, "promeki-pipeline")

using namespace promeki;
using namespace promekipipeline;

int main(int argc, char **argv) {
        Application app(argc, argv);
        Application::setAppName("promeki-pipeline");

        uint16_t port = 8080;
        String   bindHost = "0.0.0.0";
        bool     wantHelp = false;

        CmdLineParser parser;
        parser.registerOptions({
                {'p', "port", "Port to listen on (default 8080)", CmdLineParser::OptionIntCallback([&port](int v) {
                         if (v < 0 || v > 65535) {
                                 std::fprintf(stderr, "Invalid port: %d\n", v);
                                 return 1;
                         }
                         port = static_cast<uint16_t>(v);
                         return 0;
                 })},
                {'b', "bind", "Bind address (default 0.0.0.0)",
                 CmdLineParser::OptionStringCallback([&bindHost](const String &s) {
                         bindHost = s;
                         return 0;
                 })},
                {'h', "help", "Print usage and exit", CmdLineParser::OptionCallback([&wantHelp]() {
                         wantHelp = true;
                         return 0;
                 })},
        });

        if (parser.parseMain(argc, argv) != 0) return 1;
        if (wantHelp) {
                const auto *info = getBuildInfo();
                std::printf("%s %s — pipeline editor demo backend\n\n", info->name, info->version);
                std::printf("Usage: promeki-pipeline [options]\n\nOptions:\n");
                for (const String &line : parser.generateUsage()) {
                        std::printf("  %s\n", line.cstr());
                }
                return 0;
        }

        // HttpServer / PipelineManager capture the calling thread's
        // EventLoop in their ctors; passing the main Thread as parent
        // makes their lifetime track the application explicitly.  The
        // mainEventLoop() call also primes Thread::_threadLoop on the
        // main thread so a cross-thread Application::quit (e.g. from
        // the signal-handler watcher thread) can post the Quit item to
        // the right loop without racing the main thread caching it.
        ObjectBase *mainParent = Application::mainThread();
        (void)Application::mainEventLoop();
        HttpServer       server(mainParent);
        PipelineManager  manager(mainParent);
        ApiRoutes        routes(server, manager);
        EventBroadcaster broadcaster(server, manager, mainParent);
        (void)routes;
        (void)broadcaster;

        // Static files: anything not consumed by /api/* falls through
        // to the embedded cirf bundle mounted at :/promeki-pipeline/.
        server.route("/{path:*}", HttpMethod::Get,
                     HttpHandler::Ptr::takeOwnership(new HttpFileHandler(String(":/promeki-pipeline"))));

        SocketAddress address;
        if (bindHost == "0.0.0.0") {
                address = SocketAddress::any(port);
        } else if (bindHost == "127.0.0.1" || bindHost == "localhost") {
                address = SocketAddress::localhost(port);
        } else {
                Result<SocketAddress> r = SocketAddress::fromString(bindHost + ":" + String::number(port));
                if (r.second().isError()) {
                        promekiErr("Invalid bind address \"%s\": %s", bindHost.cstr(), r.second().name().cstr());
                        return 1;
                }
                address = r.first();
        }

        Error err = server.listen(address);
        if (err.isError()) {
                promekiErr("Failed to listen on %s: %s", address.toString().cstr(), err.name().cstr());
                return 1;
        }
        promekiInfo("ProMeKi Pipeline listening on http://%s:%u/", bindHost.cstr(), static_cast<unsigned>(port));

        return app.exec();
}
