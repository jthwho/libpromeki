/**
 * @file      subtitlerenderer.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/color.h>
#include <promeki/enums_subtitle.h>
#include <promeki/error.h>
#if PROMEKI_ENABLE_FREETYPE
#include <promeki/fastfont.h>
#endif
#include <promeki/namespace.h>
#include <promeki/rect.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class Subtitle;
class UncompressedVideoPayload;

/**
 * @brief Paints a @ref Subtitle onto an @ref UncompressedVideoPayload.
 * @ingroup paint
 *
 * Stateful renderer that owns a @ref FastFont and a layout policy
 * (font / size / colours / anchor override / reserved bands).  Each
 * @ref render call measures the cue's per-line, per-span geometry
 * once, optionally fills a background box, and then walks the spans
 * left-to-right using @c FastFont's per-call @ref FastFont::DrawStyle
 * override.  The multi-keyed glyph cache keeps mixed-style cues
 * cheap on every frame after the first.
 *
 * @par Inputs respected
 *
 *  - @ref Subtitle::spans — each span's @c bold / @c italic /
 *    @c underline flags and explicit @c color override route into
 *    @c FastFont's per-call style.
 *  - @ref Subtitle::anchor — 9-position layout anchor.  The renderer
 *    falls back to a configurable default (defaults to
 *    @c SubtitleAnchor::BottomCenter) when the cue's anchor is
 *    @c SubtitleAnchor::Default and the renderer was not explicitly
 *    overridden.
 *  - @ref Subtitle::region — when valid, the cue is anchored inside
 *    that pixel-space box instead of the full payload.  Invalid
 *    regions fall back to the whole frame.
 *
 * @par Inputs ignored (for now)
 *
 *  - @ref Subtitle::speaker — accessibility / voice attribution is
 *    not rendered as a visible label.  The future Phase 6
 *    `Cea708Overlay` work can pick this up; here we keep the
 *    bottom-line caption look that SubRip files actually carry.
 *
 * @par Background draw
 *
 * When @ref setDrawBackground is enabled, the renderer fills a
 * solid rectangle behind the cue using the configured background
 * colour.  Padding is one quarter line-height on every side.  Per-
 * span @c color overrides do not change the background — only the
 * text colour.
 *
 * @par Auto font sizing
 *
 * @ref setFontSize accepts @c 0 ("auto").  In that mode the
 * effective font size is computed at every @ref render call from
 * the target payload's height (~3% of the height, reference 1080
 * lines → 30 pt).
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Distinct renderers can be used in
 * parallel; concurrent access to a single instance must be
 * externally synchronised.
 *
 * @see Subtitle, FastFont, SubtitleBurnMediaIO
 */
class SubtitleRenderer {
        public:
                SubtitleRenderer();
                ~SubtitleRenderer();

                SubtitleRenderer(const SubtitleRenderer &) = delete;
                SubtitleRenderer &operator=(const SubtitleRenderer &) = delete;

                // ---- Configuration ------------------------------------------

                /// @brief Sets the TrueType font path.  Empty = bundled default.
                void setFontFilename(const String &val);

                /// @brief Sets the font size in pixels.  @c 0 = auto-scale
                ///        from the target image height.
                void setFontSize(int val);

                /// @brief Sets the default text colour (used by spans
                ///        whose @ref SubtitleSpan::color is invalid).
                void setDefaultForeground(const Color &c);

                /// @brief Sets the background colour used behind the
                ///        cue when @ref setDrawBackground is enabled.
                void setDefaultBackground(const Color &c);

                /// @brief Enables / disables the background fill behind
                ///        cue text.  Default: enabled.
                void setDrawBackground(bool v);

                /// @brief Sets the pixel margin from the bounding box
                ///        edge.  Default: 16.
                void setMargin(int v);

