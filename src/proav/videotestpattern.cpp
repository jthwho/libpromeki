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
#include <promeki/pixelformat.h>
#include <promeki/random.h>
#include <promeki/fastfont.h>
#include <promeki/timecode.h>
#include <promeki/rect.h>
#include <promeki/stringlist.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

void sdiPathRepeatBlock(uint8_t *row, size_t bytes, const void *block, size_t blockSize) {
        size_t off = 0;
        while(off + blockSize <= bytes) {
                std::memcpy(row + off, block, blockSize);
                off += blockSize;
        }
        if(off < bytes) std::memcpy(row + off, block, bytes - off);
}

void sdiPathStore16(uint8_t *dst, uint16_t val, bool be) {
        if(be) {
                dst[0] = static_cast<uint8_t>(val >> 8);
                dst[1] = static_cast<uint8_t>(val);
        } else {
                dst[0] = static_cast<uint8_t>(val);
                dst[1] = static_cast<uint8_t>(val >> 8);
        }
}

} // anonymous namespace

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

bool VideoTestPattern::isStaticPattern() const {
        return _pattern != VideoPattern::Noise;
}

void VideoTestPattern::invalidateImageCache() const {
        for(int i = 0; i < CacheSlotCount; i++) {
                _cachedImages[i] = Image();
        }
        _cacheW = 0;
        _cacheH = 0;
        _cachePixelFormatId = 0;
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
                     PixelFormat(PixelFormat::RGBA8_sRGB));
        rd.metadata() = target.metadata();
        return rd;
}

Image VideoTestPattern::create(const ImageDesc &desc, double motionOffset,
                               const Timecode &currentTimecode) const {
        const bool directPaint = desc.pixelFormat().hasPaintEngine()
                                 && _pattern != VideoPattern::ZonePlate
                                 && _pattern != VideoPattern::CircularZone
                                 && _pattern != VideoPattern::Noise;

        // Patterns that use PaintEngine (color bars, ramp, grid, etc.)
        // render natively into any format that has a paint engine —
        // including YCbCr — because createPixel(Color) auto-converts
        // to the target color model.  Patterns that write raw bytes
        // (ZonePlate, Noise) always go through an RGBA8 scratch and
        // CSC-convert into the target.  The background is cached in
        // the caller's target pixel format either way.  Burn-in is a
        // separate pass (@ref applyBurn) so create() never has to pick
        // a paintable intermediate just because text might be drawn
        // later.
        auto renderInto = [this, directPaint, &desc](Image &dst, double mo,
                                                     const Color *solidColor) {
                auto runPattern = [this, mo, solidColor](Image &t) {
                        if(solidColor != nullptr) {
                                renderSolid(t, *solidColor);
                        } else {
                                render(t, mo);
                        }
                };
                if(directPaint) {
                        runPattern(dst);
                        return;
                }
                // Scratch path: render into RGBA8 and CSC-convert.
                ImageDesc rgbDesc = rgbScratchDesc(desc);
                Image scratch(rgbDesc);
                if(!scratch.isValid()) return;
                runPattern(scratch);
                Image conv = scratch.convert(desc.pixelFormat(),
                                             desc.metadata());
                if(conv.isValid()) dst = conv;
        };

        Image out;

        if(_pattern == VideoPattern::AvSync) {
                // AvSync: slot 0 = marker (white), slot 1 = non-marker
                // (black).  Each slot is cached once in target format
                // and reused on every subsequent call.
                const bool marker = currentTimecode.isValid()
                                    && currentTimecode.frame() == 0;
                if(marker) {
                        out = cachedImage(0, desc, [&](Image &img) {
                                renderInto(img, 0.0, &Color::White);
                        });
                } else {
                        out = cachedImage(1, desc, [&](Image &img) {
                                renderInto(img, 0.0, &Color::Black);
                        });
                }
        } else if(_pattern == VideoPattern::SDIPathEQ || _pattern == VideoPattern::SDIPathPLL) {
                // SDI pathological patterns write exact digital word
                // values for 422 targets, bypassing PaintEngine / CSC.
                // Non-422 targets fall through to the scratch + CSC path
                // which produces an approximate visual.
                if(desc.pixelFormat().memLayout().sampling() == PixelMemLayout::Sampling422) {
                        out = cachedImage(0, desc, [&](Image &img) {
                                render(img, 0.0);
                        });
                } else {
                        out = cachedImage(0, desc, [&](Image &img) {
                                renderInto(img, 0.0, nullptr);
                        });
                }
        } else if(isStaticPattern() && motionOffset == 0.0) {
                // Static pattern at offset 0 — render once into slot 0
                // and reuse on subsequent calls.  setPattern() and
                // setSolidColor() dump the cache, so the slot is
                // always consistent with _pattern / _solidColor.
                out = cachedImage(0, desc, [&](Image &img) {
                        renderInto(img, 0.0, nullptr);
                });
        } else {
                // Dynamic pattern (Noise, or any non-zero motion offset)
                // — render fresh every call, no caching.
                out = Image(desc);
                if(out.isValid()) {
                        renderInto(out, motionOffset, nullptr);
                }
        }

        return out;
}

