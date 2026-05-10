/**
 * @file      motionband.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/array.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/color.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaioallocator.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Stand-alone scrolling-marker motion band for video frames.
 * @ingroup proav
 *
 * Stamps a thin horizontal band onto a video payload that contains a
 * bright marker which advances by one tick per frame and wraps every
 * @ref sequenceLength frames.  The band is intended to provide a
 * constant-rate visual reference so an observer can spot frame stutter,
 * drop, or repeat at a glance — a smooth scroll means the pipeline is
 * delivering every frame on time; a hitch, a backtrack, or a repeated
 * tick is immediate evidence of a timing fault.
 *
 * @par Visual layout
 * The band is rendered as a row of @ref sequenceLength adjacent slots,
 * one per cycle frame, each drawn as a bordered box:
 *
 * @verbatim
 *   [   ][   ][   ][XXX][   ][   ]
 * @endverbatim
 *
 * - The whole band is filled with @ref backgroundColor first.
 * - The active slot for the current cycle frame has its interior
 *   filled with @ref markerColor and runs flush with the top and
 *   bottom of the band.
 * - @c sequenceLength+1 vertical separators of @ref borderWidth
 *   pixels in @ref borderColor split the band into N equal-width
 *   slots — the leftmost and rightmost are anchored flush with the
 *   band's left/right edges; the internal ones are centred on each
 *   slot boundary.  No top or bottom horizontals are drawn.
 *
 * Slot @c i covers pixel range @c [i * w / N, (i + 1) * w / N) in the
 * band's width, so the marker advances exactly one slot per frame and
 * wraps once per cycle.
 *
 * @par Pre-render + memcpy strategy
 * The band is rendered once per cycle frame into the caller's target
 * pixel format (using an RGBA8 paint scratch + CSC for non-paintable
 * targets) and cached.  @ref apply then walks the destination payload
 * plane-by-plane and @c memcpy's the pre-rendered band rows into the
 * configured edge of the frame.  The hot path is therefore @c O(W * H)
 * memcpy bytes per frame with no per-pixel work, comparable to the
 * @ref ImageDataEncoder cost — typically well below 1% of the budget
 * at 1080p60.
 *
 * @par Sequence length
 * The cycle length is set explicitly via @ref setSequenceLength rather
 * than being derived from a clock source the band owns.  Callers driving
 * the band from a fractional frame rate (e.g. 29.97, 23.976) typically
 * round the rate's @c numerator / @c denominator to the nearest integer
 * — for 29.97 use @c 30, for 23.976 use @c 24, etc. — so the marker
 * traverses the image roughly once per wall-clock second while landing
 * on the same pixel position each cycle.
 *
 * @par Frame index
 * The cached frame to stamp is selected by
 * @code
 *   frameInCycle = ((frameCount + offset) % sequenceLength + sequenceLength) % sequenceLength
 * @endcode
 * with @ref offset added so a caller can phase-shift the marker
 * relative to its own frame counter.  The double-modulo guards against
 * negative @c frameCount values for callers that mix forward and
 * reverse playback.
 *
 * @par Cache invalidation
 * The cache is dropped on any of:
 * - @ref setEnabled toggling the band on or off,
 * - @ref setHeight, @ref setSequenceLength, or any of the visual
 *   colour / width setters,
 * - the @ref ImageDesc passed to @ref apply differing in width, height,
 *   or pixel format from the desc the cache was last populated against,
 * - @ref setAllocator switching to a new allocator.
 *
 * @ref setOffset and @ref setPosition do @em not invalidate the cache
 * — the offset is applied to the frame index at stamp time and the
 * position only affects where the stamp lands.
 *
 * @par Thread safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 *
 * @par Example
 * @code
 * MotionBand band;
 * band.setEnabled(true);
 * band.setSequenceLength(30);          // ~1 second at 29.97 fps
 *
 * for(uint64_t f = 0; ; ++f) {
 *     auto payload = UncompressedVideoPayload::allocate(desc);
 *     // ... render frame contents into payload ...
 *     payload.modify()->ensureExclusive();
 *     band.apply(*payload.modify(), f);
 * }
 * @endcode
 */
