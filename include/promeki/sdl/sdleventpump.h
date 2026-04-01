/**
 * @file      sdl/sdleventpump.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/map.h>
#include <promeki/core/keyevent.h>

union SDL_Event;

PROMEKI_NAMESPACE_BEGIN

class SDLWindow;

/**
 * @brief Pumps SDL events into the promeki event system.
 * @ingroup sdl_core
 *
 * Translates SDL events into promeki KeyEvent, MouseEvent, and
 * window lifecycle calls on the appropriate SDLWindow.  Windows
 * register themselves with the pump so that events can be routed
 * by SDL window ID.
 *
 * Supports two modes:
 * - **Non-blocking** (default): drains all pending events via
 *   SDL_PollEvent and returns immediately.
 * - **Blocking**: waits for the first event via SDL_WaitEvent,
 *   processes it, then drains any remaining pending events.
 *   This avoids busy-waiting in the main loop.
 *
 * This is not an ObjectBase — it is infrastructure owned by
 * SDLApplication (similar to TuiScreen or TuiInputParser).
 */
class SDLEventPump {
        public:
                SDLEventPump() = default;
                ~SDLEventPump() = default;

                SDLEventPump(const SDLEventPump &) = delete;
                SDLEventPump &operator=(const SDLEventPump &) = delete;

                /**
                 * @brief Pumps SDL events and dispatches them.
                 *
                 * @param blocking If true, blocks until at least one event
                 *        is available (via SDL_WaitEvent), then drains all
                 *        remaining events.  If false (default), polls all
                 *        pending events and returns immediately.
                 */
                void pumpEvents(bool blocking = false);

                /**
                 * @brief Registers a window for event routing.
                 * @param window The SDLWindow to register.
                 */
                void registerWindow(SDLWindow *window);

                /**
                 * @brief Unregisters a window from event routing.
                 * @param window The SDLWindow to unregister.
                 */
                void unregisterWindow(SDLWindow *window);

                /**
                 * @brief Finds a registered window by SDL window ID.
                 * @param id The SDL window ID.
                 * @return The SDLWindow, or nullptr if not found.
                 */
                SDLWindow *findWindow(uint32_t id) const;

        private:
                Map<uint32_t, SDLWindow *> _windowMap;

                void dispatchEvent(const SDL_Event &e);
                void handleWindowEvent(const SDL_Event &e);
                void handleKeyEvent(const SDL_Event &e);
                void handleMouseEvent(const SDL_Event &e);
                void handleMouseWheelEvent(const SDL_Event &e);

                static KeyEvent::Key translateKey(int sdlKeycode);
                static uint8_t translateModifiers(uint16_t sdlMod);
};

PROMEKI_NAMESPACE_END