void VideoTestPattern::applyBurnFontConfig() const {
        if(_burnFont == nullptr) return;
        _burnFont->setFontFilename(_burnFontFilename);
        _burnFont->setFontSize(_burnEffectiveFontSize);
        _burnFont->setForegroundColor(_burnTextColor);
        _burnFont->setBackgroundColor(_burnBackgroundColor);
        _burnFontConfigDirty = false;
}

Error VideoTestPattern::applyBurn(Image &img, const String &burnText) const {
        if(!_burnEnabled || burnText.isEmpty()) return Error::Ok;
        if(!img.isValid()) return Error::InvalidArgument;
        if(!img.pixelFormat().hasPaintEngine()) {
                promekiWarn("VideoTestPattern::applyBurn: image pixel "
                            "format '%s' has no paint engine — burn skipped",
                            img.pixelFormat().name().cstr());
                return Error::NotSupported;
        }

        // An empty _burnFontFilename is intentional: FastFont falls
        // back to the library's bundled default font internally.
        // Interpret literal backslash-n as newline so config strings
        // from the command line (where shell quoting prevents real
        // newlines) split correctly.  Then split on actual newlines.
        String text = burnText.replace("\\n", "\n");
        StringList lines = text.split("\n");
        if(lines.isEmpty()) return Error::Ok;

        // Resolve the effective font size.  A configured size of 0
        // (auto) scales from the rendered image height using 36 px at
        // 1080 lines as the reference; explicit non-zero values pass
        // through unchanged.  When the effective size differs from the
        // last applied value (either because the config changed or
        // because the image height changed under auto mode) the font
        // config gets marked dirty so FastFont is reconfigured below.
        int effectiveSize = _burnFontSize;
        if(effectiveSize <= 0) {
                const int h = static_cast<int>(img.height());
                effectiveSize = (h * 36 + 540) / 1080;
                if(effectiveSize < 1) effectiveSize = 1;
        }
        if(effectiveSize != _burnEffectiveFontSize) {
                _burnEffectiveFontSize = effectiveSize;
                _burnFontConfigDirty = true;
        }

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
                return Error::FontUnavailable;
        }

        // Measure each line; the bounding box width is the max line
        // width and the height stacks all lines with a quarter-line
        // gap between them.
        List<int> widths;
        int maxTextWidth = 0;
        for(const String &line : lines) {
                int w = _burnFont->measureText(line);
                widths.pushToBack(w);
                if(w > maxTextWidth) maxTextWidth = w;
        }
        const int lineSpacing = lineHeight / 4;
        const int n = static_cast<int>(lines.size());
        const int totalHeight = n * lineHeight
                              + (n > 1 ? (n - 1) * lineSpacing : 0);

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

        // Draw lines top-to-bottom, each centered inside the bounding box.
        int cursorY = y;
        for(int i = 0; i < n; ++i) {
                const int lineX = x + (maxTextWidth - widths[i]) / 2;
                _burnFont->drawText(lines[i], lineX, cursorY);
                cursorY += lineHeight + lineSpacing;
        }
        return Error::Ok;
}

void VideoTestPattern::computeBurnPosition(int frameW, int frameH,
                                           int textW, int totalH, int ascender,
                                           int &x, int &y) const {
        const int margin = ascender / 2;
        if(_burnPosition == BurnPosition::TopLeft) {
                x = margin;
                y = margin + ascender;
        } else if(_burnPosition == BurnPosition::TopCenter) {
                x = (frameW - textW) / 2;
                y = margin + ascender;
        } else if(_burnPosition == BurnPosition::TopRight) {
                x = frameW - textW - margin;
                y = margin + ascender;
        } else if(_burnPosition == BurnPosition::BottomLeft) {
                x = margin;
                y = frameH - margin - totalH + ascender;
        } else if(_burnPosition == BurnPosition::BottomCenter) {
                x = (frameW - textW) / 2;
                y = frameH - margin - totalH + ascender;
        } else if(_burnPosition == BurnPosition::BottomRight) {
                x = frameW - textW - margin;
                y = frameH - margin - totalH + ascender;
        } else if(_burnPosition == BurnPosition::Center) {
                x = (frameW - textW) / 2;
                y = (frameH - totalH) / 2 + ascender;
        }
}