class MotionBand {
        public:
                /** @brief Where on the frame the band is stamped. */
                enum Position {
                        Bottom = 0, ///< Band sits flush with the bottom edge.
                        Top    = 1  ///< Band sits flush with the top edge.
                };

                /** @brief Default band height in scan lines (matches one ImageDataEncoder line block). */
                static constexpr int DefaultHeight = 16;

                /** @brief Constructs a disabled MotionBand with default visuals. */
                MotionBand();

                /** @brief Destructor. */
                ~MotionBand();

                MotionBand(const MotionBand &) = delete;
                MotionBand &operator=(const MotionBand &) = delete;

                /** @brief Returns @c true when the band is enabled. */
                bool enabled() const { return _enabled; }

                /**
                 * @brief Enables or disables the band.
                 *
                 * When disabled, @ref apply is a no-op and
                 * @ref reservedLines reports @c 0.  Toggling the flag
                 * invalidates the cache so a re-enable rebuilds against
                 * whatever descriptor the next @ref apply sees.
                 */
                void setEnabled(bool val);

                /** @brief Returns the band height in scan lines. */
                int height() const { return _height; }

                /**
                 * @brief Sets the band height in scan lines.
                 *
                 * Heights are rounded up internally to the deepest
                 * vertical-chroma-subsampling factor of the target
                 * pixel format at @ref apply time, so a configured 15
                 * with a 4:2:0 target effectively becomes 16.  Pass a
                 * value @c &le; 0 to fall back to @ref DefaultHeight.
                 */
                void setHeight(int lines);

                /** @brief Returns the configured cycle length in frames. */
                int sequenceLength() const { return _sequenceLength; }

                /**
                 * @brief Sets the cycle length in frames.
                 *
                 * @ref apply is a no-op until this is set to a positive
                 * value.  Typical callers pass an integer approximation
                 * of their frame rate (e.g. 30 for 29.97 fps) so the
                 * marker traverses the image once per wall-clock second.
                 */
                void setSequenceLength(int frames);

                /** @brief Returns the per-call frame-index offset. */
                int offset() const { return _offset; }

                /**
                 * @brief Sets a constant offset added to the @c frameCount
                 *        argument of @ref apply before the modulo.
                 *
                 * Lets a caller phase-shift the marker relative to its
                 * own frame counter — e.g. starting the marker mid-cycle.
                 * Does not invalidate the cache.
                 */
                void setOffset(int frames);

                /** @brief Returns the current band position preset. */
                Position position() const { return _position; }

                /**
                 * @brief Sets the band position preset.  Does not invalidate the cache.
                 */
                void setPosition(Position p);

                /** @brief Returns the box border thickness in pixels. */
                int borderWidth() const { return _borderWidth; }

                /** @brief Sets the box border thickness in pixels.  Minimum 1. */
                void setBorderWidth(int pixels);

                /** @brief Returns the band background fill color. */
                const Color &backgroundColor() const { return _backgroundColor; }

                /** @brief Sets the band background fill color. */
                void setBackgroundColor(const Color &c);

                /** @brief Returns the active-slot fill color. */
                const Color &markerColor() const { return _markerColor; }

                /** @brief Sets the active-slot fill color. */
                void setMarkerColor(const Color &c);

                /** @brief Returns the box border color. */
                const Color &borderColor() const { return _borderColor; }

                /** @brief Sets the box border color. */
                void setBorderColor(const Color &c);

                /**
                 * @brief Returns the number of scan lines the band
                 *        occupies, or @c 0 when disabled.
                 *
                 * Consumers that stack other content (e.g. burn-in text)
                 * read this to keep their content clear of the band.
                 * Always reports the raw configured @ref height — the
                 * vertical-subsampling round-up only matters for the
                 * stamp pass and is invisible to callers because the
                 * effective stamp never exceeds @c height + 1.
                 */
                int reservedLines() const { return _enabled ? _height : 0; }