                /// @brief Overrides the cue's anchor.  Pass
                ///        @c SubtitleAnchor::Default to honour the
                ///        cue's own anchor (with @c BottomCenter as
                ///        the renderer-wide fallback for cues whose
                ///        anchor is also @c Default).
                void setAnchorOverride(const SubtitleAnchor &v);

                /// @brief Scan lines reserved at the top of the frame
                ///        (e.g. motion band, ANC data band).  Bottom /
                ///        Top-anchored cues are pushed clear.
                void setTopReserved(int lines);

                /// @brief Scan lines reserved at the bottom of the
                ///        frame.
                void setBottomReserved(int lines);

                // ---- Configuration accessors --------------------------------

                const String         &fontFilename() const { return _fontFilename; }
                int                   fontSize() const { return _fontSize; }
                const Color          &defaultForeground() const { return _defaultFg; }
                const Color          &defaultBackground() const { return _defaultBg; }
                bool                  drawBackground() const { return _drawBackground; }
                int                   margin() const { return _margin; }
                const SubtitleAnchor &anchorOverride() const { return _anchorOverride; }
                int                   topReserved() const { return _topReserved; }
                int                   bottomReserved() const { return _bottomReserved; }

                // ---- Rendering ----------------------------------------------

                /**
                 * @brief Paints @p subtitle onto @p target.
                 *
                 * @return @c Error::Ok on success.  Returns
                 *         @c Error::InvalidArgument when @p target is
                 *         invalid, @c Error::NotSupported when the
                 *         payload's pixel format has no paint engine,
                 *         @c Error::FontUnavailable when the configured
                 *         font failed to load.  Cues with no visible
                 *         content (no spans or all-empty span text)
                 *         return @c Error::Ok without modifying the
                 *         target.
                 */
                Error render(const Subtitle &subtitle, UncompressedVideoPayload &target);

        private:
                struct StyledRun {
                                SubtitleSpan span;
                                int          width = 0; ///< Measured in pixels with the run's style.
                };

                /// @brief A single display line is a sequence of styled runs.
                using StyledRunList = List<StyledRun>;
                /// @brief The full multi-line layout for a cue.
                using StyledLineList = List<StyledRunList>;

                /// @brief Resolves the cue's effective anchor through
                ///        the override + per-renderer fallback chain.
                SubtitleAnchor effectiveAnchor(const SubtitleAnchor &cueAnchor) const;

                /// @brief Decomposes @p spans (which may contain `\n`)
                ///        into a per-display-line list of styled runs.
                ///        Widths are filled in by the caller.
                void layoutSpans(const SubtitleSpan::List &spans, StyledLineList &lines);

                /// @brief Computes the top-left pixel of the cue's
                ///        bounding box given the anchor, line metrics,
                ///        and bounding region.
                void computePosition(const SubtitleAnchor &anchor, const Rect2Di32 &bounds, int maxLineWidth,
                                     int totalHeight, int ascender, int &outX, int &outBaselineY) const;

#if PROMEKI_ENABLE_FREETYPE
                /// @brief Builds a FastFont DrawStyle for @p span using
                ///        the renderer's defaults for missing colours.
                FastFont::DrawStyle styleFor(const SubtitleSpan &span) const;
#endif

                String         _fontFilename;
                int            _fontSize = 0; // 0 = auto.
                Color          _defaultFg = Color::White;
                Color          _defaultBg = Color::Black;
                bool           _drawBackground = true;
                int            _margin = 16;
                SubtitleAnchor _anchorOverride; // Default = honour cue's anchor.
                int            _topReserved = 0;
                int            _bottomReserved = 0;

                // Mutable state — updated from @ref render.  The glyph
                // renderer is only present when FreeType is compiled in;
                // without it @ref render returns @c Error::FontUnavailable.
#if PROMEKI_ENABLE_FREETYPE
                FastFont::UPtr _font;
#endif
                int            _effectiveFontSize = 0;
                bool           _fontDirty = true;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