void VideoTestPattern::render(Image &img, double motionOffset) const {
        if(_pattern == VideoPattern::ColorBars)         renderColorBars(img, motionOffset, true);
        else if(_pattern == VideoPattern::ColorBars75)  renderColorBars(img, motionOffset, false);
        else if(_pattern == VideoPattern::Ramp)         renderRamp(img, motionOffset);
        else if(_pattern == VideoPattern::Grid)         renderGrid(img, motionOffset);
        else if(_pattern == VideoPattern::Crosshatch)   renderCrosshatch(img, motionOffset);
        else if(_pattern == VideoPattern::Checkerboard) renderCheckerboard(img, motionOffset);
        else if(_pattern == VideoPattern::SolidColor)   renderSolid(img, _solidColor);
        else if(_pattern == VideoPattern::White)        renderSolid(img, Color::White);
        else if(_pattern == VideoPattern::Black)        renderSolid(img, Color::Black);
        else if(_pattern == VideoPattern::Noise)        renderNoise(img);
        else if(_pattern == VideoPattern::ZonePlate)    renderZonePlate(img, motionOffset);
        else if(_pattern == VideoPattern::ColorChecker) renderColorChecker(img);
        else if(_pattern == VideoPattern::SMPTE219)     renderSMPTE219(img);
        else if(_pattern == VideoPattern::MultiBurst)   renderMultiBurst(img);
        else if(_pattern == VideoPattern::LimitRange)   renderLimitRange(img);
        else if(_pattern == VideoPattern::CircularZone) renderCircularZone(img, motionOffset);
        else if(_pattern == VideoPattern::Alignment)    renderAlignment(img);
        else if(_pattern == VideoPattern::SDIPathEQ)    renderSDIPathological(img, true);
        else if(_pattern == VideoPattern::SDIPathPLL)   renderSDIPathological(img, false);
        // AvSync renders the "non-marker" frame (black) when called via
        // the bare render() path, since render() doesn't carry a
        // per-frame timecode.  The marker logic only kicks in via
        // create(desc, motion, tc).
        else if(_pattern == VideoPattern::AvSync)       renderSolid(img, Color::Black);
}

void VideoTestPattern::renderColorBars(Image &img, double offset, bool full) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();
        int barWidth = w / 8;
        if(barWidth < 1) barWidth = 1;

        const float lv = full ? 1.0f : 0.75f;
        const Color bars[] = {
                Color::srgb(lv,   lv,   lv),
                Color::srgb(lv,   lv,   0.0f),
                Color::srgb(0.0f, lv,   lv),
                Color::srgb(0.0f, lv,   0.0f),
                Color::srgb(lv,   0.0f, lv),
                Color::srgb(lv,   0.0f, 0.0f),
                Color::srgb(0.0f, 0.0f, lv),
                Color::Black
        };
        int intOffset = (int)std::fmod(offset, (double)w);
        if(intOffset < 0) intOffset += w;

        for(int i = 0; i < 8; i++) {
                auto pixel = pe.createPixel(bars[i]);
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
                auto black = pe.createPixel(Color::Black);
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
                float lum = (float)srcX / (float)(w - 1);
                auto pixel = pe.createPixel(Color::srgb(lum, lum, lum));
                pe.fillRect(pixel, Rect<int32_t>(x, 0, 1, h));
        }
}

void VideoTestPattern::renderGrid(Image &img, double offset) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto black = pe.createPixel(Color::Black);
        pe.fill(black);

        auto white = pe.createPixel(Color::White);
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

        auto black = pe.createPixel(Color::Black);
        pe.fill(black);

        auto white = pe.createPixel(Color::White);
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

        auto black = pe.createPixel(Color::Black);
        auto white = pe.createPixel(Color::White);
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
        int bpp = img.pixelFormat().memLayout().bytesPerBlock();
        int components = img.pixelFormat().memLayout().compCount();
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
        int bpp = img.pixelFormat().memLayout().bytesPerBlock();
        int components = img.pixelFormat().memLayout().compCount();

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

