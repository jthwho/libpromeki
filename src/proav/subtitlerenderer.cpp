/**
 * @file      subtitlerenderer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/color.h>
#include <promeki/enums_subtitle.h>
#include <promeki/error.h>
#include <promeki/fastfont.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/paintengine.h>
#include <promeki/pixelformat.h>
#include <promeki/rect.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/subtitlerenderer.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

SubtitleRenderer::SubtitleRenderer() = default;

SubtitleRenderer::~SubtitleRenderer() = default;

// ============================================================================
// Configuration
// ============================================================================

void SubtitleRenderer::setFontFilename(const String &val) {
        if (_fontFilename == val) return;
        _fontFilename = val;
        _fontDirty = true;
}

void SubtitleRenderer::setFontSize(int val) {
        if (_fontSize == val) return;
        _fontSize = val;
        _fontDirty = true;
}

void SubtitleRenderer::setDefaultForeground(const Color &c) { _defaultFg = c; }
void SubtitleRenderer::setDefaultBackground(const Color &c) { _defaultBg = c; }
void SubtitleRenderer::setDrawBackground(bool v) { _drawBackground = v; }

void SubtitleRenderer::setMargin(int v) {
        if (v < 0) v = 0;
        _margin = v;
}

void SubtitleRenderer::setAnchorOverride(const SubtitleAnchor &v) { _anchorOverride = v; }

void SubtitleRenderer::setTopReserved(int lines) { _topReserved = (lines > 0) ? lines : 0; }
void SubtitleRenderer::setBottomReserved(int lines) { _bottomReserved = (lines > 0) ? lines : 0; }

// ============================================================================
// Helpers
// ============================================================================

SubtitleAnchor SubtitleRenderer::effectiveAnchor(const SubtitleAnchor &cueAnchor) const {
        // Precedence:
        //  1. Renderer-side override, when not Default.
        //  2. Cue's own anchor, when not Default.
        //  3. BottomCenter (SubRip's de facto convention for captions).
        if (_anchorOverride != SubtitleAnchor::Default) return _anchorOverride;
        if (cueAnchor != SubtitleAnchor::Default) return cueAnchor;
        return SubtitleAnchor::BottomCenter;
}

FastFont::DrawStyle SubtitleRenderer::styleFor(const SubtitleSpan &span) const {
        FastFont::DrawStyle s;
        s.foreground = span.color().isValid() ? span.color() : _defaultFg;
        s.background = _defaultBg;
        s.bold = span.bold();
        s.italic = span.italic();
        s.underline = span.underline();
        return s;
}

void SubtitleRenderer::layoutSpans(const SubtitleSpan::List &spans, StyledLineList &lines) {
        lines.clear();
        // Always have at least one (possibly empty) output line so the
        // caller never has to special-case a no-content cue.
        lines.pushToBack(StyledRunList());

        for (size_t i = 0; i < spans.size(); ++i) {
                const SubtitleSpan &orig = spans[i];
                const String       &text = orig.text();
                const char         *p = text.cstr();
                const size_t        n = text.byteCount();

                // Split this span's text on '\n'.  Each pre-'\n' chunk
                // stays in the current line; a '\n' opens a new line
                // and the post-'\n' tail (if any) starts the new line
                // with the same style.
                size_t start = 0;
                for (size_t pos = 0; pos < n; ++pos) {
                        if (p[pos] != '\n') continue;
                        if (pos > start) {
                                SubtitleSpan part = orig;
                                part.setText(String::fromUtf8(p + start, pos - start));
                                StyledRun run;
                                run.span = part;
                                lines[lines.size() - 1].pushToBack(run);
                        }
                        lines.pushToBack(StyledRunList());
                        start = pos + 1;
                }
                if (start < n) {
                        SubtitleSpan part = orig;
                        part.setText(String::fromUtf8(p + start, n - start));
                        StyledRun run;
                        run.span = part;
                        lines[lines.size() - 1].pushToBack(run);
                }
                // A span with empty text but trailing-style intent
                // (rare) doesn't need a placeholder run; the layout
                // pass below ignores zero-width runs anyway.
        }

        // Measure each run's width with its style applied.  Done in a
        // second pass so the FastFont is shared across all runs (the
        // alternative — measure inline above — needs the font, which
        // we don't construct until render() runs).
}

void SubtitleRenderer::computePosition(const SubtitleAnchor &anchor, const Rect2Di32 &bounds, int maxLineWidth,
                                       int totalHeight, int ascender, int &outX, int &outBaselineY) const {
        const int left = bounds.x();
        const int right = bounds.x() + bounds.width();
        const int top = bounds.y();
        const int bottom = bounds.y() + bounds.height();
        // Bounded margin: don't push past the side margins.
        const int marginX = _margin;
        const int marginY = _margin;

        int x = left + marginX;
        int baselineY = bottom - marginY - totalHeight + ascender;

        const int v = anchor.value();
        // Horizontal placement:
        if (v == SubtitleAnchor::BottomLeft.value() || v == SubtitleAnchor::MiddleLeft.value()
            || v == SubtitleAnchor::TopLeft.value()) {
                x = left + marginX;
        } else if (v == SubtitleAnchor::BottomRight.value() || v == SubtitleAnchor::MiddleRight.value()
                   || v == SubtitleAnchor::TopRight.value()) {
                x = right - marginX - maxLineWidth;
        } else {
                // Centered horizontal placement.
                x = left + (bounds.width() - maxLineWidth) / 2;
        }

        // Vertical placement:
        if (v == SubtitleAnchor::TopLeft.value() || v == SubtitleAnchor::TopCenter.value()
            || v == SubtitleAnchor::TopRight.value()) {
                baselineY = top + marginY + ascender;
        } else if (v == SubtitleAnchor::MiddleLeft.value() || v == SubtitleAnchor::MiddleCenter.value()
                   || v == SubtitleAnchor::MiddleRight.value()) {
                baselineY = top + (bounds.height() - totalHeight) / 2 + ascender;
        }
        // Bottom-* keeps the default baselineY = bottom - margin - totalHeight + ascender.

        outX = x;
        outBaselineY = baselineY;
}

// ============================================================================
// Render
// ============================================================================

Error SubtitleRenderer::render(const Subtitle &subtitle, UncompressedVideoPayload &target) {
        if (!target.isValid()) return Error::InvalidArgument;
        const PixelFormat &pf = target.desc().pixelFormat();
        if (!pf.hasPaintEngine()) return Error::NotSupported;

        const SubtitleSpan::List &spans = subtitle.spans();
        if (spans.isEmpty()) return Error::Ok;
        bool anyText = false;
        for (size_t i = 0; i < spans.size(); ++i) {
                if (!spans[i].text().isEmpty()) {
                        anyText = true;
                        break;
                }
        }
        if (!anyText) return Error::Ok;

        const int imgW = static_cast<int>(target.desc().size().width());
        const int imgH = static_cast<int>(target.desc().size().height());
        if (imgW <= 0 || imgH <= 0) return Error::Ok;

        // Resolve the effective font size and lazy-create the FastFont.
        int effectiveSize = _fontSize;
        if (effectiveSize <= 0) {
                // Auto: ~3% of image height, reference 30 px at 1080.
                effectiveSize = (imgH * 30 + 540) / 1080;
                if (effectiveSize < 8) effectiveSize = 8;
        }
        PaintEngine pe = target.createPaintEngine();
        if (_font.isNull()) {
                _font = FastFont::UPtr::create(pe);
                _fontDirty = true;
        } else {
                _font->setPaintEngine(pe);
        }
        if (_fontDirty || effectiveSize != _effectiveFontSize) {
                _font->setFontFilename(_fontFilename);
                _font->setFontSize(effectiveSize);
                _font->setForegroundColor(_defaultFg);
                _font->setBackgroundColor(_defaultBg);
                _effectiveFontSize = effectiveSize;
                _fontDirty = false;
        }

        const int ascender = _font->ascender();
        const int lineHeight = _font->lineHeight();
        if (lineHeight <= 0 || ascender <= 0) return Error::FontUnavailable;

        // Phase 1 — split spans on '\n' into per-line lists.
        StyledLineList lines;
        layoutSpans(spans, lines);

        // Phase 2 — measure each run with its style applied; trim
        // empty trailing lines so a cue with a stray "\n" at the end
        // doesn't reserve an extra blank row.
        int       maxLineWidth = 0;
        List<int> lineWidths;
        for (size_t li = 0; li < lines.size(); ++li) {
                auto &line = lines[li];
                int   lw = 0;
                for (size_t ri = 0; ri < line.size(); ++ri) {
                        StyledRun &run = line[ri];
                        run.width = _font->measureText(run.span.text(), styleFor(run.span));
                        lw += run.width;
                }
                lineWidths.pushToBack(lw);
                if (lw > maxLineWidth) maxLineWidth = lw;
        }
        while (!lines.isEmpty() && lines[lines.size() - 1].isEmpty()) {
                lines.remove(lines.size() - 1);
                lineWidths.remove(lineWidths.size() - 1);
        }
        const int nLines = static_cast<int>(lines.size());
        if (nLines == 0 || maxLineWidth == 0) return Error::Ok;

        const int lineSpacing = lineHeight / 4;
        const int totalHeight = nLines * lineHeight + (nLines > 1 ? (nLines - 1) * lineSpacing : 0);

        // Determine the bounding region.  If the cue has a valid
        // region hint, use it (clamped to the frame).  Otherwise use
        // the full frame minus reserved top / bottom bands.
        Rect2Di32 bounds;
        if (subtitle.region().isValid()) {
                int x = subtitle.region().x();
                int y = subtitle.region().y();
                int w = subtitle.region().width();
                int h = subtitle.region().height();
                if (x < 0) {
                        w += x;
                        x = 0;
                }
                if (y < 0) {
                        h += y;
                        y = 0;
                }
                if (x + w > imgW) w = imgW - x;
                if (y + h > imgH) h = imgH - y;
                if (w > 0 && h > 0) bounds = Rect2Di32(x, y, w, h);
        }
        if (!bounds.isValid()) {
                const int top = _topReserved;
                const int bot = imgH - _bottomReserved;
                if (bot - top > 0) {
                        bounds = Rect2Di32(0, top, imgW, bot - top);
                } else {
                        bounds = Rect2Di32(0, 0, imgW, imgH);
                }
        }

        int boxX = 0;
        int firstBaselineY = 0;
        computePosition(effectiveAnchor(subtitle.anchor()), bounds, maxLineWidth, totalHeight, ascender,
                        boxX, firstBaselineY);

        // Optional background rectangle behind the cue (single rect
        // covering all lines, padded by a quarter line-height).  Per-
        // span colour overrides only affect text — not bg.
        if (_drawBackground && _defaultBg.isValid()) {
                PaintEngine::Pixel bgPixel = pe.createPixel(_defaultBg);
                const int          pad = lineHeight / 4;
                Rect<int32_t>      bgRect(boxX - pad, firstBaselineY - ascender - pad, maxLineWidth + pad * 2,
                                          totalHeight + pad * 2);
                pe.fillRect(bgPixel, bgRect);
        }

        // Phase 3 — draw lines top-to-bottom, each centered inside
        // the bounding box if the anchor is a centre variant; left-
        // / right-anchored cues hug the box edge directly.
        const SubtitleAnchor eAnchor = effectiveAnchor(subtitle.anchor());
        const int            anchorV = eAnchor.value();
        const bool           leftAligned = (anchorV == SubtitleAnchor::BottomLeft.value()
                                  || anchorV == SubtitleAnchor::MiddleLeft.value()
                                  || anchorV == SubtitleAnchor::TopLeft.value());
        const bool           rightAligned = (anchorV == SubtitleAnchor::BottomRight.value()
                                    || anchorV == SubtitleAnchor::MiddleRight.value()
                                    || anchorV == SubtitleAnchor::TopRight.value());

        int cursorY = firstBaselineY;
        for (int li = 0; li < nLines; ++li) {
                const auto &line = lines[static_cast<size_t>(li)];
                const int   lw = lineWidths[static_cast<size_t>(li)];
                int         lineX = boxX;
                if (leftAligned) {
                        lineX = boxX;
                } else if (rightAligned) {
                        lineX = boxX + (maxLineWidth - lw);
                } else {
                        lineX = boxX + (maxLineWidth - lw) / 2;
                }

                int penX = lineX;
                for (size_t ri = 0; ri < line.size(); ++ri) {
                        const StyledRun &run = line[ri];
                        if (run.span.text().isEmpty()) continue;
                        // Per-span background paint: if the span carries
                        // an explicit backgroundColor, draw a rectangle
                        // covering this run's pixel range before the
                        // glyph blits.  Lands on top of the cue's full
                        // bg rectangle so it overrides for just this
                        // run's pixels — useful for highlighted speakers
                        // or CEA-708 SetPenColor bg overrides.
                        //
                        // Opacity handling: @c Transparent skips the bg
                        // paint entirely (and similarly the glyph blit
                        // for fg).  @c Translucent / @c Flash still
                        // paint as opaque for now — full alpha-blend
                        // support is a paint-engine enhancement.
                        const bool bgTransparent =
                                run.span.backgroundOpacity() == SubtitleOpacity::Transparent;
                        const bool fgTransparent =
                                run.span.foregroundOpacity() == SubtitleOpacity::Transparent;
                        if (run.span.backgroundColor().isValid() && !bgTransparent) {
                                const PaintEngine::Pixel spanBg =
                                        pe.createPixel(run.span.backgroundColor());
                                Rect<int32_t> rect(penX, cursorY - ascender, run.width, lineHeight);
                                pe.fillRect(spanBg, rect);
                        }
                        if (!fgTransparent) {
                                _font->drawText(run.span.text(), penX, cursorY, styleFor(run.span));
                        }
                        penX += run.width;
                }
                cursorY += lineHeight + lineSpacing;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
