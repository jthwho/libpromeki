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
#include <promeki/uncompressedvideopayload.h>
#include <promeki/timecode.h>
#include <promeki/enums.h>
#include <promeki/fastfont.h>

PROMEKI_NAMESPACE_BEGIN

class UncompressedVideoPayload;

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
 * Burn-in is a separate pass: callers render the background via
 * @c create(), then draw a text overlay via @c applyBurn() once the
 * text content has been determined.  This lets the caller assemble
 * upstream context (e.g. a @c Frame with metadata) before deciding
 * what to burn in.  The overlay text may contain @c '\n' to span
 * multiple lines; each line is centered horizontally within the
 * bounding box and the whole block is positioned according to
 * @c burnPosition().  Burn-in uses the @c FastFont glyph cache so
 * repeated draws of the same characters at the same size/color are a
 * memcpy per scanline with no per-pixel alpha blending — see
 * @ref fonts.  The image must be in a paintable pixel format
 * (@c PixelFormat::hasPaintEngine() == true); @c applyBurn() returns
 * @c Error::NotSupported on non-paintable formats.
 *
 * @par Example
 * @code
 * VideoTestPattern gen;
 * gen.setPattern(VideoPattern::ColorBars);
 *
 * // Static pattern, no burn: the first create() renders, subsequent
 * // calls return the cached image via shallow copy.
 * ImageDesc desc(1920, 1080, PixelFormat::RGB8_sRGB);
 * Image a = gen.create(desc);  // renders colorbars
 * Image b = gen.create(desc);  // returns cache
 *
 * // Add a burn-in line.  A font filename may optionally be set;
 * // without it the library's bundled default font is used.
 * gen.setBurnFontSize(48);
 * a.ensureExclusive();
 * gen.applyBurn(a, "01:00:00:00\nmediaplay");
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 */
class VideoTestPattern {
        public:
                /** @brief Video test pattern type (alias for @ref VideoPattern). */
                using Pattern = VideoPattern;

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
                 * gen.setPattern(VideoPattern::ColorBars);
                 * gen.setPattern(VideoPattern::ZonePlate);
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
                 * When @c false, @c applyBurn() is a no-op regardless
                 * of the text passed in.  The flag exists as a
                 * convenience gate for callers that drive a
                 * @c VideoTestPattern from a config and want a single
                 * place to turn the overlay on or off.
                 */
                void setBurnEnabled(bool val) { _burnEnabled = val; }

                /** @brief Returns the TrueType font file used for burn-in. */
                const String &burnFontFilename() const { return _burnFontFilename; }

                /**
                 * @brief Sets the TrueType font file used for burn-in.
                 * @param path Absolute or working-directory-relative font path.
                 */
                void setBurnFontFilename(const String &path);

                /**
                 * @brief Returns the configured burn font size in pixels.
                 *
                 * A value of @c 0 indicates automatic sizing — the
                 * effective pixel size is computed from the rendered
                 * image height (36 px at 1080 lines, scaling linearly).
                 */
                int burnFontSize() const { return _burnFontSize; }

                /**
                 * @brief Sets the burn font size in pixels.
                 * @param px Font size in pixels, or @c 0 to auto-scale
                 *           from the rendered image height using the
                 *           reference of 36 px at 1080 lines.
                 */
                void setBurnFontSize(int px);

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
                 * @brief Creates a new Image and renders the pattern into it.
                 *
                 * For static patterns (not @c Noise) called with
                 * @c motionOffset == 0, the background is rendered once
                 * and cached; subsequent calls with the same @c desc
                 * return a shallow copy of the cache.
                 *
                 * The @p currentTimecode is only used by the @c AvSync
                 * pattern to choose between the marker (white,
                 * @c tc.frame()==0) and non-marker (black) cached
                 * frames.  Burn-in is a separate pass — see
                 * @ref applyBurn.
                 *
                 * @param desc             Image descriptor specifying size and pixel format.
                 * @param motionOffset     Horizontal motion offset in pixels.
                 * @param currentTimecode  Per-frame timecode (used by @c AvSync only).
                 * @return A new payload containing the rendered pattern.
                 */
                SharedPtr<UncompressedVideoPayload, true, UncompressedVideoPayload>
                createPayload(const ImageDesc &desc,
                              double motionOffset = 0.0,
                              const Timecode &currentTimecode = Timecode()) const;

                /**
                 * @brief Draws a text overlay onto an existing payload.
                 *
                 * Splits @p burnText on @c '\n' and renders each line
                 * stacked under the burn font, centered horizontally
                 * within the bounding box and positioned according to
                 * @ref burnPosition.  When @ref burnEnabled is @c false
                 * or @p burnText is empty this is a no-op and returns
                 * @c Error::Ok.
                 */
                Error applyBurn(UncompressedVideoPayload &inout,
                                const String &burnText) const;

                /**
                 * @brief Renders the pattern into an existing payload.
                 * @param img          The target payload (must be valid and allocated).
                 * @param motionOffset Horizontal motion offset in pixels.
                 */
                void render(UncompressedVideoPayload &img, double motionOffset = 0.0) const;

