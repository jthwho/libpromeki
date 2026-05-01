/**
 * @file      application.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/application.h>
#include <promeki/thread.h>
#include <promeki/eventloop.h>
#include <promeki/logger.h>
#include <promeki/fileiodevice.h>
#include <promeki/libraryoptions.h>
#include <promeki/crashhandler.h>
#include <promeki/signalhandler.h>
#include <promeki/env.h>
#include <promeki/socketaddress.h>
#if PROMEKI_ENABLE_HTTP
#include <promeki/debugserver.h>
#endif

PROMEKI_NAMESPACE_BEGIN

const char *Application::DebugServerEnv = "PROMEKI_DEBUG_SERVER";

Application::Data &Application::data() {
        static Data d;
        return d;
}

Application::Application(int argc, char **argv) {
        // _eventLoop is a member and has already been constructed by
        // the time this body runs, which sets EventLoop::current() on
        // the main thread.  Thread::adoptCurrentThread then lazily
        // caches that pointer the first time mainEventLoop() is
        // queried — so subsystems (SDL, TUI) constructed immediately
        // after this Application on the stack see a ready-made main
        // EventLoop via Application::mainEventLoop().
        data().arguments = StringList(static_cast<size_t>(argc), const_cast<const char **>(argv));
        data().mainThread = Thread::adoptCurrentThread();
        LibraryOptions::instance().loadFromEnvironment();
        if (LibraryOptions::instance().getAs<bool>(LibraryOptions::CrashHandler)) {
                CrashHandler::install();
        }
        if (LibraryOptions::instance().getAs<bool>(LibraryOptions::TerminationSignalHandler)) {
                SignalHandler::install();
        }
        maybeStartDebugServerFromEnv();
        return;
}

Application::~Application() {
        stopDebugServer();
        SignalHandler::uninstall();
        CrashHandler::uninstall();
        delete data().mainThread;
        data().mainThread = nullptr;
        data().arguments.clear();
        data().exitCode.setValue(0);
        data().shouldQuit.setValue(false);
        data().quitHandler = nullptr;
        return;
}

const StringList &Application::arguments() {
        return data().arguments;
}

const UUID &Application::appUUID() {
        return data().appUUID;
}

void Application::setAppUUID(const UUID &uuid) {
        data().appUUID = uuid;
        return;
}

const String &Application::appName() {
        return data().appName;
}

void Application::setAppName(const String &name) {
        data().appName = name;
        // Re-install so the crash log path picks up the new name.
        refreshCrashHandler();
        return;
}

Thread *Application::mainThread() {
        return data().mainThread;
}

EventLoop *Application::mainEventLoop() {
        Thread *t = data().mainThread;
        return t != nullptr ? t->threadEventLoop() : nullptr;
}

IODevice *Application::stdinDevice() {
        return FileIODevice::stdinDevice();
}

IODevice *Application::stdoutDevice() {
        return FileIODevice::stdoutDevice();
}

IODevice *Application::stderrDevice() {
        return FileIODevice::stderrDevice();
}

void Application::installSignalHandlers() {
        SignalHandler::install();
        return;
}

void Application::uninstallSignalHandlers() {
        SignalHandler::uninstall();
        return;
}

bool Application::areSignalHandlersInstalled() {
        return SignalHandler::isInstalled();
}

void Application::installCrashHandler() {
        CrashHandler::install();
        return;
}

void Application::uninstallCrashHandler() {
        CrashHandler::uninstall();
        return;
}

bool Application::isCrashHandlerInstalled() {
        return CrashHandler::isInstalled();
}

void Application::refreshCrashHandler() {
        if (CrashHandler::isInstalled()) CrashHandler::install();
        return;
}

void Application::writeTrace(const char *reason) {
        CrashHandler::writeTrace(reason);
        return;
}

void Application::quit(int exitCode) {
        // If an intercept handler is installed, give it first crack —
        // it may swallow the request (e.g. to trigger an async
        // shutdown that calls quit() again when it's done).
        QuitRequestHandler handler = data().quitHandler;
        if (handler) {
                if (handler(exitCode)) return;
        }
        data().exitCode.setValue(exitCode);
        data().shouldQuit.setValue(true);
        // Wake any thread blocked in EventLoop::exec() on the main
        // thread's event loop so it unwinds promptly.  Safe from any
        // thread — EventLoop::quit() is mutex-protected.
        EventLoop *mainLoop = mainEventLoop();
        if (mainLoop != nullptr) mainLoop->quit(exitCode);
        return;
}

void Application::setQuitRequestHandler(QuitRequestHandler handler) {
        data().quitHandler = std::move(handler);
        return;
}

bool Application::shouldQuit() {
        return data().shouldQuit.value();
}

int Application::exitCode() {
        return data().exitCode.value();
}

int Application::exec() {
        return _eventLoop.exec();
}

#if PROMEKI_ENABLE_HTTP

Error Application::startDebugServer(const SocketAddress &address) {
        if (data().debugServer) {
                if (data().debugServer->isListening()) return Error::AlreadyOpen;
        } else {
                data().debugServer = UniquePtr<DebugServer>::create();
                data().debugServer->installDefaultModules();
        }
        return data().debugServer->listen(address);
}

Error Application::startDebugServer(uint16_t port) {
        auto [addr, err] = DebugServer::parseSpec(String(":") + String::number(port));
        if (err.isError()) return err;
        return startDebugServer(addr);
}

void Application::stopDebugServer() {
        if (!data().debugServer) return;
        data().debugServer->close();
        data().debugServer.clear();
}

DebugServer *Application::debugServer() {
        return data().debugServer.get();
}

void Application::maybeStartDebugServerFromEnv() {
        String spec = Env::get(DebugServerEnv);
        if (spec.isEmpty()) return;
        auto [addr, perr] = DebugServer::parseSpec(spec);
        if (perr.isError()) {
                promekiWarn("%s=\"%s\" is not a valid host:port spec — debug server disabled", DebugServerEnv,
                            spec.cstr());
                return;
        }
        Error err = startDebugServer(addr);
        if (err.isError()) {
                promekiWarn("Failed to start debug server on %s: %s", addr.toString().cstr(), err.name().cstr());
                return;
        }
        promekiInfo("Debug server listening on %s", addr.toString().cstr());
}

#else // !PROMEKI_ENABLE_HTTP

Error Application::startDebugServer(const SocketAddress &) {
        return Error::NotSupported;
}

Error Application::startDebugServer(uint16_t) {
        return Error::NotSupported;
}

void Application::stopDebugServer() {}

DebugServer *Application::debugServer() {
        return nullptr;
}

void Application::maybeStartDebugServerFromEnv() {}

#endif // PROMEKI_ENABLE_HTTP

PROMEKI_NAMESPACE_END
