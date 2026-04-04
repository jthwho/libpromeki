/**
 * @file      sdl/sdlvideowidget.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/widget.h>
#include <promeki/image.h>

struct SDL_Texture;
struct SDL_Renderer;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Widget that displays a promeki Image via SDL texture rendering.
 * @ingroup sdl_core
 *
 * SDLVideoWidget manages an SDL_Texture and renders a promeki Image
 * within its widget geometry.  It handles pixel format conversion
 * (unsupported formats are converted to RGBA8) and aspect-ratio-
 * preserving scaling with letterbox/pillarbox bars.
 *
 * The widget obtains the SDL_Renderer from its ancestor SDLWindow.
 * It must be a child (direct or indirect) of an SDLWindow to render.
 *
 * @par Example
 * @code
 * SDLWindow window("Player", 1280, 720);
 * SDLVideoWidget *video = new SDLVideoWidget(&window);
 * video->setGeometry(window.geometry());
 * video->setImage(myImage);
 * window.show();
 * @endcode
 */
class SDLVideoWidget : public Widget {
        PROMEKI_OBJECT(SDLVideoWidget, Widget)
        public:
                /** @brief Scaling mode for displayed images. */
                enum ScaleMode {
                        ScaleNone,      ///< No scaling, centered at native size.
                        ScaleFit,       ///< Scale to fit, preserving aspect ratio.
                        ScaleStretch    ///< Stretch to fill, ignoring aspect ratio.
                };

                /**
                 * @brief Constructs an SDLVideoWidget.
                 * @param parent Parent widget (typically an SDLWindow).
                 */
                SDLVideoWidget(ObjectBase *parent = nullptr);

                /** @brief Destructor. Frees the SDL texture. */
                ~SDLVideoWidget();

                SDLVideoWidget(const SDLVideoWidget &) = delete;
                SDLVideoWidget &operator=(const SDLVideoWidget &) = delete;

                /**
                 * @brief Sets the image to display.
                 *
                 * The image is uploaded to an SDL texture on the next
                 * paint.  If the pixel format is not directly supported
                 * by SDL, it is converted to RGBA8.
                 *
                 * @param image The image to display.
                 */
                void setImage(const Image &image);

                /**
                 * @brief Sets the scaling mode.
                 * @param mode The scaling mode.
                 */
                void setScaleMode(ScaleMode mode) { _scaleMode = mode; }

                /** @brief Returns the current scaling mode. */
                ScaleMode scaleMode() const { return _scaleMode; }

                /**
                 * @brief Maps a promeki PixelDesc ID to an SDL pixel format.
                 * @param pd The promeki PixelDesc::ID.
                 * @return The SDL pixel format enum value, or 0 if no direct mapping.
                 */
                static uint32_t mapPixelDesc(PixelDesc::ID pd);

                /**
                 * @brief Returns whether a promeki pixel description can be directly uploaded.
                 * @param pd The promeki PixelDesc::ID.
                 * @return true if the format maps directly to an SDL format.
                 */
                static bool isDirectlyMappable(PixelDesc::ID pd);

        protected:
                void paintEvent(PaintEvent *e) override;

        private:
                SDL_Texture    *_texture = nullptr;
                Size2Di32       _textureSize{0, 0};
                uint32_t        _texturePixFmt = 0;
                ScaleMode       _scaleMode = ScaleFit;
                Image           _currentImage;

                void ensureTexture(int w, int h, uint32_t sdlPixFmt);
                void uploadImage(const Image &image, uint32_t sdlPixFmt);
                SDL_Renderer *findRenderer() const;
};

PROMEKI_NAMESPACE_END