        private:
                // Background pattern state
                VideoPattern    _pattern = VideoPattern::ColorBars;
                Color           _solidColor;

                // Burn-in config
                bool            _burnEnabled = false;
                String          _burnFontFilename;
                int             _burnFontSize = 36;
                // Effective font size actually passed to FastFont.
                // Equals _burnFontSize when that is > 0; when
                // _burnFontSize is 0 (auto) it is computed from the
                // rendered image height at renderBurn() time.  Tracked
                // separately so image-size changes can trigger a font
                // reconfigure via _burnFontConfigDirty.
                mutable int     _burnEffectiveFontSize = 36;
                Color           _burnTextColor = Color::White;
                Color           _burnBackgroundColor = Color::Black;
                bool            _burnDrawBackground = true;
                BurnPosition    _burnPosition = BurnPosition::BottomCenter;

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
                mutable SharedPtr<UncompressedVideoPayload, true, UncompressedVideoPayload>
                                _cachedPayloads[CacheSlotCount];
                mutable size_t  _cacheW = 0;
                mutable size_t  _cacheH = 0;
                mutable int     _cachePixelFormatId = 0;

                // Burn font — lazily constructed the first time burn
                // actually runs, because FastFont needs a PaintEngine
                // (and thus a pixel format) at construction time.
                mutable FastFont::UPtr _burnFont;
                mutable bool      _burnFontConfigDirty = true;

                bool isStaticPattern() const;

                /**
                 * @brief Looks up (or lazily builds) a cached payload slot.
                 *
                 * If the requested @p desc differs from the desc the
                 * cache was last populated against, every slot is
                 * dropped first.  The slot at @p slotIndex is then
                 * returned, allocating and handing a fresh
                 * @ref UncompressedVideoPayload::Ptr to @p build for
                 * population if it isn't already valid.  The builder
                 * may reassign the Ptr (e.g. replace the payload with
                 * the result of a CSC).
                 *
                 * @tparam Builder A callable @c void(UncompressedVideoPayload::Ptr &).
                 * @param slotIndex Slot index, @c 0 .. @c CacheSlotCount-1.
                 * @return The cached payload.
                 */
                template <typename Builder>
                SharedPtr<UncompressedVideoPayload, true, UncompressedVideoPayload>
                cachedPayload(int slotIndex,
                              const ImageDesc &desc,
                              Builder &&build) const {
                        if(_cacheW != desc.width()
                           || _cacheH != desc.height()
                           || _cachePixelFormatId != static_cast<int>(desc.pixelFormat().id())) {
                                invalidatePayloadCache();
                                _cacheW = desc.width();
                                _cacheH = desc.height();
                                _cachePixelFormatId =
                                        static_cast<int>(desc.pixelFormat().id());
                        }
                        auto &slot = _cachedPayloads[slotIndex];
                        if(!slot.isValid()) {
                                slot = UncompressedVideoPayload::allocate(desc);
                                if(slot.isValid()) build(slot);
                        }
                        return slot;
                }

                /** @brief Drops every slot from the payload cache. */
                void invalidatePayloadCache() const;

                void applyBurnFontConfig() const;
                void computeBurnPosition(int frameW, int frameH,
                                         int textW, int totalH, int ascender,
                                         int &x, int &y) const;

                /**
                 * @brief Returns an @c ImageDesc at @p target's size in
                 *        the RGB16_LE_sRGB format used as the internal
                 *        paintable scratch for non-paintable targets.
                 *
                 * Pattern rendering and burn-in both go through @c PaintEngine,
                 * which is only registered for RGB-style formats.  When the
                 * caller asks for a non-paintable target (e.g. any YUV/YCbCr
                 * layout), we render into RGB16 at the same size and convert
                 * to the target format via @c UncompressedVideoPayload::convert()
                 * (which pulls a cached @c CSCPipeline from the global registry).
                 */
                ImageDesc rgbScratchDesc(const ImageDesc &target) const;

                void renderColorBars(UncompressedVideoPayload &img, double offset, bool full) const;
                void renderRamp(UncompressedVideoPayload &img, double offset) const;
                void renderGrid(UncompressedVideoPayload &img, double offset) const;
                void renderCrosshatch(UncompressedVideoPayload &img, double offset) const;
                void renderCheckerboard(UncompressedVideoPayload &img, double offset) const;
                void renderZonePlate(UncompressedVideoPayload &img, double phase) const;
                void renderNoise(UncompressedVideoPayload &img) const;
                void renderSolid(UncompressedVideoPayload &img, const Color &color) const;
                void renderColorChecker(UncompressedVideoPayload &img) const;
                void renderSMPTE219(UncompressedVideoPayload &img) const;
                void renderMultiBurst(UncompressedVideoPayload &img) const;
                void renderLimitRange(UncompressedVideoPayload &img) const;
                void renderCircularZone(UncompressedVideoPayload &img, double phase) const;
                void renderAlignment(UncompressedVideoPayload &img) const;
                void renderSDIPathological(UncompressedVideoPayload &img, bool isEQ) const;
};

PROMEKI_NAMESPACE_END
