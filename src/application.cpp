/**
 * @file      application.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/namespace.h>
#include <promeki/application.h>
#include <promeki/uuid.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

struct Application::Data {
    UUID   appUUID;
    String appName;
};

static Application **currentApplicationPtr() {
    static Application *app = nullptr;
    return &app;
}

Application *Application::current() {
    return *currentApplicationPtr();
}

Application::Application() : _d(new Data) {
    if(current() != nullptr) {
        promekiErr("Application(%p): Another promeki application object already exists (%p)!", this, current());
    } else {
        *currentApplicationPtr() = this;
    }
}

Application::~Application() {
    if(current() != this) {
        promekiErr("Application(%p): Application object destroyed, but not current application (%p)", this, current());
    } else {
        *currentApplicationPtr() = nullptr;
    }
    delete _d;
}

const UUID &Application::appUUID() const {
    return _d->appUUID;
}

void Application::setAppUUID(const UUID &uuid) {
    _d->appUUID = uuid;
}

const String &Application::appName() const {
    return _d->appName;
}

void Application::setAppName(const String &name) {
    _d->appName = name;
}

PROMEKI_NAMESPACE_END