void VideoTestPattern::renderColorChecker(Image &img) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        // X-Rite ColorChecker Classic: 6 columns x 4 rows = 24 patches.
        // sRGB values from the BabelColor "Average" reference data set.
        static const Color patches[24] = {
                Color::srgb(115/255.0f,  82/255.0f,  68/255.0f),  // Dark Skin
                Color::srgb(194/255.0f, 150/255.0f, 130/255.0f),  // Light Skin
                Color::srgb( 98/255.0f, 122/255.0f, 157/255.0f),  // Blue Sky
                Color::srgb( 87/255.0f, 108/255.0f,  67/255.0f),  // Foliage
                Color::srgb(133/255.0f, 128/255.0f, 177/255.0f),  // Blue Flower
                Color::srgb(103/255.0f, 189/255.0f, 170/255.0f),  // Bluish Green
                Color::srgb(214/255.0f, 126/255.0f,  44/255.0f),  // Orange
                Color::srgb( 80/255.0f,  91/255.0f, 166/255.0f),  // Purplish Blue
                Color::srgb(193/255.0f,  90/255.0f,  99/255.0f),  // Moderate Red
                Color::srgb( 94/255.0f,  60/255.0f, 108/255.0f),  // Purple
                Color::srgb(157/255.0f, 188/255.0f,  64/255.0f),  // Yellow Green
                Color::srgb(224/255.0f, 163/255.0f,  46/255.0f),  // Orange Yellow
                Color::srgb( 56/255.0f,  61/255.0f, 150/255.0f),  // Blue
                Color::srgb( 70/255.0f, 148/255.0f,  73/255.0f),  // Green
                Color::srgb(175/255.0f,  54/255.0f,  60/255.0f),  // Red
                Color::srgb(231/255.0f, 199/255.0f,  31/255.0f),  // Yellow
                Color::srgb(187/255.0f,  86/255.0f, 149/255.0f),  // Magenta
                Color::srgb(  8/255.0f, 133/255.0f, 161/255.0f),  // Cyan
                Color::srgb(243/255.0f, 243/255.0f, 242/255.0f),  // White 9.5
                Color::srgb(200/255.0f, 200/255.0f, 200/255.0f),  // Neutral 8
                Color::srgb(160/255.0f, 160/255.0f, 160/255.0f),  // Neutral 6.5
                Color::srgb(122/255.0f, 122/255.0f, 121/255.0f),  // Neutral 5
                Color::srgb( 85/255.0f,  85/255.0f,  85/255.0f),  // Neutral 3.5
                Color::srgb( 52/255.0f,  52/255.0f,  52/255.0f),  // Black 2
        };

        auto bg = pe.createPixel(Color::srgb(0.2f, 0.2f, 0.2f));
        pe.fill(bg);

        const int cols = 6;
        const int rows = 4;
        int gap = w / 80;
        if(gap < 1) gap = 1;

        int patchW = (w - gap * (cols + 1)) / cols;
        int patchH = (h - gap * (rows + 1)) / rows;

        for(int row = 0; row < rows; row++) {
                for(int col = 0; col < cols; col++) {
                        int idx = row * cols + col;
                        int x = gap + col * (patchW + gap);
                        int y = gap + row * (patchH + gap);
                        auto pixel = pe.createPixel(patches[idx]);
                        pe.fillRect(pixel, Rect<int32_t>(x, y, patchW, patchH));
                }
        }
}

void VideoTestPattern::renderSMPTE219(Image &img) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        const int topH = h * 2 / 3;
        const int midH = h / 12;
        const int botH = h - topH - midH;
        const int botY = topH + midH;
        const int barW = w / 7;

        const float lv = 0.75f;
        const Color topBars[7] = {
                Color::srgb(lv,   lv,   lv),
                Color::srgb(lv,   lv,   0.0f),
                Color::srgb(0.0f, lv,   lv),
                Color::srgb(0.0f, lv,   0.0f),
                Color::srgb(lv,   0.0f, lv),
                Color::srgb(lv,   0.0f, 0.0f),
                Color::srgb(0.0f, 0.0f, lv),
        };

        for(int i = 0; i < 7; i++) {
                auto pixel = pe.createPixel(topBars[i]);
                int x = i * barW;
                int bw = (i == 6) ? (w - x) : barW;
                pe.fillRect(pixel, Rect<int32_t>(x, 0, bw, topH));
        }

        const Color midBars[7] = {
                Color::srgb(0.0f, 0.0f, lv),
                Color::Black,
                Color::srgb(lv,   0.0f, lv),
                Color::Black,
                Color::srgb(0.0f, lv,   lv),
                Color::Black,
                Color::srgb(lv,   lv,   lv),
        };

        for(int i = 0; i < 7; i++) {
                auto pixel = pe.createPixel(midBars[i]);
                int x = i * barW;
                int bw = (i == 6) ? (w - x) : barW;
                pe.fillRect(pixel, Rect<int32_t>(x, topH, bw, midH));
        }

        auto negI = pe.createPixel(Color::srgb(0.0f, 0.129f, 0.298f));
        pe.fillRect(negI, Rect<int32_t>(0, botY, barW, botH));

        auto white = pe.createPixel(Color::White);
        pe.fillRect(white, Rect<int32_t>(barW, botY, barW, botH));

        auto posQ = pe.createPixel(Color::srgb(0.196f, 0.0f, 0.416f));
        pe.fillRect(posQ, Rect<int32_t>(barW * 2, botY, barW, botH));

        auto black = pe.createPixel(Color::Black);
        pe.fillRect(black, Rect<int32_t>(barW * 3, botY, w - barW * 3, botH));

        // PLUGE in bar 5 area: sub-black (same as black in sRGB) |
        // reference black | above-black (+4%).  Only the above-black
        // strip needs drawing over the black fill.
        int plugeW = barW / 3;
        int plugeX = barW * 5;
        auto aboveBlack = pe.createPixel(Color::srgb(0.04f, 0.04f, 0.04f));
        pe.fillRect(aboveBlack, Rect<int32_t>(plugeX + plugeW * 2, botY,
                                               plugeW, botH));
}

