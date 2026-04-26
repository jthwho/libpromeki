/**
 * @file      debugserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/debugserver.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpstatus.h>

PROMEKI_NAMESPACE_BEGIN

const String DebugServer::DefaultApiPrefix = "/api";
const String DebugServer::DefaultBindHost  = "127.0.0.1";

DebugServer::DebugServer(ObjectBase *parent) :
        ObjectBase(parent),
        _api(_server, DefaultApiPrefix, this) {
        _api.setTitle("Promeki Debug API");
        _api.setDescription(
                "Built-in diagnostic surface: build info, environment, "
                "library options, memory stats, logger control, and "
                "anything else mounted through this server's HttpApi.");
}

DebugServer::~DebugServer() {
        close();
}

Error DebugServer::listen(const SocketAddress &address) {
        return _server.listen(address);
}

Error DebugServer::listen(uint16_t port) {
        // Build the loopback address explicitly so the bind site
        // matches the documented default.  parseSpec is the canonical
        // place that knows how to fold ":port" into a real address.
        auto [addr, err] = parseSpec(String(":") + String::number(port));
        if(err.isError()) return err;
        return _server.listen(addr);
}

void DebugServer::close() {
        _server.close();
}

bool DebugServer::isListening() const {
        return _server.isListening();
}

SocketAddress DebugServer::serverAddress() const {
        return _server.serverAddress();
}

void DebugServer::installDefaultModules() {
        _api.installPromekiAPI();
        _api.mount();

        // Root redirect is exclusive to DebugServer — applications
        // that wire the promeki API into their own server keep
        // control of "/" themselves.  The redirect target is the
        // debug UI's index page, which lives at <prefix>/promeki/.
        const String uiTarget = _api.resolve("/promeki/");
        _server.route("/", HttpMethod::Get,
                [uiTarget](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        res.setHeader("Location", uiTarget);
                        res.setText("");
                });
}

Result<SocketAddress> DebugServer::parseSpec(const String &spec) {
        if(spec.isEmpty()) return makeError<SocketAddress>(Error::Invalid);
        // Bare ":port" form — fold in the loopback default so tools
        // can write `PROMEKI_DEBUG_SERVER=:1234` without spelling out
        // the host.  A user that wants any-iface exposure must say so
        // explicitly with `0.0.0.0:1234` or `[::]:1234`.
        if(spec.startsWith(":")) {
                return SocketAddress::fromString(DefaultBindHost + spec);
        }
        return SocketAddress::fromString(spec);
}

PROMEKI_NAMESPACE_END
