/**
 * @file      videotestpattern.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/imagedesc.h>

PROMEKI_NAMESPACE_BEGIN

class Image;

/**
 * @brief Standalone video test pattern generator.
 * @ingroup proav
 *
 * Generates video test patterns into Image objects. Supports dual-mode
 * operation: create a new Image with the pattern, or render into an
 * existing Image. Motion support via an offset parameter that advances
 * per frame.
 *
 * This class is not thread-safe. External synchronization is required
 * for concurrent access.
 *
 * @par Example
 * @code
 * VideoTestPattern gen;
 * gen.setPattern(VideoTestPattern::ColorBars);
 *
 * // Create a new image with the pattern
 * ImageDesc desc(1920, 1080, PixelDesc::RGB8_sRGB);
 * Image img = gen.create(desc);
 *
 * // Or render into an existing image
 * Image existing(desc);
 * gen.render(existing, 10.0);
 * @endcode
 */
class VideoTestPattern {
        public:
                /** @brief Video test pattern type. */
                enum Pattern {
                        ColorBars,      ///< @brief SMPTE 100% color bars.
                        ColorBars75,    ///< @brief SMPTE 75% color bars.
                        Ramp,           ///< @brief Luminance gradient ramp.
                        Grid,           ///< @brief White grid lines on black.
                        Crosshatch,     ///< @brief Diagonal crosshatch lines.
                        Checkerboard,   ///< @brief Alternating black/white squares.
                        SolidColor,     ///< @brief Solid fill with configured color.
                        White,          ///< @brief Solid white.
                        Black,          ///< @brief Solid black.
                        Noise,          ///< @brief Random pixel noise.
                        ZonePlate       ///< @brief Circular zone plate.
                };

                /** @brief Constructs a VideoTestPattern with default settings (ColorBars). */
                VideoTestPattern() = default;

                /** @brief Destructor. */
                ~VideoTestPattern() = default;

                VideoTestPattern(const VideoTestPattern &) = delete;
                VideoTestPattern &operator=(const VideoTestPattern &) = delete;

                /** @brief Returns the current pattern type. */
                Pattern pattern() const { return _pattern; }

                /**
                 * @brief Sets the pattern type.
                 *
                 * @par Example
                 * @code
                 * gen.setPattern(VideoTestPattern::ColorBars);
                 * gen.setPattern(VideoTestPattern::ZonePlate);
                 * @endcode
                 */
                void setPattern(Pattern pattern) { _pattern = pattern; }

                /** @brief Returns the solid color red component (0-65535). */
                uint16_t solidColorR() const { return _solidR; }

                /** @brief Returns the solid color green component (0-65535). */
                uint16_t solidColorG() const { return _solidG; }

                /** @brief Returns the solid color blue component (0-65535). */
                uint16_t solidColorB() const { return _solidB; }

                /**
                 * @brief Sets the solid color (used when pattern is SolidColor).
                 * @param r Red component (0-65535).
                 * @param g Green component (0-65535).
                 * @param b Blue component (0-65535).
                 *
                 * @par Example
                 * @code
                 * gen.setPattern(VideoTestPattern::SolidColor);
                 * gen.setSolidColor(32768, 0, 65535); // purple
                 * @endcode
                 */
                void setSolidColor(uint16_t r, uint16_t g, uint16_t b) {
                        _solidR = r;
                        _solidG = g;
                        _solidB = b;
                }

                /**
                 * @brief Creates a new Image and renders the pattern into it.
                 * @param desc Image descriptor specifying size and pixel format.
                 * @param motionOffset Horizontal motion offset in pixels.
                 * @return A new Image containing the rendered pattern.
                 *
                 * @par Example
                 * @code
                 * ImageDesc desc(1920, 1080, PixelDesc::RGB8_sRGB);
                 * Image frame = gen.create(desc, motionOffset);
                 * motionOffset += 2.0; // advance 2 pixels per frame
                 * @endcode
                 */
                Image create(const ImageDesc &desc, double motionOffset = 0.0) const;

                /**
                 * @brief Renders the pattern into an existing Image.
                 * @param img The target image (must be valid and allocated).
                 * @param motionOffset Horizontal motion offset in pixels.
                 *
                 * @par Example
                 * @code
                 * // Re-render into a pre-allocated image each frame
                 * gen.render(frame, motionOffset);
                 * motionOffset += 2.0;
                 * @endcode
                 */
                void render(Image &img, double motionOffset = 0.0) const;

                /**
                 * @brief Parses a pattern name string to a Pattern enum value.
                 * @param name Pattern name (lowercase, e.g. "colorbars", "zoneplate").
                 * @return Result containing the Pattern on success, or Error::Invalid.
                 *
                 * @par Example
                 * @code
                 * auto [pat, err] = VideoTestPattern::fromString("colorbars75");
                 * if(err.isOk()) gen.setPattern(pat);
                 * @endcode
                 */
                static Result<Pattern> fromString(const String &name);

                /**
                 * @brief Returns the name string for a Pattern enum value.
                 * @param pattern The pattern value.
                 * @return The pattern name string (lowercase).
                 *
                 * @par Example
                 * @code
                 * String name = VideoTestPattern::toString(VideoTestPattern::ZonePlate);
                 * // name == "zoneplate"
                 * @endcode
                 */
                static String toString(Pattern pattern);

        private:
                Pattern         _pattern = ColorBars;
                uint16_t        _solidR = 0;
                uint16_t        _solidG = 0;
                uint16_t        _solidB = 0;

                void renderColorBars(Image &img, double offset, bool full) const;
                void renderRamp(Image &img, double offset) const;
                void renderGrid(Image &img, double offset) const;
                void renderCrosshatch(Image &img, double offset) const;
                void renderCheckerboard(Image &img, double offset) const;
                void renderZonePlate(Image &img, double phase) const;
                void renderNoise(Image &img) const;
                void renderSolid(Image &img, uint16_t r, uint16_t g, uint16_t b) const;
};

PROMEKI_NAMESPACE_END