void VideoTestPattern::renderMultiBurst(Image &img) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto gray = pe.createPixel(Color::srgb(0.5f, 0.5f, 0.5f));
        pe.fill(gray);

        auto black = pe.createPixel(Color::Black);
        auto white = pe.createPixel(Color::White);

        int refW = w / 12;
        if(refW < 2) refW = 2;
        int margin = w / 60;
        if(margin < 1) margin = 1;
        pe.fillRect(white, Rect<int32_t>(margin, 0, refW, h));

        int packetStart = refW + margin * 2;
        int packetRegion = w - packetStart - margin;
        int gap = w / 120;
        if(gap < 1) gap = 1;
        const int packetCount = 6;
        int packetW = (packetRegion - gap * (packetCount - 1)) / packetCount;
        if(packetW < 2) packetW = 2;

        int baseBar = packetW / 4;
        if(baseBar < 1) baseBar = 1;

        for(int s = 0; s < packetCount; s++) {
                int x0 = packetStart + s * (packetW + gap);
                int barW = baseBar >> s;
                if(barW < 1) barW = 1;
                for(int x = 0; x < packetW; ) {
                        bool isWhiteBand = (x / barW) % 2 == 0;
                        int runEnd = ((x / barW) + 1) * barW;
                        if(runEnd > packetW) runEnd = packetW;
                        pe.fillRect(isWhiteBand ? white : black,
                                    Rect<int32_t>(x0 + x, 0, runEnd - x, h));
                        x = runEnd;
                }
        }
}

void VideoTestPattern::renderLimitRange(Image &img) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        struct Level {
                float   lum;
                Color   marker;
        };
        static const Level levels[] = {
                { 0.0f,          Color::srgb(0.25f, 0.0f,  0.0f)  },  // Black
                { 16.0f / 255,   Color::srgb(0.0f,  0.0f,  0.25f) },  // Limited-range black
                { 0.25f,         Color::Black                      },  // 25%
                { 0.50f,         Color::Black                      },  // 50%
                { 0.75f,         Color::Black                      },  // 75%
                { 235.0f / 255,  Color::srgb(0.0f,  0.25f, 0.0f)  },  // Limited-range white
                { 1.0f,          Color::srgb(0.25f, 0.0f,  0.0f)  },  // White
        };
        static constexpr int count = sizeof(levels) / sizeof(levels[0]);

        int markerW = w / 30;
        if(markerW < 2) markerW = 2;

        for(int i = 0; i < count; i++) {
                int y0 = h * i / count;
                int y1 = h * (i + 1) / count;
                int barH = y1 - y0;
                float lum = levels[i].lum;
                auto pixel = pe.createPixel(Color::srgb(lum, lum, lum));
                pe.fillRect(pixel, Rect<int32_t>(0, y0, w, barH));
                auto marker = pe.createPixel(levels[i].marker);
                pe.fillRect(marker, Rect<int32_t>(0, y0, markerW, barH));
        }
}

void VideoTestPattern::renderCircularZone(Image &img, double phase) const {
        int w = (int)img.width();
        int h = (int)img.height();

        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        int bpp = img.pixelFormat().memLayout().bytesPerBlock();
        int components = img.pixelFormat().memLayout().compCount();
        double cx = w / 2.0;
        double cy = h / 2.0;
        double freq = 2.0 * M_PI / 8.0;

        for(int y = 0; y < h; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < w; x++) {
                        double dx = x - cx;
                        double dy = y - cy;
                        double r = std::sqrt(dx * dx + dy * dy);
                        double val = (std::sin(r * freq + phase * 0.1) + 1.0) * 0.5;
                        uint8_t lum = (uint8_t)(val * 255.0);
                        uint8_t *p = row + x * bpp;
                        for(int c = 0; c < components && c < 3; c++) {
                                p[c] = lum;
                        }
                        if(components >= 4) p[3] = 255;
                }
        }
}

void VideoTestPattern::renderAlignment(Image &img) const {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();
        int cx = w / 2;
        int cy = h / 2;

        auto black = pe.createPixel(Color::Black);
        pe.fill(black);

        auto white = pe.createPixel(Color::White);
        auto gray = pe.createPixel(Color::srgb(0.25f, 0.25f, 0.25f));

        // Title-safe (80%) rectangle
        int tsX = w / 10;
        int tsY = h / 10;
        pe.drawRect(gray, Rect<int32_t>(tsX, tsY, w - tsX * 2, h - tsY * 2));

        // Action-safe (90%) rectangle
        int asX = w / 20;
        int asY = h / 20;
        pe.drawRect(white, Rect<int32_t>(asX, asY, w - asX * 2, h - asY * 2));

        // Center crosshair
        pe.drawLine(white, cx, 0, cx, h - 1);
        pe.drawLine(white, 0, cy, w - 1, cy);

        // Corner brackets
        int bLen = w / 20;
        if(bLen < 4) bLen = 4;
        pe.drawLine(white, 0, 0, bLen, 0);
        pe.drawLine(white, 0, 0, 0, bLen);
        pe.drawLine(white, w - 1, 0, w - 1 - bLen, 0);
        pe.drawLine(white, w - 1, 0, w - 1, bLen);
        pe.drawLine(white, 0, h - 1, bLen, h - 1);
        pe.drawLine(white, 0, h - 1, 0, h - 1 - bLen);
        pe.drawLine(white, w - 1, h - 1, w - 1 - bLen, h - 1);
        pe.drawLine(white, w - 1, h - 1, w - 1, h - 1 - bLen);

        // Tick marks along edges every 10%
        int tickLen = h / 40;
        if(tickLen < 2) tickLen = 2;
        for(int i = 1; i < 10; i++) {
                int tx = w * i / 10;
                int ty = h * i / 10;
                pe.drawLine(white, tx, 0, tx, tickLen);
                pe.drawLine(white, tx, h - 1, tx, h - 1 - tickLen);
                pe.drawLine(white, 0, ty, tickLen, ty);
                pe.drawLine(white, w - 1, ty, w - 1 - tickLen, ty);
        }

        // Rule-of-thirds crosshairs
        int thirdLen = w / 60;
        if(thirdLen < 2) thirdLen = 2;
        for(int gy = 1; gy <= 2; gy++) {
                for(int gx = 1; gx <= 2; gx++) {
                        int px = w * gx / 3;
                        int py = h * gy / 3;
                        pe.drawLine(gray, px - thirdLen, py, px + thirdLen, py);
                        pe.drawLine(gray, px, py - thirdLen, px, py + thirdLen);
                }
        }
}

