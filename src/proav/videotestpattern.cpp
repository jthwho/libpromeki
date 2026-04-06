/**
 * @file      videotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/videotestpattern.h>
#include <promeki/image.h>
#include <promeki/paintengine.h>
#include <promeki/pixeldesc.h>
#include <promeki/random.h>

PROMEKI_NAMESPACE_BEGIN

Image VideoTestPattern::create(const ImageDesc &desc, double motionOffset) const {
        Image img(desc);
        render(img, motionOffset);
        return img;
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
