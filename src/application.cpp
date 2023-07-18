/**
 * @file      application.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/namespace.h>
#include <promeki/application.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

static Application **currentApplicationPtr() {
    static Application *app = nullptr;
    return &app;
}

Application *Application::current() {
    return *currentApplicationPtr();
}

Application::Application() {
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
}

PROMEKI_NAMESPACE_END

