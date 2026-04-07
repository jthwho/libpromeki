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
#include <promeki/color.h>
#include <promeki/imagedesc.h>
#include <promeki/image.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

class FastFont;

/**
 * @brief Standalone video test pattern generator.
 * @ingroup proav
 *
 * Generates video test patterns into Image objects and optionally burns
 * a text overlay (including a timecode line) on top of the pattern.
 * Supports dual-mode operation: create a new Image with the pattern, or
 * render into an existing Image. Motion support via an offset parameter
 * that advances per frame.
 *
 * @par Performance
 *
 * When the configured pattern is @b static (i.e. @c motionOffset == 0
 * and the pattern is not @c Noise), @c create() caches the rendered
 * background on the first call and reuses it on subsequent calls with
 * the same @c ImageDesc, pattern, and solid color.  When burn-in is
 * enabled the cached background is copied (via COW plane detach) and
 * the text is drawn onto the copy, leaving the cache untouched so the
 * next frame can reuse it again.  When burn-in is disabled @c create()
 * returns the cache by shallow copy with no pixel work at all.
 *
 * For dynamic patterns (@c Noise, or when @c motionOffset != 0) the
 * background is rendered fresh each call.
 *
 * This class is not thread-safe. External synchronization is required
 * for concurrent access.
 *
 * @par Burn-in
 *
 * When @c burnEnabled() is true, @c create() draws an optional text
 * block on top of the pattern.  The block can contain two lines:
 *
 * - A @b timecode line, rendered only when the caller supplies a valid
 *   @c Timecode via the @c create() overload that takes one.
 * - A @b custom text line, rendered whenever @c burnText() is
 *   non-empty.
 *
 * Both lines are optional and can be shown independently or together.
 * If neither is present the burn stage is a no-op.  Burn-in uses the
 * @c FastFont glyph cache so repeated draws of the same characters at
 * the same size/color are a memcpy per scanline with no per-pixel
 * alpha blending — see @ref fonts.
 *
 * @par Example
 * @code
 * VideoTestPattern gen;
 * gen.setPattern(VideoTestPattern::ColorBars);
 *
 * // Static pattern, no burn: the first create() renders, subsequent
 * // calls return the cached image via shallow copy.
 * ImageDesc desc(1920, 1080, PixelDesc::RGB8_sRGB);
 * Image a = gen.create(desc);  // renders colorbars
 * Image b = gen.create(desc);  // returns cache
 *
 * // Add a burn-in timecode line.
 * gen.setBurnFontFilename("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
 * gen.setBurnFontSize(48);
 * gen.setBurnText("mediaplay");
 * gen.setBurnEnabled(true);
 *
 * Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
 * Image c = gen.create(desc, 0.0, tc);  // cached bg + burned text
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
                        ZonePlate,      ///< @brief Circular zone plate.
                        AvSync          ///< @brief A/V sync marker: white on tc.frame()==0, black otherwise.
                };

                /** @brief Text-burn position preset. */
                enum BurnPosition {
                        BurnTopLeft,        ///< @brief Top-left corner.
                        BurnTopCenter,      ///< @brief Top center.
                        BurnTopRight,       ///< @brief Top-right corner.
                        BurnBottomLeft,     ///< @brief Bottom-left corner.
                        BurnBottomCenter,   ///< @brief Bottom center (default).
                        BurnBottomRight     ///< @brief Bottom-right corner.
                };

                /** @brief Constructs a VideoTestPattern with default settings (ColorBars). */
                VideoTestPattern();

                /** @brief Destructor. */
                ~VideoTestPattern();

                VideoTestPattern(const VideoTestPattern &) = delete;
                VideoTestPattern &operator=(const VideoTestPattern &) = delete;

                /** @brief Returns the current pattern type. */
                Pattern pattern() const { return _pattern; }

                /**
                 * @brief Sets the pattern type.  Invalidates the background cache.
                 *
                 * @par Example
                 * @code
                 * gen.setPattern(VideoTestPattern::ColorBars);
                 * gen.setPattern(VideoTestPattern::ZonePlate);
                 * @endcode
                 */
                void setPattern(Pattern pattern);

                /** @brief Returns the solid color. */
                const Color &solidColor() const { return _solidColor; }

                /**
                 * @brief Sets the solid color (used when pattern is SolidColor).
                 *        Invalidates the background cache.
                 * @param color The fill color.
                 */
                void setSolidColor(const Color &color);

                // ---- Burn-in configuration ----

                /** @brief Returns true if text burn-in is enabled. */
                bool burnEnabled() const { return _burnEnabled; }

                /**
                 * @brief Enables or disables text burn-in.
                 *
                 * When enabled, @c create() draws the configured burn text
                 * (and the per-frame timecode, if provided) on top of the
                 * background pattern.  A font filename must also be set
                 * before the burn will actually draw anything.
                 */
                void setBurnEnabled(bool val) { _burnEnabled = val; }

                /** @brief Returns the TrueType font file used for burn-in. */
                const String &burnFontFilename() const { return _burnFontFilename; }

                /**
                 * @brief Sets the TrueType font file used for burn-in.
                 * @param path Absolute or working-directory-relative font path.
                 */
                void setBurnFontFilename(const String &path);

                /** @brief Returns the burn font size in pixels. */
                int burnFontSize() const { return _burnFontSize; }

                /** @brief Sets the burn font size in pixels. */
                void setBurnFontSize(int px);

                /** @brief Returns the static burn text (the custom-text line). */
                const String &burnText() const { return _burnText; }

                /**
                 * @brief Sets the static burn text line.
                 *
                 * Drawn below the timecode line when both are present.
                 * Pass an empty string to remove the custom line.
                 */
                void setBurnText(const String &text) { _burnText = text; }

                /** @brief Returns the burn-in foreground color. */
                const Color &burnTextColor() const { return _burnTextColor; }

                /** @brief Sets the burn-in foreground color. */
                void setBurnTextColor(const Color &c);

                /** @brief Returns the burn-in background color. */
                const Color &burnBackgroundColor() const { return _burnBackgroundColor; }

                /** @brief Sets the burn-in background color. */
                void setBurnBackgroundColor(const Color &c);

                /** @brief Returns whether a background rectangle is drawn behind the burn text. */
                bool burnDrawBackground() const { return _burnDrawBackground; }

                /**
                 * @brief Enables/disables the background rectangle drawn behind the burn text.
                 *
                 * When enabled (default), a filled rect in the configured
                 * background color is drawn behind the text for legibility.
                 * When disabled, the FastFont character cells are drawn
                 * directly over the pattern — they still include the
                 * background color inside each cell, but without surrounding
                 * padding.
                 */
                void setBurnDrawBackground(bool val) { _burnDrawBackground = val; }

                /** @brief Returns the current burn-in position preset. */
                BurnPosition burnPosition() const { return _burnPosition; }

                /** @brief Sets the burn-in position preset. */
                void setBurnPosition(BurnPosition pos) { _burnPosition = pos; }

                /**
                 * @brief Parses a burn position name (lowercase) to enum.
                 * @param name One of: topleft, topcenter, topright,
                 *             bottomleft, bottomcenter, bottomright.
                 */
                static Result<BurnPosition> burnPositionFromString(const String &name);

                /** @brief Returns the lowercase name of a burn position. */
                static String burnPositionToString(BurnPosition pos);

                /**
                 * @brief Creates a new Image and renders the pattern into it.
                 *
                 * For static patterns (not @c Noise) called with
                 * @c motionOffset == 0, the background is rendered once
                 * and cached; subsequent calls with the same @c desc
                 * and solid-color settings either return the cache
                 * directly (burn disabled) or return a detached copy
                 * with the burn text drawn on top (burn enabled).
                 *
                 * When burn-in is enabled but the caller does not
                 * provide a @c Timecode, only the configured static
                 * burn text is rendered — use the three-argument
                 * overload to include a per-frame timecode line.
                 *
                 * @param desc         Image descriptor specifying size and pixel format.
                 * @param motionOffset Horizontal motion offset in pixels.
                 * @return A new Image containing the rendered pattern.
                 */
                Image create(const ImageDesc &desc, double motionOffset = 0.0) const;

                /**
                 * @brief Creates a new Image with the pattern and an optional per-frame timecode burn.
                 *
                 * Same caching behavior as the two-argument overload:
                 * the background for a static pattern is rendered once
                 * and copied on every subsequent call before the
                 * timecode line is burned in, so the cost of an
                 * always-changing timecode is one plane copy plus the
                 * FastFont text draw per frame.
                 *
                 * @param desc             Image descriptor.
                 * @param motionOffset     Horizontal motion offset in pixels.
                 * @param currentTimecode  Per-frame timecode.  If invalid,
                 *                         no timecode line is drawn (only
                 *                         the static burn text, if set).
                 * @return A new Image.
                 */
                Image create(const ImageDesc &desc, double motionOffset,
                             const Timecode &currentTimecode) const;

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
                // Background pattern state
                Pattern         _pattern = ColorBars;
                Color           _solidColor;

                // Burn-in config
                bool            _burnEnabled = false;
                String          _burnFontFilename;
                int             _burnFontSize = 36;
                String          _burnText;
                Color           _burnTextColor = Color::White;
                Color           _burnBackgroundColor = Color::Black;
                bool            _burnDrawBackground = true;
                BurnPosition    _burnPosition = BurnBottomCenter;

                // Generic image cache: a small fixed array of slots
                // shared by all patterns.  Slot meanings are local to
                // each pattern's branch in create() — for example
                // AvSync uses slot 0 for "white marker" and slot 1 for
                // "black non-marker", while a static pattern uses slot
                // 0 for its pre-rendered background.  Patterns are
                // mutually exclusive at any given time, so reusing the
                // same slot index across patterns is fine.
                //
                // Any change to _pattern, _solidColor, or the
                // requested ImageDesc dumps the entire cache; the next
                // create() call rebuilds whatever it needs.  Two
                // renderSolid() calls (the most expensive thing the
                // current patterns can do on a rebuild) are essentially
                // free, so a simpler dump-on-change policy is the right
                // tradeoff over per-key invalidation.
                static constexpr int CacheSlotCount = 2;
                mutable Image   _cachedImages[CacheSlotCount];
                mutable size_t  _cacheW = 0;
                mutable size_t  _cacheH = 0;
                mutable int     _cachePixelDescId = 0;

                // Burn font — lazily constructed the first time burn
                // actually runs, because FastFont needs a PaintEngine
                // (and thus a pixel format) at construction time.
                mutable FastFont *_burnFont = nullptr;
                mutable bool      _burnFontConfigDirty = true;

                bool isStaticPattern() const;

                /**
                 * @brief Looks up (or lazily builds) a cached image slot.
                 *
                 * If the requested @p desc differs from the desc the
                 * cache was last populated against, every slot is
                 * dropped first.  The slot at @p slotIndex is then
                 * returned, allocating and handing a fresh @c Image to
                 * @p build for population if it isn't already valid.
                 *
                 * @tparam Builder A callable @c void(Image &).
                 * @param slotIndex Slot index, @c 0 .. @c CacheSlotCount-1.
                 *                  Slot meanings are local to each
                 *                  caller — patterns are mutually
                 *                  exclusive so reusing slot 0 across
                 *                  pattern branches is fine.
                 * @return A reference into the cache.  Valid until the
                 *         next cache mutation; callers should
                 *         shallow-copy the result before returning it.
                 */
                template <typename Builder>
                const Image &cachedImage(int slotIndex,
                                         const ImageDesc &desc,
                                         Builder &&build) const {
                        if(_cacheW != desc.width()
                           || _cacheH != desc.height()
                           || _cachePixelDescId != static_cast<int>(desc.pixelDesc().id())) {
                                invalidateImageCache();
                                _cacheW = desc.width();
                                _cacheH = desc.height();
                                _cachePixelDescId =
                                        static_cast<int>(desc.pixelDesc().id());
                        }
                        Image &slot = _cachedImages[slotIndex];
                        if(!slot.isValid()) {
                                slot = Image(desc);
                                if(slot.isValid()) build(slot);
                        }
                        return slot;
                }

                /** @brief Drops every slot from the image cache. */
                void invalidateImageCache() const;

                void applyBurnFontConfig() const;
                void renderBurn(Image &img, const Timecode &tc) const;
                void computeBurnPosition(int frameW, int frameH,
                                         int textW, int totalH, int ascender,
                                         int &x, int &y) const;

                void renderColorBars(Image &img, double offset, bool full) const;
                void renderRamp(Image &img, double offset) const;
                void renderGrid(Image &img, double offset) const;
                void renderCrosshatch(Image &img, double offset) const;
                void renderCheckerboard(Image &img, double offset) const;
                void renderZonePlate(Image &img, double phase) const;
                void renderNoise(Image &img) const;
                void renderSolid(Image &img, const Color &color) const;
};

PROMEKI_NAMESPACE_END
