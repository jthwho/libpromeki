/**
 * @file      sdleventpump.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdleventpump.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

void SDLEventPump::pumpEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
                dispatchEvent(e);
        }
        return;
}

void SDLEventPump::dispatchEvent(const SDL_Event &e) {
        switch (e.type) {
                case SDL_EVENT_QUIT: {
                        Application::quit(0);
                        break;
                }

                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_MOVED:
                case SDL_EVENT_WINDOW_SHOWN:
                case SDL_EVENT_WINDOW_HIDDEN:
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST: handleWindowEvent(e); break;

                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: handleKeyEvent(e); break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                case SDL_EVENT_MOUSE_MOTION: handleMouseEvent(e); break;

                case SDL_EVENT_MOUSE_WHEEL: handleMouseWheelEvent(e); break;

                default: break;
        }
        return;
}

void SDLEventPump::registerWindow(SDLWindow *window) {
        uint32_t id = window->sdlWindowID();
        if (id != 0) {
                _windowMap[id] = window;
        }
        return;
}

void SDLEventPump::unregisterWindow(SDLWindow *window) {
        uint32_t id = window->sdlWindowID();
        if (id != 0) {
                auto it = _windowMap.find(id);
                if (it != _windowMap.end()) {
                        _windowMap.remove(it);
                }
        }
        return;
}

SDLWindow *SDLEventPump::findWindow(uint32_t id) const {
        auto it = _windowMap.find(id);
        if (it != _windowMap.end()) return it->second;
        return nullptr;
}

void SDLEventPump::handleWindowEvent(const SDL_Event &e) {
        SDLWindow *window = findWindow(e.window.windowID);
        if (window == nullptr) return;

        switch (e.type) {
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                        window->closedSignal.emit();
                        window->hide();
                        window->destroyWindow();
                        // If no windows remain, quit the application
                        if (_windowMap.isEmpty()) Application::quit(0);
                        break;

                case SDL_EVENT_WINDOW_RESIZED: window->syncSizeFromSDL(); break;

                case SDL_EVENT_WINDOW_MOVED:
                        window->syncPositionFromSDL();
                        window->movedSignal.emit(window->position());
                        break;

                case SDL_EVENT_WINDOW_SHOWN: window->setVisible(true); break;

                case SDL_EVENT_WINDOW_HIDDEN: window->setVisible(false); break;

                default: break;
        }
        return;
}

void SDLEventPump::handleKeyEvent(const SDL_Event &e) {
        SDLWindow *window = findWindow(e.key.windowID);
        if (window == nullptr) return;

        Event::Type   type = (e.type == SDL_EVENT_KEY_DOWN) ? KeyEvent::KeyPress : KeyEvent::KeyRelease;
        KeyEvent::Key key = translateKey(e.key.key);
        uint8_t       mods = translateModifiers(e.key.mod);

        // Build text string for printable keys
        String text;
        if (e.type == SDL_EVENT_KEY_DOWN && key >= 32 && key < 127) {
                char c = static_cast<char>(key);
                if (mods & KeyEvent::ShiftModifier) {
                        // Let SDL text input handle shifted characters
                } else {
                        text = String(&c, 1);
                }
        }

        // Qt-style dispatch: start at the subsystem's focused widget
        // (falling back to the window root if none), then walk up the
        // parent chain until a handler calls @c accept().  Default
        // Widget behaviour is to leave the event unaccepted, so the
        // propagation naturally reaches the window unless a specific
        // handler claims it.
        //
        // Delivery is synchronous here — the SDL pump already runs on
        // the main thread, so routing directly avoids an extra event
        // loop round-trip without changing thread semantics.
        KeyEvent      keyEv(type, key, mods, text);
        Widget       *target = nullptr;
        SdlSubsystem *sub = SdlSubsystem::instance();
        if (sub != nullptr) target = sub->focusedWidget();
        if (target == nullptr) target = window;

        while (target != nullptr) {
                target->sendEvent(&keyEv);
                if (keyEv.isAccepted()) break;
                target = dynamic_cast<Widget *>(target->parent());
        }
}

void SDLEventPump::handleMouseEvent(const SDL_Event &e) {
        SDLWindow         *window = nullptr;
        Point2Di32         pos;
        MouseEvent::Button button = MouseEvent::NoButton;
        MouseEvent::Action action = MouseEvent::Move;
        uint8_t            buttons = 0;

        if (e.type == SDL_EVENT_MOUSE_MOTION) {
                window = findWindow(e.motion.windowID);
                if (window == nullptr) return;
                pos = Point2Di32(static_cast<int>(e.motion.x), static_cast<int>(e.motion.y));
                action = MouseEvent::Move;

                // Map SDL button state
                if (e.motion.state & SDL_BUTTON_LMASK) buttons |= MouseEvent::LeftButton;
                if (e.motion.state & SDL_BUTTON_MMASK) buttons |= MouseEvent::MiddleButton;
                if (e.motion.state & SDL_BUTTON_RMASK) buttons |= MouseEvent::RightButton;
        } else {
                window = findWindow(e.button.windowID);
                if (window == nullptr) return;
                pos = Point2Di32(static_cast<int>(e.button.x), static_cast<int>(e.button.y));
                action = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? MouseEvent::Press : MouseEvent::Release;

                switch (e.button.button) {
                        case SDL_BUTTON_LEFT: button = MouseEvent::LeftButton; break;
                        case SDL_BUTTON_MIDDLE: button = MouseEvent::MiddleButton; break;
                        case SDL_BUTTON_RIGHT: button = MouseEvent::RightButton; break;
                        default: break;
                }

                if (e.button.clicks >= 2 && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                        action = MouseEvent::DoubleClick;
                }
        }

        uint8_t mods = translateModifiers(SDL_GetModState());

        MouseEvent *mouseEvent = new MouseEvent(pos, button, action, mods, buttons);
        EventLoop  *loop = window->eventLoop();
        if (loop != nullptr) {
                loop->postEvent(window, mouseEvent);
        } else {
                delete mouseEvent;
        }
        return;
}

void SDLEventPump::handleMouseWheelEvent(const SDL_Event &e) {
        SDLWindow *window = findWindow(e.wheel.windowID);
        if (window == nullptr) return;

        float mx, my;
        SDL_GetMouseState(&mx, &my);
        Point2Di32 pos(static_cast<int>(mx), static_cast<int>(my));

        MouseEvent::Action action = (e.wheel.y > 0) ? MouseEvent::ScrollUp : MouseEvent::ScrollDown;
        uint8_t            mods = translateModifiers(SDL_GetModState());

        MouseEvent *mouseEvent = new MouseEvent(pos, MouseEvent::NoButton, action, mods, 0);
        EventLoop  *loop = window->eventLoop();
        if (loop != nullptr) {
                loop->postEvent(window, mouseEvent);
        } else {
                delete mouseEvent;
        }
        return;
}

KeyEvent::Key SDLEventPump::translateKey(int sdlKeycode) {
        // SDL3 keycodes for printable ASCII are the ASCII values themselves
        if (sdlKeycode >= 32 && sdlKeycode < 127) {
                return static_cast<KeyEvent::Key>(sdlKeycode);
        }

        switch (sdlKeycode) {
                case SDLK_ESCAPE: return KeyEvent::Key_Escape;
                case SDLK_RETURN: return KeyEvent::Key_Enter;
                case SDLK_TAB: return KeyEvent::Key_Tab;
                case SDLK_BACKSPACE: return KeyEvent::Key_Backspace;
                case SDLK_INSERT: return KeyEvent::Key_Insert;
                case SDLK_DELETE: return KeyEvent::Key_Delete;
                case SDLK_HOME: return KeyEvent::Key_Home;
                case SDLK_END: return KeyEvent::Key_End;
                case SDLK_PAGEUP: return KeyEvent::Key_PageUp;
                case SDLK_PAGEDOWN: return KeyEvent::Key_PageDown;
                case SDLK_UP: return KeyEvent::Key_Up;
                case SDLK_DOWN: return KeyEvent::Key_Down;
                case SDLK_LEFT: return KeyEvent::Key_Left;
                case SDLK_RIGHT: return KeyEvent::Key_Right;
                case SDLK_F1: return KeyEvent::Key_F1;
                case SDLK_F2: return KeyEvent::Key_F2;
                case SDLK_F3: return KeyEvent::Key_F3;
                case SDLK_F4: return KeyEvent::Key_F4;
                case SDLK_F5: return KeyEvent::Key_F5;
                case SDLK_F6: return KeyEvent::Key_F6;
                case SDLK_F7: return KeyEvent::Key_F7;
                case SDLK_F8: return KeyEvent::Key_F8;
                case SDLK_F9: return KeyEvent::Key_F9;
                case SDLK_F10: return KeyEvent::Key_F10;
                case SDLK_F11: return KeyEvent::Key_F11;
                case SDLK_F12: return KeyEvent::Key_F12;
                default: return KeyEvent::Key_Unknown;
        }
}

uint8_t SDLEventPump::translateModifiers(uint16_t sdlMod) {
        uint8_t mods = KeyEvent::NoModifier;
        if (sdlMod & SDL_KMOD_SHIFT) mods |= KeyEvent::ShiftModifier;
        if (sdlMod & SDL_KMOD_CTRL) mods |= KeyEvent::CtrlModifier;
        if (sdlMod & SDL_KMOD_ALT) mods |= KeyEvent::AltModifier;
        if (sdlMod & SDL_KMOD_GUI) mods |= KeyEvent::MetaModifier;
        return mods;
}

PROMEKI_NAMESPACE_END
