/**
 * @file      videotestpattern.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/color.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaioallocator.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/timecode.h>
#include <promeki/enums_tpg.h>
#include <promeki/fastfont.h>
#include <promeki/motionband.h>

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
 * what to burn in.  The overlay text may contain newline characters to span
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
                 * @brief Returns the number of scan lines reserved at
                 *        the top of the image that the burn must not
                 *        overlap.
                 */
                int burnTopReserved() const { return _burnTopReserved; }

                /**
                 * @brief Reserves a band of scan lines at the top of
                 *        the image that the burn-in must not overlap.
                 *
                 * Callers that overlay other content at the very top
                 * of the frame (e.g. @ref ImageDataEncoder writing a
                 * VITC-style data band starting at line 0) use this
                 * to push the burn down below that band.  The reserved
                 * band shifts @c TopLeft / @c TopCenter / @c TopRight
                 * burns down so their first line begins at or below
                 * the reserved range, and clamps @c Center burns down
                 * the same way when the natural center would put the
                 * burn inside the reserved region.  @c Bottom* burns
                 * are unaffected.  Pass @c 0 (the default) to disable.
                 */
                void setBurnTopReserved(int lines) { _burnTopReserved = (lines > 0) ? lines : 0; }

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
                createPayload(const ImageDesc &desc, double motionOffset = 0.0,
                              const Timecode &currentTimecode = Timecode()) const;

                /**
                 * @brief Draws a text overlay onto an existing payload.
                 *
                 * Splits @p burnText on newline characters and renders each line
                 * stacked under the burn font, centered horizontally
                 * within the bounding box and positioned according to
                 * @ref burnPosition.  When @ref burnEnabled is @c false
                 * or @p burnText is empty this is a no-op and returns
                 * @c Error::Ok.
                 */
                Error applyBurn(UncompressedVideoPayload &inout, const String &burnText) const;

                /**
                 * @brief Renders the pattern into an existing payload.
                 * @param img          The target payload (must be valid and allocated).
                 * @param motionOffset Horizontal motion offset in pixels.
                 */
                void render(UncompressedVideoPayload &img, double motionOffset = 0.0) const;

                /**
                 * @brief Installs a custom @ref MediaIOAllocator for
                 *        the cached payload slots.
                 *
                 * Used by @ref TpgMediaIO to route the cached
                 * background through @ref MemSpace::SystemCow so per-
                 * frame burn-in detaches into a @c MAP_PRIVATE clone
                 * rather than triggering a full-frame @c memcpy.  Pass
                 * a null Ptr to revert to
                 * @ref MediaIOAllocator::defaultAllocator (heap-backed
                 * @c MemSpace::Default).
                 *
                 * Setting the allocator after a cache slot has been
                 * populated does not retro-actively re-allocate that
                 * slot; the next allocator that triggers
                 * @ref invalidatePayloadCache (typically a descriptor
                 * change) is what picks up the new policy.
                 */
                void setAllocator(MediaIOAllocator::Ptr allocator);

                /**
                 * @brief Returns the currently-installed allocator (or null if default).
                 */
                MediaIOAllocator::Ptr allocator() const { return _allocator; }

                // ---- Motion band ----

                /**
                 * @brief Returns the embedded @ref MotionBand for direct
                 *        configuration.
                 *
                 * The motion band is parallel to burn-in: callers
                 * configure it through this accessor and then invoke
                 * @ref applyMotionBand per frame between
                 * @ref createPayload and @ref applyBurn.  When enabled
                 * the band's @ref MotionBand::reservedLines value is
                 * automatically respected by @ref applyBurn so
                 * @c Bottom* burn positions are pushed clear of the
                 * band.
                 */
                MotionBand &motionBand() { return _motionBand; }

                /** @copydoc motionBand() */
                const MotionBand &motionBand() const { return _motionBand; }

                /**
                 * @brief Stamps the motion band frame for @p frameCount
                 *        into @p inout.
                 *
                 * Convenience wrapper around @ref MotionBand::apply.
                 * @p inout must be valid and exclusively owned by the
                 * caller (call @c ensureExclusive first).  Returns
                 * @c Error::Ok with no work when the band is disabled.
                 */
                Error applyMotionBand(UncompressedVideoPayload &inout, uint64_t frameCount) const {
                        return _motionBand.apply(inout, frameCount);
                }

        private:
                // Background pattern state
                VideoPattern _pattern = VideoPattern::ColorBars;
                Color        _solidColor;

                // Burn-in config
                bool   _burnEnabled = false;
                String _burnFontFilename;
                int    _burnFontSize = 36;
                // Effective font size actually passed to FastFont.
                // Equals _burnFontSize when that is > 0; when
                // _burnFontSize is 0 (auto) it is computed from the
                // rendered image height at renderBurn() time.  Tracked
                // separately so image-size changes can trigger a font
                // reconfigure via _burnFontConfigDirty.
                mutable int  _burnEffectiveFontSize = 36;
                Color        _burnTextColor = Color::White;
                Color        _burnBackgroundColor = Color::Black;
                bool         _burnDrawBackground = true;
                BurnPosition _burnPosition = BurnPosition::BottomCenter;
                int          _burnTopReserved = 0;

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
                mutable size_t _cacheW = 0;
                mutable size_t _cacheH = 0;
                mutable int    _cachePixelFormatId = 0;

                // Optional allocator override.  Null = use
                // MediaIOAllocator::defaultAllocator() so freshly-
                // constructed VideoTestPatterns behave exactly as
                // before the allocator framework existed.
                MediaIOAllocator::Ptr _allocator;

                // Embedded motion band — parallel to burn-in.  Owns
                // its own pre-rendered cache of N band frames in the
                // target pixel format and stamps the appropriate one
                // into the frame from applyMotionBand().
                MotionBand _motionBand;

                // Burn font — lazily constructed the first time burn
                // actually runs, because FastFont needs a PaintEngine
                // (and thus a pixel format) at construction time.
                mutable FastFont::UPtr _burnFont;
                mutable bool           _burnFontConfigDirty = true;

                // Cached @ref PaintEngine::Pixel for @ref _burnBackgroundColor.
                // applyBurn() runs a single fillRect per frame; building
                // that pixel via @c PaintEngine::createPixel(Color) goes
                // through a Color::convert into the target colour model
                // (sRGB→Rec.709 YCbCr Limited on NV12 / YUV outputs),
                // which is non-trivial.  We cache it and invalidate
                // whenever the configured bg colour changes (see
                // @ref setBurnBackgroundColor) or the target pixel format
                // changes (detected via @ref _cachedBgPixelFormat).
                mutable PaintEngine::Pixel _cachedBgPixel;
                mutable PixelFormat        _cachedBgPixelFormat;

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
                cachedPayload(int slotIndex, const ImageDesc &desc, Builder &&build) const {
                        if (_cacheW != desc.width() || _cacheH != desc.height() ||
                            _cachePixelFormatId != static_cast<int>(desc.pixelFormat().id())) {
                                invalidatePayloadCache();
                                _cacheW = desc.width();
                                _cacheH = desc.height();
                                _cachePixelFormatId = static_cast<int>(desc.pixelFormat().id());
                        }
                        auto &slot = _cachedPayloads[slotIndex];
                        if (!slot.isValid()) {
                                // Route allocation through the
                                // installed allocator (typically
                                // SystemCow when wired by TpgMediaIO).
                                // Null _allocator falls back to the
                                // process-wide default — no behaviour
                                // change for callers that never wire
                                // one up.
                                slot = _allocator.isValid() ? _allocator->allocateVideoPayload(desc)
                                                            : UncompressedVideoPayload::allocate(desc);
                                if (slot.isValid()) {
                                        build(slot);
                                        // Seal the cached payload so
                                        // subsequent ensureExclusive()
                                        // detaches a SystemCow-backed
                                        // buffer cheaply (MAP_PRIVATE
                                        // clone, not full-frame memcpy).
                                        // Default backends no-op.
                                        (void)slot->data().seal();
                                }
                        }
                        return slot;
                }

                /** @brief Drops every slot from the payload cache. */
                void invalidatePayloadCache() const;

                void applyBurnFontConfig() const;
                void computeBurnPosition(int frameW, int frameH, int textW, int totalH, int ascender, int &x,
                                         int &y) const;

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

#endif // PROMEKI_ENABLE_PROAV
