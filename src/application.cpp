/**
 * @file      application.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/application.h>
#include <promeki/core/thread.h>
#include <promeki/core/eventloop.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

Application::Data &Application::data() {
        static Data d;
        return d;
}

Application::Application(int argc, char **argv) {
        data().arguments = StringList(static_cast<size_t>(argc), const_cast<const char **>(argv));
        data().mainThread = Thread::adoptCurrentThread();
        return;
}

Application::~Application() {
        delete data().mainThread;
        data().mainThread = nullptr;
        data().arguments.clear();
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
        return;
}

Thread *Application::mainThread() {
        return data().mainThread;
}

EventLoop *Application::mainEventLoop() {
        Thread *t = data().mainThread;
        return t != nullptr ? t->threadEventLoop() : nullptr;
}

PROMEKI_NAMESPACE_END
