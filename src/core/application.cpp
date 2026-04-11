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

PROMEKI_NAMESPACE_BEGIN

Application::Data &Application::data() {
        static Data d;
        return d;
}

Application::Application(int argc, char **argv) {
        data().arguments = StringList(static_cast<size_t>(argc), const_cast<const char **>(argv));
        data().mainThread = Thread::adoptCurrentThread();
        LibraryOptions::instance().loadFromEnvironment();
        if(LibraryOptions::instance().getAs<bool>(LibraryOptions::CrashHandler)) {
                CrashHandler::install();
        }
        return;
}

Application::~Application() {
        CrashHandler::uninstall();
        delete data().mainThread;
        data().mainThread = nullptr;
        data().arguments.clear();
        data().exitCode = 0;
        data().shouldQuit = false;
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
        if(CrashHandler::isInstalled()) CrashHandler::install();
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

void Application::writeTrace(const char *reason) {
        CrashHandler::writeTrace(reason);
        return;
}

void Application::quit(int exitCode) {
        data().exitCode = exitCode;
        data().shouldQuit = true;
        return;
}

bool Application::shouldQuit() {
        return data().shouldQuit;
}

int Application::exitCode() {
        return data().exitCode;
}

PROMEKI_NAMESPACE_END
