/**
 * @file      sdlvideowidget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/logger.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

SDLVideoWidget::SDLVideoWidget(ObjectBase *parent) : Widget(parent) {
        setSizePolicy(SizeExpanding);
        return;
}

SDLVideoWidget::~SDLVideoWidget() {
        if(_texture != nullptr) {
                SDL_DestroyTexture(_texture);
                _texture = nullptr;
        }
        return;
}

void SDLVideoWidget::setImage(const Image &image) {
        _currentImage = image;
        update();
        return;
}

void SDLVideoWidget::paintEvent(PaintEvent *) {
        if(!_currentImage.isValid()) return;

        SDL_Renderer *renderer = findRenderer();
        if(renderer == nullptr) return;

        // Upload the image to a texture
        int promekiFmt = _currentImage.pixelFormatID();
        uint32_t sdlFmt = mapPixelFormat(promekiFmt);

        if(sdlFmt != 0) {
                ensureTexture(_currentImage.width(), _currentImage.height(), sdlFmt);
                uploadImage(_currentImage, sdlFmt);
        } else {
                Image converted = _currentImage.convert(PixelFormat::RGBA8, _currentImage.metadata());
                if(!converted.isValid()) {
                        promekiErr("SDLVideoWidget: format conversion to RGBA8 failed");
                        return;
                }
                uint32_t rgba8Fmt = mapPixelFormat(PixelFormat::RGBA8);
                ensureTexture(converted.width(), converted.height(), rgba8Fmt);
                uploadImage(converted, rgba8Fmt);
        }

        if(_texture == nullptr) return;

        // Calculate destination rect within our widget geometry
        // using the parent's coordinate system
        Point2Di32 origin = mapToGlobal(Point2Di32(0, 0));
        float ox = static_cast<float>(origin.x());
        float oy = static_cast<float>(origin.y());
        float ww = static_cast<float>(width());
        float wh = static_cast<float>(height());

        if(_scaleMode == ScaleNone) {
                float tw = static_cast<float>(_textureSize.width());
                float th = static_cast<float>(_textureSize.height());
                float dx = ox + (ww - tw) / 2.0f;
                float dy = oy + (wh - th) / 2.0f;
                SDL_FRect dst = { dx, dy, tw, th };
                SDL_RenderTexture(renderer, _texture, nullptr, &dst);
        } else if(_scaleMode == ScaleFit) {
                float imgAspect = static_cast<float>(_textureSize.width()) /
                                  static_cast<float>(_textureSize.height());
                float widgetAspect = ww / wh;
                float dstW, dstH;
                if(imgAspect > widgetAspect) {
                        dstW = ww;
                        dstH = dstW / imgAspect;
                } else {
                        dstH = wh;
                        dstW = dstH * imgAspect;
                }
                float dx = ox + (ww - dstW) / 2.0f;
                float dy = oy + (wh - dstH) / 2.0f;
                SDL_FRect dst = { dx, dy, dstW, dstH };
                SDL_RenderTexture(renderer, _texture, nullptr, &dst);
        } else {
                SDL_FRect dst = { ox, oy, ww, wh };
                SDL_RenderTexture(renderer, _texture, nullptr, &dst);
        }
        return;
}

uint32_t SDLVideoWidget::mapPixelFormat(int promekiFormat) {
        switch(promekiFormat) {
                case PixelFormat::RGBA8:     return SDL_PIXELFORMAT_RGBA8888;
                case PixelFormat::RGB8:      return SDL_PIXELFORMAT_RGB24;
                default:                     return 0;
        }
}

bool SDLVideoWidget::isDirectlyMappable(int promekiFormat) {
        return mapPixelFormat(promekiFormat) != 0;
}

void SDLVideoWidget::ensureTexture(int w, int h, uint32_t sdlPixFmt) {
        if(_texture != nullptr &&
           _textureSize.width() == w &&
           _textureSize.height() == h &&
           _texturePixFmt == sdlPixFmt) {
                return;
        }

        SDL_Renderer *renderer = findRenderer();
        if(renderer == nullptr) return;

        if(_texture != nullptr) {
                SDL_DestroyTexture(_texture);
                _texture = nullptr;
        }

        _texture = SDL_CreateTexture(
                renderer,
                static_cast<SDL_PixelFormat>(sdlPixFmt),
                SDL_TEXTUREACCESS_STREAMING,
                w, h
        );

        if(_texture == nullptr) {
                promekiErr("SDLVideoWidget: SDL_CreateTexture failed: %s", SDL_GetError());
                _textureSize = Size2Di32(0, 0);
                _texturePixFmt = 0;
                return;
        }

        _textureSize = Size2Di32(w, h);
        _texturePixFmt = sdlPixFmt;
        return;
}

void SDLVideoWidget::uploadImage(const Image &image, uint32_t sdlPixFmt) {
        (void)sdlPixFmt;
        if(_texture == nullptr) return;

        size_t stride = image.lineStride(0);
        const void *pixels = image.data(0);

        if(!SDL_UpdateTexture(_texture, nullptr, pixels, static_cast<int>(stride))) {
                promekiErr("SDLVideoWidget: SDL_UpdateTexture failed: %s", SDL_GetError());
        }
        return;
}

SDL_Renderer *SDLVideoWidget::findRenderer() const {
        // Walk up the parent chain to find the SDLWindow
        ObjectBase *p = parent();
        while(p != nullptr) {
                SDLWindow *win = dynamic_cast<SDLWindow *>(p);
                if(win != nullptr) return win->sdlRenderer();
                p = p->parent();
        }
        return nullptr;
}

PROMEKI_NAMESPACE_END