void VideoTestPattern::renderSDIPathological(Image &img, bool isEQ) const {
        // SMPTE RP 198 reference values (10-bit).
        //   Check Field (EQ stress): W0 = 0x300, W1 = 0x198
        //   Matrix      (PLL stress): W0 = 0x200, W1 = 0x110
        //
        // Every line has uniform component values that alternate phase:
        //   Even lines: Cb = W0, Y = W1, Cr = W0
        //   Odd  lines: Cb = W1, Y = W0, Cr = W1
        const uint16_t w0 = isEQ ? uint16_t(0x300) : uint16_t(0x200);
        const uint16_t w1 = isEQ ? uint16_t(0x198) : uint16_t(0x110);

        const int iw = static_cast<int>(img.width());
        const int ih = static_cast<int>(img.height());
        const auto pfId = img.pixelFormat().memLayout().id();

        // ---- Interleaved UYVY 8-bit ----
        if(pfId == PixelMemLayout::I_422_UYVY_3x8) {
                uint8_t v0 = static_cast<uint8_t>(w0 >> 2);
                uint8_t v1 = static_cast<uint8_t>(w1 >> 2);
                uint8_t blockE[4] = { v0, v1, v0, v1 };
                uint8_t blockO[4] = { v1, v0, v1, v0 };
                uint8_t *buf = static_cast<uint8_t *>(img.data(0));
                size_t stride = img.lineStride(0);
                size_t lineBytes = static_cast<size_t>(iw / 2) * 4;
                for(int y = 0; y < ih; y++)
                        sdiPathRepeatBlock(buf + y * stride, lineBytes,
                                           (y & 1) ? blockO : blockE, 4);
                return;
        }

        // ---- Interleaved UYVY 10/12/16-bit (LE and BE) ----
        if(pfId == PixelMemLayout::I_422_UYVY_3x10_LE || pfId == PixelMemLayout::I_422_UYVY_3x10_BE ||
           pfId == PixelMemLayout::I_422_UYVY_3x12_LE || pfId == PixelMemLayout::I_422_UYVY_3x12_BE ||
           pfId == PixelMemLayout::I_422_UYVY_3x16_LE || pfId == PixelMemLayout::I_422_UYVY_3x16_BE) {
                int shift = 0;
                bool be = false;
                if(pfId == PixelMemLayout::I_422_UYVY_3x10_BE)      be = true;
                else if(pfId == PixelMemLayout::I_422_UYVY_3x12_LE) shift = 2;
                else if(pfId == PixelMemLayout::I_422_UYVY_3x12_BE) { shift = 2; be = true; }
                else if(pfId == PixelMemLayout::I_422_UYVY_3x16_LE) shift = 6;
                else if(pfId == PixelMemLayout::I_422_UYVY_3x16_BE) { shift = 6; be = true; }
                uint16_t v0 = static_cast<uint16_t>(w0 << shift);
                uint16_t v1 = static_cast<uint16_t>(w1 << shift);
                uint8_t blockE[8], blockO[8];
                sdiPathStore16(blockE + 0, v0, be); sdiPathStore16(blockE + 2, v1, be);
                sdiPathStore16(blockE + 4, v0, be); sdiPathStore16(blockE + 6, v1, be);
                sdiPathStore16(blockO + 0, v1, be); sdiPathStore16(blockO + 2, v0, be);
                sdiPathStore16(blockO + 4, v1, be); sdiPathStore16(blockO + 6, v0, be);
                uint8_t *buf = static_cast<uint8_t *>(img.data(0));
                size_t stride = img.lineStride(0);
                size_t lineBytes = static_cast<size_t>(iw / 2) * 8;
                for(int y = 0; y < ih; y++)
                        sdiPathRepeatBlock(buf + y * stride, lineBytes,
                                           (y & 1) ? blockO : blockE, 8);
                return;
        }

        // ---- Interleaved YUYV 8-bit ----
        if(pfId == PixelMemLayout::I_422_3x8) {
                uint8_t v0 = static_cast<uint8_t>(w0 >> 2);
                uint8_t v1 = static_cast<uint8_t>(w1 >> 2);
                uint8_t blockE[4] = { v1, v0, v1, v0 };
                uint8_t blockO[4] = { v0, v1, v0, v1 };
                uint8_t *buf = static_cast<uint8_t *>(img.data(0));
                size_t stride = img.lineStride(0);
                size_t lineBytes = static_cast<size_t>(iw / 2) * 4;
                for(int y = 0; y < ih; y++)
                        sdiPathRepeatBlock(buf + y * stride, lineBytes,
                                           (y & 1) ? blockO : blockE, 4);
                return;
        }

        // ---- Interleaved YUYV 10-bit (LE) ----
        if(pfId == PixelMemLayout::I_422_3x10) {
                uint8_t blockE[8], blockO[8];
                sdiPathStore16(blockE + 0, w1, false); sdiPathStore16(blockE + 2, w0, false);
                sdiPathStore16(blockE + 4, w1, false); sdiPathStore16(blockE + 6, w0, false);
                sdiPathStore16(blockO + 0, w0, false); sdiPathStore16(blockO + 2, w1, false);
                sdiPathStore16(blockO + 4, w0, false); sdiPathStore16(blockO + 6, w1, false);
                uint8_t *buf = static_cast<uint8_t *>(img.data(0));
                size_t stride = img.lineStride(0);
                size_t lineBytes = static_cast<size_t>(iw / 2) * 8;
                for(int y = 0; y < ih; y++)
                        sdiPathRepeatBlock(buf + y * stride, lineBytes,
                                           (y & 1) ? blockO : blockE, 8);
                return;
        }

        // ---- v210 packed ----
        if(pfId == PixelMemLayout::I_422_v210) {
                // v210 word layout (each 32-bit LE word holds 3 x 10-bit values):
                //   Word 0: Cb0[9:0]  Y0[19:10] Cr0[29:20]
                //   Word 1: Y1[9:0]   Cb1[19:10] Y2[29:20]
                // Pattern repeats every 2 words (8 bytes) because all Y,
                // Cb, Cr values are uniform within each line.
                uint32_t wordA = (w0 & 0x3FFu)
                               | ((w1 & 0x3FFu) << 10)
                               | ((w0 & 0x3FFu) << 20);
                uint32_t wordB = (w1 & 0x3FFu)
                               | ((w0 & 0x3FFu) << 10)
                               | ((w1 & 0x3FFu) << 20);
                uint32_t blockE[2] = { wordA, wordB };
                uint32_t blockO[2] = { wordB, wordA };
                uint8_t *buf = static_cast<uint8_t *>(img.data(0));
                size_t stride = img.lineStride(0);
                for(int y = 0; y < ih; y++)
                        sdiPathRepeatBlock(buf + y * stride, stride,
                                           (y & 1) ? blockO : blockE, 8);
                return;
        }

        // ---- Planar 422 8-bit ----
        if(pfId == PixelMemLayout::P_422_3x8) {
                uint8_t v0 = static_cast<uint8_t>(w0 >> 2);
                uint8_t v1 = static_cast<uint8_t>(w1 >> 2);
                int chromaW = iw / 2;
                for(int p = 0; p < 3; p++) {
                        uint8_t *buf = static_cast<uint8_t *>(img.data(p));
                        size_t stride = img.lineStride(p);
                        int pw = (p == 0) ? iw : chromaW;
                        uint8_t evenVal = (p == 0) ? v1 : v0;
                        uint8_t oddVal  = (p == 0) ? v0 : v1;
                        for(int y = 0; y < ih; y++)
                                std::memset(buf + y * stride,
                                            (y & 1) ? oddVal : evenVal, pw);
                }
                return;
        }

        // ---- Planar 422 10/12/16-bit (LE and BE) ----
        if(pfId == PixelMemLayout::P_422_3x10_LE || pfId == PixelMemLayout::P_422_3x10_BE ||
           pfId == PixelMemLayout::P_422_3x12_LE || pfId == PixelMemLayout::P_422_3x12_BE ||
           pfId == PixelMemLayout::P_422_3x16_LE || pfId == PixelMemLayout::P_422_3x16_BE) {
                int shift = 0;
                bool be = false;
                if(pfId == PixelMemLayout::P_422_3x10_BE)      be = true;
                else if(pfId == PixelMemLayout::P_422_3x12_LE) shift = 2;
                else if(pfId == PixelMemLayout::P_422_3x12_BE) { shift = 2; be = true; }
                else if(pfId == PixelMemLayout::P_422_3x16_LE) shift = 6;
                else if(pfId == PixelMemLayout::P_422_3x16_BE) { shift = 6; be = true; }
                uint16_t v0 = static_cast<uint16_t>(w0 << shift);
                uint16_t v1 = static_cast<uint16_t>(w1 << shift);
                uint8_t pat0[2], pat1[2];
                sdiPathStore16(pat0, v0, be);
                sdiPathStore16(pat1, v1, be);
                int chromaW = iw / 2;
                for(int p = 0; p < 3; p++) {
                        uint8_t *buf = static_cast<uint8_t *>(img.data(p));
                        size_t stride = img.lineStride(p);
                        int pw = (p == 0) ? iw : chromaW;
                        const uint8_t *evenPat = (p == 0) ? pat1 : pat0;
                        const uint8_t *oddPat  = (p == 0) ? pat0 : pat1;
                        for(int y = 0; y < ih; y++)
                                sdiPathRepeatBlock(buf + y * stride,
                                                   static_cast<size_t>(pw) * 2,
                                                   (y & 1) ? oddPat : evenPat, 2);
                }
                return;
        }

        // ---- Semi-planar 422 8-bit ----
        if(pfId == PixelMemLayout::SP_422_8) {
                uint8_t v0 = static_cast<uint8_t>(w0 >> 2);
                uint8_t v1 = static_cast<uint8_t>(w1 >> 2);
                int chromaW = iw / 2;
                {
                        uint8_t *buf = static_cast<uint8_t *>(img.data(0));
                        size_t stride = img.lineStride(0);
                        for(int y = 0; y < ih; y++)
                                std::memset(buf + y * stride,
                                            (y & 1) ? v0 : v1, iw);
                }
                {
                        uint8_t *buf = static_cast<uint8_t *>(img.data(1));
                        size_t stride = img.lineStride(1);
                        uint8_t blockE[2] = { v0, v0 };
                        uint8_t blockO[2] = { v1, v1 };
                        for(int y = 0; y < ih; y++)
                                sdiPathRepeatBlock(buf + y * stride,
                                                   static_cast<size_t>(chromaW) * 2,
                                                   (y & 1) ? blockO : blockE, 2);
                }
                return;
        }

        // ---- Semi-planar 422 10/12-bit (LE and BE) ----
        if(pfId == PixelMemLayout::SP_422_10_LE || pfId == PixelMemLayout::SP_422_10_BE ||
           pfId == PixelMemLayout::SP_422_12_LE || pfId == PixelMemLayout::SP_422_12_BE) {
                int shift = 0;
                bool be = false;
                if(pfId == PixelMemLayout::SP_422_10_BE)      be = true;
                else if(pfId == PixelMemLayout::SP_422_12_LE) shift = 2;
                else if(pfId == PixelMemLayout::SP_422_12_BE) { shift = 2; be = true; }
                uint16_t v0 = static_cast<uint16_t>(w0 << shift);
                uint16_t v1 = static_cast<uint16_t>(w1 << shift);
                int chromaW = iw / 2;
                {
                        uint8_t *buf = static_cast<uint8_t *>(img.data(0));
                        size_t stride = img.lineStride(0);
                        uint8_t pat0[2], pat1[2];
                        sdiPathStore16(pat0, v0, be);
                        sdiPathStore16(pat1, v1, be);
                        for(int y = 0; y < ih; y++)
                                sdiPathRepeatBlock(buf + y * stride,
                                                   static_cast<size_t>(iw) * 2,
                                                   (y & 1) ? pat0 : pat1, 2);
                }
                {
                        uint8_t *buf = static_cast<uint8_t *>(img.data(1));
                        size_t stride = img.lineStride(1);
                        uint8_t blockE[4], blockO[4];
                        sdiPathStore16(blockE + 0, v0, be);
                        sdiPathStore16(blockE + 2, v0, be);
                        sdiPathStore16(blockO + 0, v1, be);
                        sdiPathStore16(blockO + 2, v1, be);
                        for(int y = 0; y < ih; y++)
                                sdiPathRepeatBlock(buf + y * stride,
                                                   static_cast<size_t>(chromaW) * 4,
                                                   (y & 1) ? blockO : blockE, 4);
                }
                return;
        }

        // ---- Fallback: approximate visual via PaintEngine ----
        if(!img.pixelFormat().hasPaintEngine()) return;
        PaintEngine pe = img.createPaintEngine();
        Color evenColor, oddColor;
        if(isEQ) {
                evenColor = Color::srgb(1.0f, 0.02f, 1.0f);
                oddColor  = Color::srgb(0.44f, 0.96f, 0.37f);
        } else {
                evenColor = Color::srgb(0.24f, 0.24f, 0.24f);
                oddColor  = Color::srgb(0.0f, 0.86f, 0.0f);
        }
        auto evenPixel = pe.createPixel(evenColor);
        auto oddPixel  = pe.createPixel(oddColor);
        for(int y = 0; y < ih; y++)
                pe.fillRect((y & 1) ? oddPixel : evenPixel,
                            Rect<int32_t>(0, y, iw, 1));
}

PROMEKI_NAMESPACE_END