                /**
                 * @brief Stamps the cached band frame for
                 *        @c (frameCount + offset) % sequenceLength
                 *        into @p inout.
                 *
                 * @p inout must be valid, allocated, and exclusively
                 * owned by the caller (call @c ensureExclusive first).
                 * The band cache is built lazily on the first call for
                 * a given descriptor; subsequent calls reuse the cache
                 * until the descriptor or visual configuration changes.
                 *
                 * @return @c Error::Ok on success or when the band is
                 *         disabled.  @c Error::InvalidArgument when the
                 *         payload is null.  @c Error::NotSupported when
                 *         the band height exceeds the image height or
                 *         the band cannot be built in the target format.
                 */
                Error apply(UncompressedVideoPayload &inout, uint64_t frameCount) const;

                /**
                 * @brief Installs a custom @ref MediaIOAllocator for
                 *        the cached band frames.
                 *
                 * Mirrors @ref VideoTestPattern::setAllocator — pass an
                 * allocator that routes through @ref MemSpace::SystemCow
                 * to keep the cached frames behind a CoW boundary.
                 * Switching to a new allocator drops the cache; pass a
                 * null Ptr to revert to @ref MediaIOAllocator::defaultAllocator.
                 */
                void setAllocator(MediaIOAllocator::Ptr allocator);

                /** @brief Returns the currently-installed allocator (or null if default). */
                MediaIOAllocator::Ptr allocator() const { return _allocator; }

                /**
                 * @brief Drops every cached band frame.
                 *
                 * Called automatically on configuration changes.  Exposed
                 * for callers that want to force a rebuild without
                 * touching configuration.
                 */
                void invalidateCache() const;

        private:
                bool     _enabled = false;
                int      _height = DefaultHeight;
                int      _sequenceLength = 0;
                int      _offset = 0;
                Position _position = Position::Bottom;
                int      _borderWidth = 4;
                Color    _backgroundColor;
                Color    _markerColor;
                Color    _borderColor;

                MediaIOAllocator::Ptr _allocator;

                // Cache: one slot per cycle frame, lazily filled on
                // first request.  All slots are sized to the same
                // (imageWidth x effectiveHeight) and rendered in the
                // descriptor's pixel format so apply() is just a
                // plane-by-plane memcpy.
                mutable List<UncompressedVideoPayload::Ptr> _cache;
                mutable size_t                              _cacheImgWidth = 0;
                mutable size_t                              _cacheImgHeight = 0;
                mutable int                                 _cachePixelFormatId = 0;
                mutable int                                 _cacheEffectiveHeight = 0;

                /**
                 * @brief Returns the band's effective pixel height for
                 *        @p target, rounded up to the deepest vertical
                 *        chroma subsampling factor.
                 */
                int effectiveBandHeight(const ImageDesc &target) const;

                /**
                 * @brief Lazily builds (or returns) the cached band
                 *        frame for cycle index @p frameInCycle.
                 */
                UncompressedVideoPayload::Ptr cachedFrame(int frameInCycle, const ImageDesc &target) const;

                /**
                 * @brief Renders the band frame for cycle index
                 *        @p frameInCycle into @p out.
                 */
                void renderFrame(UncompressedVideoPayload &out, int frameInCycle) const;

                /**
                 * @brief Returns an @c ImageDesc at @p target's width
                 *        and the band's effective height in the
                 *        RGBA8_sRGB scratch format used for paint when
                 *        the target itself has no PaintEngine.
                 */
                ImageDesc rgbScratchDesc(const ImageDesc &target, int bandHeight) const;

                /**
                 * @brief Returns an @c ImageDesc at @p target's width
                 *        and the band's effective height in @p target's
                 *        pixel format.
                 */
                ImageDesc bandDesc(const ImageDesc &target, int bandHeight) const;

                /**
                 * @brief Computes the destination Y of the band's top
                 *        edge in @p target.
                 */
                int bandTopY(const ImageDesc &target, int bandHeight) const;
};

PROMEKI_NAMESPACE_END
