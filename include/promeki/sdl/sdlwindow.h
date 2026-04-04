/**
 * @file      sdl/sdlwindow.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/widget.h>
#include <promeki/string.h>

struct SDL_Window;
struct SDL_Renderer;

PROMEKI_NAMESPACE_BEGIN

class SDLEventPump;

/**
 * @brief Top-level SDL window that serves as a root Widget.
 * @ingroup sdl_core
 *
 * SDLWindow is a Widget that wraps an SDL_Window and owns the
 * SDL_Renderer for that window.  Child widgets can be added to
 * it and positioned via the core Layout system.  The window's
 * widget geometry always reflects the SDL window's client area.
 *
 * SDLWindow handles the paint cycle: when paintAll() is called,
 * it clears the renderer, paints all child widgets (which use
 * the renderer via sdlRenderer()), and presents.
 *
 * @par Example
 * @code
 * SDLWindow window;
 * window.setTitle("My App");
 * window.resize(1280, 720);
 * window.show();
 * @endcode
 */
class SDLWindow : public Widget {
        PROMEKI_OBJECT(SDLWindow, Widget)
        friend class SDLEventPump;
        public:
                /**
                 * @brief Constructs a window with default size.
                 * @param parent Parent ObjectBase, or nullptr.
                 */
                SDLWindow(ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a window with a title and size.
                 * @param title  Window title.
                 * @param width  Window width in pixels.
                 * @param height Window height in pixels.
                 * @param parent Parent ObjectBase, or nullptr.
                 */
                SDLWindow(const String &title, int width, int height,
                          ObjectBase *parent = nullptr);

                /** @brief Destructor. Destroys the SDL window and renderer. */
                ~SDLWindow();

                SDLWindow(const SDLWindow &) = delete;
                SDLWindow &operator=(const SDLWindow &) = delete;

                /** @brief Returns the window title. */
                String title() const { return _title; }

                /**
                 * @brief Sets the window title.
                 * @param title The new title.
                 */
                void setTitle(const String &title);

                /**
                 * @brief Resizes the SDL window.
                 * @param width  New width in pixels.
                 * @param height New height in pixels.
                 *
                 * This resizes the underlying SDL window. The widget
                 * geometry is updated to match via syncSizeFromSDL().
                 */
                void resize(int width, int height);

                /** @brief Returns the window position on screen. */
                Point2Di32 position() const { return _position; }

                /**
                 * @brief Moves the window on screen.
                 * @param x New X position.
                 * @param y New Y position.
                 */
                void move(int x, int y);

                /** @brief Shows the window. Creates the SDL window if needed. */
                void show();

                /** @brief Hides the window. */
                void hide();

                /** @brief Returns true if the window is fullscreen. */
                bool isFullScreen() const { return _fullScreen; }

                /**
                 * @brief Sets fullscreen mode.
                 * @param fullScreen True for fullscreen, false for windowed.
                 */
                void setFullScreen(bool fullScreen);

                /**
                 * @brief Returns the underlying SDL_Window pointer.
                 * @return The SDL_Window, or nullptr if not created.
                 */
                SDL_Window *sdlWindow() const { return _sdlWindow; }

                /**
                 * @brief Returns the SDL window ID.
                 * @return The window ID, or 0 if not created.
                 */
                uint32_t sdlWindowID() const;

                /**
                 * @brief Returns the SDL_Renderer for this window.
                 *
                 * Child widgets use this to render. Only valid after
                 * the window has been shown.
                 *
                 * @return The SDL_Renderer, or nullptr if not created.
                 */
                SDL_Renderer *sdlRenderer() const { return _sdlRenderer; }

                /**
                 * @brief Paints the window and all child widgets.
                 *
                 * Clears the renderer, calls paintWidget() on each
                 * visible child, and presents the result.  Call this
                 * from the main thread whenever the window needs
                 * repainting.
                 */
                void paintAll();

                /** @brief Marks the window as needing a repaint. */
                void update() override;

                /** @brief Signal emitted when the window is closed. */
                PROMEKI_SIGNAL(closed)

                /** @brief Signal emitted when the window is moved. */
                PROMEKI_SIGNAL(moved, Point2Di32)

        private:
                SDL_Window     *_sdlWindow = nullptr;
                SDL_Renderer   *_sdlRenderer = nullptr;
                String          _title = "promeki";
                Point2Di32      _position{0, 0};
                bool            _fullScreen = false;

                void createWindow();
                void destroyWindow();
                void syncSizeFromSDL();
                void syncPositionFromSDL();
                void paintWidget(Widget *widget);
};

PROMEKI_NAMESPACE_END
