/**
 * @file      debugserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/debugserver.h>
#include <promeki/debugmodules.h>

PROMEKI_NAMESPACE_BEGIN

const String DebugServer::DefaultApiPrefix = "/promeki/debug/api";
const String DebugServer::DefaultUiPrefix  = "/promeki/debug";
const String DebugServer::DefaultBindHost  = "127.0.0.1";

DebugServer::DebugServer(ObjectBase *parent) : ObjectBase(parent) {
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

void DebugServer::installDefaultModules(const String &apiPrefix, const String &uiPrefix) {
        installBuildInfoDebugRoutes(_server, apiPrefix);
        installEnvDebugRoutes(_server, apiPrefix);
        installLibraryOptionsDebugRoutes(_server, apiPrefix);
        installMemoryDebugRoutes(_server, apiPrefix);
        installLoggerDebugRoutes(_server, apiPrefix);
        installDebugFrontendRoutes(_server, uiPrefix);
        installDebugRootRedirect(_server, uiPrefix);
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
