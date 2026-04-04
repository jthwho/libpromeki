/**
 * @file      sdlwindow.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlwindow.h>
#include <promeki/sdl/sdlapplication.h>
#include <promeki/logger.h>
#include <promeki/layout.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

SDLWindow::SDLWindow(ObjectBase *parent) : Widget(parent) {
        setGeometry(Rect2Di32(0, 0, 1280, 720));
        Widget::setVisible(false);
        return;
}

SDLWindow::SDLWindow(const String &title, int width, int height, ObjectBase *parent)
        : Widget(parent), _title(title) {
        setGeometry(Rect2Di32(0, 0, width, height));
        Widget::setVisible(false);
        return;
}

SDLWindow::~SDLWindow() {
        destroyWindow();
        return;
}

void SDLWindow::setTitle(const String &title) {
        _title = title;
        if(_sdlWindow != nullptr) {
                SDL_SetWindowTitle(_sdlWindow, _title.cstr());
        }
        return;
}

void SDLWindow::resize(int w, int h) {
        if(_sdlWindow != nullptr) {
                SDL_SetWindowSize(_sdlWindow, w, h);
                syncSizeFromSDL();
        } else {
                setGeometry(Rect2Di32(0, 0, w, h));
        }
        return;
}

void SDLWindow::move(int x, int y) {
        _position = Point2Di32(x, y);
        if(_sdlWindow != nullptr) {
                SDL_SetWindowPosition(_sdlWindow, x, y);
        }
        return;
}

void SDLWindow::show() {
        if(_sdlWindow == nullptr) createWindow();
        if(_sdlWindow != nullptr) {
                SDL_ShowWindow(_sdlWindow);
                Widget::setVisible(true);
        }
        return;
}

void SDLWindow::hide() {
        if(_sdlWindow != nullptr) {
                SDL_HideWindow(_sdlWindow);
                Widget::setVisible(false);
        }
        return;
}

void SDLWindow::setFullScreen(bool fullScreen) {
        _fullScreen = fullScreen;
        if(_sdlWindow != nullptr) {
                SDL_SetWindowFullscreen(_sdlWindow, fullScreen);
                syncSizeFromSDL();
        }
        return;
}

uint32_t SDLWindow::sdlWindowID() const {
        if(_sdlWindow == nullptr) return 0;
        return SDL_GetWindowID(_sdlWindow);
}

void SDLWindow::update() {
        Widget::update();
        // Push an SDL user event to wake SDL_WaitEvent so the
        // main loop calls paintAll() promptly.
        SDL_Event event = {};
        event.type = SDL_EVENT_USER;
        SDL_PushEvent(&event);
        return;
}

void SDLWindow::paintAll() {
        if(_sdlRenderer == nullptr) return;

        SDL_SetRenderDrawColor(_sdlRenderer, 0, 0, 0, 255);
        SDL_RenderClear(_sdlRenderer);

        // Paint all visible children
        for(auto *child : childList()) {
                Widget *w = dynamic_cast<Widget *>(child);
                if(w && w->isVisible()) paintWidget(w);
        }

        SDL_RenderPresent(_sdlRenderer);
        clearDirty();
        return;
}

void SDLWindow::paintWidget(Widget *widget) {
        PaintEvent e;
        widget->sendEvent(&e);
        widget->clearDirty();

        // Recurse into children
        for(auto *child : widget->childList()) {
                Widget *w = dynamic_cast<Widget *>(child);
                if(w && w->isVisible()) paintWidget(w);
        }
        return;
}

void SDLWindow::createWindow() {
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
        if(_fullScreen) flags |= SDL_WINDOW_FULLSCREEN;

        _sdlWindow = SDL_CreateWindow(
                _title.cstr(),
                width(), height(),
                flags
        );

        if(_sdlWindow == nullptr) {
                promekiErr("SDLWindow: SDL_CreateWindow failed: %s", SDL_GetError());
                return;
        }

        _sdlRenderer = SDL_CreateRenderer(_sdlWindow, nullptr);
        if(_sdlRenderer == nullptr) {
                promekiErr("SDLWindow: SDL_CreateRenderer failed: %s", SDL_GetError());
                SDL_DestroyWindow(_sdlWindow);
                _sdlWindow = nullptr;
                return;
        }

        // Register with the event pump
        SDLApplication *app = SDLApplication::instance();
        if(app != nullptr) {
                app->eventPump().registerWindow(this);
        }

        syncPositionFromSDL();
        return;
}

void SDLWindow::destroyWindow() {
        if(_sdlWindow == nullptr) return;

        // Unregister from the event pump
        SDLApplication *app = SDLApplication::instance();
        if(app != nullptr) {
                app->eventPump().unregisterWindow(this);
        }

        if(_sdlRenderer != nullptr) {
                SDL_DestroyRenderer(_sdlRenderer);
                _sdlRenderer = nullptr;
        }

        SDL_DestroyWindow(_sdlWindow);
        _sdlWindow = nullptr;
        Widget::setVisible(false);
        return;
}

void SDLWindow::syncSizeFromSDL() {
        if(_sdlWindow == nullptr) return;
        int w, h;
        SDL_GetWindowSize(_sdlWindow, &w, &h);
        setGeometry(Rect2Di32(0, 0, w, h));
        return;
}

void SDLWindow::syncPositionFromSDL() {
        if(_sdlWindow == nullptr) return;
        int x, y;
        SDL_GetWindowPosition(_sdlWindow, &x, &y);
        _position = Point2Di32(x, y);
        return;
}

PROMEKI_NAMESPACE_END
