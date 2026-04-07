/**
 * @file      sdlvideowidget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/pixeldesc.h>
#include <promeki/system.h>
#include <promeki/logger.h>

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

        if(!uploadCurrentImage()) return;
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

uint32_t SDLVideoWidget::mapPixelDesc(const PixelDesc &pd) {
        // SDL3 ARRAY formats (RGB24, BGR24, the *32 aliases, RGB48,
        // BGR48, RGBA64, ARGB64, BGRA64, ABGR64, RGB48_FLOAT,
        // RGBA64_FLOAT, etc.) store components in memory in the order
        // their name implies, regardless of host endianness — so the
        // byte layout always matches the matching libpromeki "array"
        // PixelDesc for that component order.
        //
        // SDL3's ARRAYU16 / ARRAYF16 / ARRAYF32 formats, however, pack
        // their 16- or 32-bit components in *host* byte order.  We can
        // therefore direct-map libpromeki's LE variants only on a
        // little-endian host and the BE variants only on a big-endian
        // host.
        //
        // Formats omitted below (10-bit-in-16-bit-word, 12-bit, DPX,
        // v210, YUV, linear-light float) either have no direct SDL
        // equivalent or carry semantics (bit scaling, color model)
        // that SDL won't interpret correctly.  Those fall through to
        // the CSC fallback in uploadCurrentImage().
        constexpr bool LE = System::isLittleEndian();
        constexpr bool BE = System::isBigEndian();

        switch(pd.id()) {
                // 8-bit, array-ordered, sRGB
                case PixelDesc::RGB8_sRGB:      return SDL_PIXELFORMAT_RGB24;
                case PixelDesc::BGR8_sRGB:      return SDL_PIXELFORMAT_BGR24;
                case PixelDesc::RGBA8_sRGB:     return SDL_PIXELFORMAT_RGBA32;
                case PixelDesc::BGRA8_sRGB:     return SDL_PIXELFORMAT_BGRA32;
                case PixelDesc::ARGB8_sRGB:     return SDL_PIXELFORMAT_ARGB32;
                case PixelDesc::ABGR8_sRGB:     return SDL_PIXELFORMAT_ABGR32;

                // 16-bit per channel, sRGB — LE variants map directly
                // on a little-endian host.
                case PixelDesc::RGB16_LE_sRGB:   return LE ? SDL_PIXELFORMAT_RGB48 : 0;
                case PixelDesc::BGR16_LE_sRGB:   return LE ? SDL_PIXELFORMAT_BGR48 : 0;
                case PixelDesc::RGBA16_LE_sRGB:  return LE ? SDL_PIXELFORMAT_RGBA64 : 0;
                case PixelDesc::BGRA16_LE_sRGB:  return LE ? SDL_PIXELFORMAT_BGRA64 : 0;
                case PixelDesc::ARGB16_LE_sRGB:  return LE ? SDL_PIXELFORMAT_ARGB64 : 0;
                case PixelDesc::ABGR16_LE_sRGB:  return LE ? SDL_PIXELFORMAT_ABGR64 : 0;

                // ... and BE variants map directly on a big-endian host.
                case PixelDesc::RGB16_BE_sRGB:   return BE ? SDL_PIXELFORMAT_RGB48 : 0;
                case PixelDesc::BGR16_BE_sRGB:   return BE ? SDL_PIXELFORMAT_BGR48 : 0;
                case PixelDesc::RGBA16_BE_sRGB:  return BE ? SDL_PIXELFORMAT_RGBA64 : 0;
                case PixelDesc::BGRA16_BE_sRGB:  return BE ? SDL_PIXELFORMAT_BGRA64 : 0;
                case PixelDesc::ARGB16_BE_sRGB:  return BE ? SDL_PIXELFORMAT_ARGB64 : 0;
                case PixelDesc::ABGR16_BE_sRGB:  return BE ? SDL_PIXELFORMAT_ABGR64 : 0;

                default:                        return 0;
        }
}

bool SDLVideoWidget::isDirectlyMappable(const PixelDesc &pd) {
        return mapPixelDesc(pd) != 0;
}

bool SDLVideoWidget::uploadCurrentImage() {
        if(!_currentImage.isValid()) return false;

        const PixelDesc &srcPd = _currentImage.pixelDesc();

        // Compressed formats can't be uploaded — they need to be
        // decoded first by an ImageCodec or similar.  Report clearly
        // rather than silently failing inside convert().
        if(srcPd.isCompressed()) {
                promekiErr("SDLVideoWidget: compressed pixel format '%s' "
                           "cannot be displayed — decode it first",
                           srcPd.name().cstr());
                return false;
        }

        // Fast path: direct SDL format — upload as-is with no CSC.
        uint32_t sdlFmt = mapPixelDesc(srcPd);
        if(sdlFmt != 0) {
                ensureTexture(_currentImage.width(),
                              _currentImage.height(), sdlFmt);
                if(_texture == nullptr) return false;
                uploadImage(_currentImage, sdlFmt);
                return true;
        }

        // Fallback: run the image through the CSC pipeline into
        // RGBA8_sRGB, which every backend can display.  This handles
        // YUV, linear float, DPX, v210, non-host-endian and 10/12-bit
        // formats, and any user-registered PixelDesc the CSC pipeline
        // knows how to convert.
        Image converted = _currentImage.convert(
                PixelDesc(PixelDesc::RGBA8_sRGB), _currentImage.metadata());
        if(!converted.isValid()) {
                promekiErr("SDLVideoWidget: CSC from '%s' to RGBA8_sRGB failed",
                           srcPd.name().cstr());
                return false;
        }

        uint32_t rgba8Fmt = mapPixelDesc(PixelDesc(PixelDesc::RGBA8_sRGB));
        ensureTexture(converted.width(), converted.height(), rgba8Fmt);
        if(_texture == nullptr) return false;
        uploadImage(converted, rgba8Fmt);
        return true;
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
