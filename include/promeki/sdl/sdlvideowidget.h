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
 * within its widget geometry.  It handles aspect-ratio-preserving
 * scaling with letterbox/pillarbox bars.
 *
 * The widget obtains the SDL_Renderer from its ancestor SDLWindow.
 * It must be a child (direct or indirect) of an SDLWindow to render.
 *
 * @par Supported pixel formats
 *
 * Every uncompressed @c PixelDesc the library supports can be
 * displayed.  Formats with a direct SDL equivalent (8-bit RGB/BGR/
 * RGBA/BGRA/ARGB/ABGR and the host-endian 16-bit variants of those)
 * are uploaded to an SDL texture as-is.  Everything else — DPX
 * packings, v210, 10/12-bit words, YUV, float, linear, non-host
 * endian — is routed through @c Image::convert() and displayed as
 * RGBA8_sRGB via the CSC pipeline.  @c isDirectlyMappable() reports
 * only the fast path: non-directly-mappable formats still render
 * correctly, just via an extra conversion step.
 *
 * Compressed pixel descriptions (e.g. @c JPEG_RGB8_sRGB) are not
 * supported — the caller must decode them first.
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
                 * @brief Maps a promeki PixelDesc to an SDL pixel format.
                 *
                 * Returns a direct SDL format for uncompressed 8-bit
                 * RGB/BGR/RGBA/BGRA/ARGB/ABGR formats and the
                 * host-endian 16-bit variants of those.  Returns 0 for
                 * formats without a direct SDL equivalent (DPX, v210,
                 * YUV, float, linear-light, non-host-endian 16-bit,
                 * 10/12-bit-in-16-bit-word formats, compressed, etc).
                 * Formats that return 0 are still displayable — the
                 * widget falls back to CSC conversion to RGBA8_sRGB.
                 *
                 * @param pd The promeki pixel description.
                 * @return The SDL pixel format enum value, or 0 if no direct mapping.
                 */
                static uint32_t mapPixelDesc(const PixelDesc &pd);

                /**
                 * @brief Returns whether a format can skip CSC conversion on upload.
                 *
                 * This is the fast-path predicate — it does NOT tell you
                 * whether the widget can display the format (it can
                 * display every uncompressed format via the fallback
                 * path).
                 *
                 * @param pd The promeki pixel description.
                 * @return true if the format maps directly to an SDL format.
                 */
                static bool isDirectlyMappable(const PixelDesc &pd);

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
                bool uploadCurrentImage();
                SDL_Renderer *findRenderer() const;
};

PROMEKI_NAMESPACE_END
