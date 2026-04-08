/**
 * @file      videotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstring>
#include <promeki/videotestpattern.h>
#include <promeki/image.h>
#include <promeki/paintengine.h>
#include <promeki/pixeldesc.h>
#include <promeki/random.h>
#include <promeki/fastfont.h>
#include <promeki/timecode.h>
#include <promeki/rect.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

VideoTestPattern::VideoTestPattern() = default;

VideoTestPattern::~VideoTestPattern() {
        delete _burnFont;
}

void VideoTestPattern::setPattern(Pattern pattern) {
        if(_pattern == pattern) return;
        _pattern = pattern;
        invalidateImageCache();
}

void VideoTestPattern::setSolidColor(const Color &color) {
        if(_solidColor == color) return;
        _solidColor = color;
        invalidateImageCache();
}

void VideoTestPattern::setBurnFontFilename(const String &path) {
        if(_burnFontFilename == path) return;
        _burnFontFilename = path;
        _burnFontConfigDirty = true;
}

void VideoTestPattern::setBurnFontSize(int px) {
        if(_burnFontSize == px) return;
        _burnFontSize = px;
        _burnFontConfigDirty = true;
}

void VideoTestPattern::setBurnTextColor(const Color &c) {
        if(_burnTextColor == c) return;
        _burnTextColor = c;
        _burnFontConfigDirty = true;
}

void VideoTestPattern::setBurnBackgroundColor(const Color &c) {
        if(_burnBackgroundColor == c) return;
        _burnBackgroundColor = c;
        _burnFontConfigDirty = true;
}

Result<VideoTestPattern::BurnPosition>
VideoTestPattern::burnPositionFromString(const String &name) {
        if(name == "topleft")      return makeResult(BurnTopLeft);
        if(name == "topcenter")    return makeResult(BurnTopCenter);
        if(name == "topright")     return makeResult(BurnTopRight);
        if(name == "bottomleft")   return makeResult(BurnBottomLeft);
        if(name == "bottomcenter") return makeResult(BurnBottomCenter);
        if(name == "bottomright")  return makeResult(BurnBottomRight);
        if(name == "center")       return makeResult(BurnCenter);
        return makeError<BurnPosition>(Error::Invalid);
}

String VideoTestPattern::burnPositionToString(BurnPosition pos) {
        switch(pos) {
                case BurnTopLeft:      return "topleft";
                case BurnTopCenter:    return "topcenter";
                case BurnTopRight:     return "topright";
                case BurnBottomLeft:   return "bottomleft";
                case BurnBottomCenter: return "bottomcenter";
                case BurnBottomRight:  return "bottomright";
                case BurnCenter:       return "center";
        }
        return "bottomcenter";
}

bool VideoTestPattern::isStaticPattern() const {
        return _pattern != Noise;
}

void VideoTestPattern::invalidateImageCache() const {
        for(int i = 0; i < CacheSlotCount; i++) {
                _cachedImages[i] = Image();
        }
        _cacheW = 0;
        _cacheH = 0;
        _cachePixelDescId = 0;
}

Image VideoTestPattern::create(const ImageDesc &desc, double motionOffset) const {
        return create(desc, motionOffset, Timecode());
}

ImageDesc VideoTestPattern::rgbScratchDesc(const ImageDesc &target) const {
        // Use RGBA8_sRGB as the scratch format rather than RGB16 so
        // the fallback scratch -> target CSC lands on an already-
        // registered Highway fast path (e.g. RGBA8_sRGB ->
        // YUV8_422_Rec709) rather than the generic unpack/matrix/pack
        // pipeline.  All the current VideoTestPattern patterns and
        // the FastFont burn-in cache look identical at 8 vs 16 bits
        // (the patterns emit 16-bit component values that the
        // interleaved paint engine already truncates on write), and
        // benchmarks show this swap dropping the YUV burn-in/motion
        // cost by roughly the same factor the fast path is faster
        // than the scalar pipeline.
        ImageDesc rd(target.width(), target.height(),
                     PixelDesc(PixelDesc::RGBA8_sRGB));
        rd.metadata() = target.metadata();
        return rd;
}

Image VideoTestPattern::create(const ImageDesc &desc, double motionOffset,
                               const Timecode &currentTimecode) const {
        const bool directPaint = desc.pixelDesc().hasPaintEngine();
        const bool wantTc = _burnEnabled && currentTimecode.isValid();
        const bool wantText = _burnEnabled && !_burnText.isEmpty();
        const bool wantBurn = wantTc || wantText;

        // Decide which format the cache should hold.
        //
        // 1. Paintable target (RGB, BGR, ...): cache holds the target
        //    format directly.  Burn (if any) happens on a detached
        //    copy, no conversion ever runs.
        //
        // 2. Non-paintable target (YUV, YCbCr, ...) with no burn:
        //    cache holds the *already-converted* target-format image.
        //    We do the one-time RGB16->target conversion inside the
        //    cache builder, then every subsequent create() call is a
        //    shallow copy with zero CSC work.
        //
        // 3. Non-paintable target with burn: cache must hold a
        //    paintable RGB16 background so we can burn onto a detached
        //    copy each frame.  We then convert to the target format
        //    after burn, via the cached CSCPipeline.
        //
        // The cache key includes the pixel desc ID, so toggling burn
        // on/off automatically invalidates when the cache format
        // changes between (2) and (3).
        const bool cacheInRGBBackground = !directPaint && wantBurn;
        const ImageDesc cacheDesc = cacheInRGBBackground
                ? rgbScratchDesc(desc)
                : desc;

        // Renders @p dst in place (paintable formats) or via an RGB16
        // scratch + Image::convert() (non-paintable).  The convert()
        // path pulls a compiled pipeline from the global CSC cache, so
        // repeated calls for the same format pair don't recompile.
        auto renderInto = [this, directPaint, cacheInRGBBackground,
                           &desc](Image &dst, double mo,
                                  const Color *solidColor) {
                auto runPattern = [this, mo, solidColor](Image &t) {
                        if(solidColor != nullptr) {
                                renderSolid(t, *solidColor);
                        } else {
                                render(t, mo);
                        }
                };
                if(directPaint || cacheInRGBBackground) {
                        // dst is in a paintable format (target or RGB16).
                        runPattern(dst);
                        return;
                }
                // Non-paintable target with no burn: render into an
                // RGB16 scratch, then convert into the cache slot.
                ImageDesc rgbDesc = rgbScratchDesc(desc);
                Image scratch(rgbDesc);
                if(!scratch.isValid()) return;
                runPattern(scratch);
                Image conv = scratch.convert(desc.pixelDesc(),
                                             desc.metadata());
                if(conv.isValid()) dst = conv;
        };

        Image out;

        if(_pattern == AvSync) {
                // AvSync: slot 0 = marker (white), slot 1 = non-marker
                // (black).  Each slot is cached once in cacheDesc format
                // and reused on every subsequent call.
                const bool marker = currentTimecode.isValid()
                                    && currentTimecode.frame() == 0;
                if(marker) {
                        out = cachedImage(0, cacheDesc, [&](Image &img) {
                                renderInto(img, 0.0, &Color::White);
                        });
                } else {
                        out = cachedImage(1, cacheDesc, [&](Image &img) {
                                renderInto(img, 0.0, &Color::Black);
                        });
                }
        } else if(isStaticPattern() && motionOffset == 0.0) {
                // Static pattern at offset 0 — render once into slot 0
                // and reuse on subsequent calls.  setPattern() and
                // setSolidColor() dump the cache, so the slot is
                // always consistent with _pattern / _solidColor.
                out = cachedImage(0, cacheDesc, [&](Image &img) {
                        renderInto(img, 0.0, nullptr);
                });
        } else {
                // Dynamic pattern (Noise, or any non-zero motion offset)
                // — render fresh every call, no caching.
                out = Image(cacheDesc);
                if(out.isValid()) {
                        renderInto(out, motionOffset, nullptr);
                }
        }

        if(!out.isValid()) return out;

        if(wantBurn) {
                // Burn on a detached copy so the cached background (if
                // any) stays pristine for the next frame.  `out` is in
                // a paintable format here: either the target (case 1)
                // or the RGB16 background (case 3).
                out.ensureExclusive();
                renderBurn(out, currentTimecode);

                // Case 3: convert the burn-in RGB16 image to the
                // caller's target format.  The global CSC cache keeps
                // the compiled pipeline alive between calls so this
                // doesn't pay compile() cost every frame.
                if(cacheInRGBBackground) {
                        out = out.convert(desc.pixelDesc(),
                                          desc.metadata());
                }
        }

        return out;
}

void VideoTestPattern::applyBurnFontConfig() const {
        if(_burnFont == nullptr) return;
        _burnFont->setFontFilename(_burnFontFilename);
        _burnFont->setFontSize(_burnFontSize);
        _burnFont->setForegroundColor(_burnTextColor);
        _burnFont->setBackgroundColor(_burnBackgroundColor);
        _burnFontConfigDirty = false;
}

void VideoTestPattern::renderBurn(Image &img, const Timecode &tc) const {
        // An empty _burnFontFilename is intentional: FastFont falls
        // back to the library's bundled default font internally.
        // Build the text lines we'll actually draw.
        String tcLine;
        if(tc.isValid()) {
                auto tcResult = tc.toString();
                tcLine = tcResult.first();
        }
        const String &textLine = _burnText;
        const bool hasTc = !tcLine.isEmpty();
        const bool hasText = !textLine.isEmpty();
        if(!hasTc && !hasText) return;

        // Lazy-create the FastFont bound to this image's paint engine.
        PaintEngine pe = img.createPaintEngine();
        if(_burnFont == nullptr) {
                _burnFont = new FastFont(pe);
                _burnFontConfigDirty = true;
        } else {
                _burnFont->setPaintEngine(pe);
        }
        if(_burnFontConfigDirty) applyBurnFontConfig();

        const int ascender = _burnFont->ascender();
        const int lineHeight = _burnFont->lineHeight();
        if(lineHeight <= 0 || ascender <= 0) {
                // FastFont failed to load — don't draw anything.
                return;
        }

        const int tcWidth = hasTc ? _burnFont->measureText(tcLine) : 0;
        const int textWidth = hasText ? _burnFont->measureText(textLine) : 0;
        const int maxTextWidth = tcWidth > textWidth ? tcWidth : textWidth;
        const int lineSpacing = lineHeight / 4;
        const int totalHeight = (hasTc ? lineHeight : 0)
                              + (hasText ? (hasTc ? lineSpacing : 0) + lineHeight : 0);

        int x = 0, y = 0;
        computeBurnPosition((int)img.width(), (int)img.height(),
                            maxTextWidth, totalHeight, ascender, x, y);

        if(_burnDrawBackground) {
                const int pad = lineHeight / 4;
                PaintEngine::Pixel bgPixel = pe.createPixel(_burnBackgroundColor);
                Rect<int32_t> bgRect(x - pad, y - ascender - pad,
                                     maxTextWidth + pad * 2,
                                     totalHeight + pad * 2);
                pe.fillRect(bgPixel, bgRect);
        }

        // Draw timecode line first (top), then static text below.
        int cursorY = y;
        if(hasTc) {
                const int lineX = x + (maxTextWidth - tcWidth) / 2;
                _burnFont->drawText(tcLine, lineX, cursorY);
                cursorY += lineHeight + lineSpacing;
        }
        if(hasText) {
                const int lineX = x + (maxTextWidth - textWidth) / 2;
                _burnFont->drawText(textLine, lineX, cursorY);
        }
}

void VideoTestPattern::computeBurnPosition(int frameW, int frameH,
                                           int textW, int totalH, int ascender,
                                           int &x, int &y) const {
        const int margin = ascender / 2;
        switch(_burnPosition) {
                case BurnTopLeft:
                        x = margin;
                        y = margin + ascender;
                        break;
                case BurnTopCenter:
                        x = (frameW - textW) / 2;
                        y = margin + ascender;
                        break;
                case BurnTopRight:
                        x = frameW - textW - margin;
                        y = margin + ascender;
                        break;
                case BurnBottomLeft:
                        x = margin;
                        y = frameH - margin - totalH + ascender;
                        break;
                case BurnBottomCenter:
                        x = (frameW - textW) / 2;
                        y = frameH - margin - totalH + ascender;
                        break;
                case BurnBottomRight:
                        x = frameW - textW - margin;
                        y = frameH - margin - totalH + ascender;
                        break;
                case BurnCenter:
                        x = (frameW - textW) / 2;
                        y = (frameH - totalH) / 2 + ascender;
                        break;
        }
}

void VideoTestPattern::render(Image &img, double motionOffset) const {
        switch(_pattern) {
                case ColorBars:      renderColorBars(img, motionOffset, true); break;
                case ColorBars75:    renderColorBars(img, motionOffset, false); break;
                case Ramp:           renderRamp(img, motionOffset); break;
                case Grid:           renderGrid(img, motionOffset); break;
                case Crosshatch:     renderCrosshatch(img, motionOffset); break;
                case Checkerboard:   renderCheckerboard(img, motionOffset); break;
                case SolidColor:     renderSolid(img, _solidColor); break;
                case White:          renderSolid(img, Color::White); break;
                case Black:          renderSolid(img, Color::Black); break;
                case Noise:          renderNoise(img); break;
                case ZonePlate:      renderZonePlate(img, motionOffset); break;
                // AvSync renders the "non-marker" frame (black) when
                // called via the bare render() path, since render()
                // doesn't carry a per-frame timecode.  The marker
                // logic only kicks in via create(desc, motion, tc).
                case AvSync:         renderSolid(img, Color::Black); break;
        }
}

Result<VideoTestPattern::Pattern> VideoTestPattern::fromString(const String &name) {
        if(name == "colorbars")    return makeResult(ColorBars);
        if(name == "colorbars75")  return makeResult(ColorBars75);
        if(name == "ramp")         return makeResult(Ramp);
        if(name == "grid")         return makeResult(Grid);
        if(name == "crosshatch")   return makeResult(Crosshatch);
        if(name == "checkerboard") return makeResult(Checkerboard);
        if(name == "solidcolor")   return makeResult(SolidColor);
        if(name == "white")        return makeResult(White);
        if(name == "black")        return makeResult(Black);
        if(name == "noise")        return makeResult(Noise);
        if(name == "zoneplate")    return makeResult(ZonePlate);
        if(name == "avsync")       return makeResult(AvSync);
        return makeError<Pattern>(Error::Invalid);
}

String VideoTestPattern::toString(Pattern pattern) {
        switch(pattern) {
                case ColorBars:    return "colorbars";
                case ColorBars75:  return "colorbars75";
                case Ramp:         return "ramp";
                case Grid:         return "grid";
                case Crosshatch:   return "crosshatch";
                case Checkerboard: return "checkerboard";
                case SolidColor:   return "solidcolor";
                case White:        return "white";
                case Black:        return "black";
                case Noise:        return "noise";
                case ZonePlate:    return "zoneplate";
                case AvSync:       return "avsync";
        }
        return "unknown";
}

void VideoTestPattern::renderColorBars(Image &img, double offset, bool full) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();
        int barWidth = w / 8;
        if(barWidth < 1) barWidth = 1;

        struct BarColor { uint16_t r, g, b; };
        BarColor bars100[] = {
                {65535, 65535, 65535}, {65535, 65535, 0},
                {0, 65535, 65535}, {0, 65535, 0},
                {65535, 0, 65535}, {65535, 0, 0},
                {0, 0, 65535}, {0, 0, 0}
        };
        BarColor bars75[] = {
                {49151, 49151, 49151}, {49151, 49151, 0},
                {0, 49151, 49151}, {0, 49151, 0},
                {49151, 0, 49151}, {49151, 0, 0},
                {0, 0, 49151}, {0, 0, 0}
        };

        BarColor *bars = full ? bars100 : bars75;
        int intOffset = (int)std::fmod(offset, (double)w);
        if(intOffset < 0) intOffset += w;

        for(int i = 0; i < 8; i++) {
                auto pixel = pe.createPixel(bars[i].r, bars[i].g, bars[i].b);
                int x0 = (i * barWidth + intOffset) % w;
                if(x0 + barWidth <= w) {
                        pe.fillRect(pixel, Rect<int32_t>(x0, 0, barWidth, h));
                } else {
                        int firstPart = w - x0;
                        pe.fillRect(pixel, Rect<int32_t>(x0, 0, firstPart, h));
                        pe.fillRect(pixel, Rect<int32_t>(0, 0, barWidth - firstPart, h));
                }
        }
        if(barWidth * 8 < w) {
                auto black = pe.createPixel(0, 0, 0);
                int remaining = w - barWidth * 8;
                int x0 = (barWidth * 8 + intOffset) % w;
                pe.fillRect(black, Rect<int32_t>(x0, 0, remaining, h));
        }
}

void VideoTestPattern::renderRamp(Image &img, double offset) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();
        int intOffset = (int)std::fmod(offset, (double)w);
        if(intOffset < 0) intOffset += w;

        for(int x = 0; x < w; x++) {
                int srcX = (x + intOffset) % w;
                uint16_t lum = (uint16_t)((double)srcX / (double)(w - 1) * 65535.0);
                auto pixel = pe.createPixel(lum, lum, lum);
                pe.fillRect(pixel, Rect<int32_t>(x, 0, 1, h));
        }
}

void VideoTestPattern::renderGrid(Image &img, double offset) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto white = pe.createPixel(65535, 65535, 65535);
        int spacing = 128;
        int intOffset = (int)std::fmod(offset, (double)spacing);
        if(intOffset < 0) intOffset += spacing;

        for(int x = intOffset; x < w; x += spacing) {
                pe.fillRect(white, Rect<int32_t>(x, 0, 1, h));
        }
        for(int y = intOffset; y < h; y += spacing) {
                pe.fillRect(white, Rect<int32_t>(0, y, w, 1));
        }
}

void VideoTestPattern::renderCrosshatch(Image &img, double offset) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto white = pe.createPixel(65535, 65535, 65535);
        int spacing = 96;
        int intOffset = (int)std::fmod(offset, (double)spacing);
        if(intOffset < 0) intOffset += spacing;

        for(int d = -h + intOffset; d < w + h; d += spacing) {
                pe.drawLine(white, d, 0, d + h, h);
        }
        for(int d = -h + intOffset; d < w + h; d += spacing) {
                pe.drawLine(white, w - d, 0, w - d - h, h);
        }
}

void VideoTestPattern::renderCheckerboard(Image &img, double offset) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto black = pe.createPixel(0, 0, 0);
        auto white = pe.createPixel(65535, 65535, 65535);
        pe.fill(black);

        int squareSize = 64;
        int intOffset = (int)std::fmod(offset, (double)(squareSize * 2));
        if(intOffset < 0) intOffset += squareSize * 2;

        for(int y = 0; y < h; y += squareSize) {
                for(int x = 0; x < w; x += squareSize) {
                        int adjX = x + intOffset;
                        int adjY = y + intOffset;
                        bool isWhite = ((adjX / squareSize) + (adjY / squareSize)) % 2 == 0;
                        if(isWhite) {
                                int rw = (x + squareSize > w) ? w - x : squareSize;
                                int rh = (y + squareSize > h) ? h - y : squareSize;
                                pe.fillRect(white, Rect<int32_t>(x, y, rw, rh));
                        }
                }
        }
}

void VideoTestPattern::renderZonePlate(Image &img, double phase) const {
        int w = (int)img.width();
        int h = (int)img.height();

        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        int bpp = img.pixelDesc().pixelFormat().bytesPerBlock();
        int components = img.pixelDesc().pixelFormat().compCount();
        double cx = w / 2.0;
        double cy = h / 2.0;
        double scale = 0.001;

        for(int y = 0; y < h; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < w; x++) {
                        double dx = x - cx;
                        double dy = y - cy;
                        double r2 = dx * dx + dy * dy;
                        double val = (std::sin(r2 * scale + phase * 0.1) + 1.0) * 0.5;
                        uint8_t lum = (uint8_t)(val * 255.0);
                        uint8_t *p = row + x * bpp;
                        for(int c = 0; c < components && c < 3; c++) {
                                p[c] = lum;
                        }
                        if(components >= 4) p[3] = 255;
                }
        }
}

void VideoTestPattern::renderNoise(Image &img) const {
        int w = (int)img.width();
        int h = (int)img.height();

        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        int bpp = img.pixelDesc().pixelFormat().bytesPerBlock();
        int components = img.pixelDesc().pixelFormat().compCount();

        Random rng;
        for(int y = 0; y < h; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < w; x++) {
                        uint8_t *p = row + x * bpp;
                        for(int c = 0; c < components && c < 3; c++) {
                                p[c] = (uint8_t)rng.randomInt(0, 255);
                        }
                        if(components >= 4) p[3] = 255;
                }
        }
}

void VideoTestPattern::renderSolid(Image &img, const Color &color) const {
        PaintEngine pe = img.createPaintEngine();
        auto pixel = pe.createPixel(color);
        pe.fill(pixel);
}

PROMEKI_NAMESPACE_END
