/**
 * @file      font.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/font.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // libpromeki ships a FiraCode monospace font through its compiled-in
        // resource filesystem (see Resource). Font subclasses that call
        // effectiveFilename() with an empty _fontFilename get this path
        // back. It is deliberately kept out of the public header so that
        // the exact bundled default is an internal implementation detail
        // that can be moved or renamed without touching the Font API.
        constexpr const char *kDefaultFontFilename = ":/.PROMEKI/fonts/FiraCodeNerdFontMono-Regular.ttf";

        // Default fallback chain consulted by effectiveFallbacks() when
        // the caller has not supplied an explicit list. Ordered from
        // best-matching coverage to broadest coverage:
        //   1. Sarasa Mono J — Hiragana / Katakana / full CJK Unified
        //      Ideographs, monospaced metrics that line up with the
        //      primary FiraCode face.
        //   2. Noto Sans Mono — broad non-CJK Unicode (Devanagari,
        //      Thai, Arabic, Hebrew, IPA, math, etc.) for everything
        //      else the primary face does not carry.
        constexpr const char *kDefaultFontFallbacks[] = {
                ":/.PROMEKI/fonts/SarasaMonoJ-Regular.ttf",
                ":/.PROMEKI/fonts/NotoSansMono-Regular.ttf",
        };

} // namespace

Font::Font(const PaintEngine &pe) : _paintEngine(pe) {}

Font::~Font() {}

void Font::onStateChanged() {}

void Font::onColorChanged() {
        // Default to the same heavy-handed invalidation as
        // onStateChanged so existing subclasses (BasicFont, etc.)
        // keep behaving correctly without overriding the new hook.
        onStateChanged();
}

void Font::setFontFilename(const String &val) {
        if (_fontFilename == val) return;
        _fontFilename = val;
        onStateChanged();
}

void Font::setFontFallbacks(const StringList &val) {
        if (_fontFallbacks == val) return;
        _fontFallbacks = val;
        onStateChanged();
}

void Font::setFontSize(int val) {
        if (_fontSize == val) return;
        _fontSize = val;
        onStateChanged();
}

void Font::setForegroundColor(const Color &color) {
        if (_fg == color) return;
        _fg = color;
        onColorChanged();
}

void Font::setBackgroundColor(const Color &color) {
        if (_bg == color) return;
        _bg = color;
        onColorChanged();
}

void Font::setPaintEngine(const PaintEngine &pe) {
        // Only trip the state-changed hook when something the font
        // subclasses actually care about has changed.  For FastFont
        // (and any other cache-heavy subclass) the glyph cache stores
        // pre-rendered cells in the target pixel format — the cache
        // stays valid across different PaintEngine *instances* as long
        // as the pixel format is the same.  Callers that rebind the
        // font to a fresh engine every frame (e.g. the TPG burn-in
        // path, which creates a new PaintEngine on each detached
        // image copy) would otherwise thrash the glyph cache on every
        // frame even though nothing observable has changed.
        const bool formatChanged = (_paintEngine.pixelFormat() != pe.pixelFormat());
        _paintEngine = pe;
        if (formatChanged) onStateChanged();
}

void Font::setKerningEnabled(bool val) {
        if (_kerning == val) return;
        _kerning = val;
        onStateChanged();
}

bool Font::isValid() const {
        // An empty _fontFilename is valid because effectiveFilename()
        // falls back to the bundled default, which is always present.
        // What we do require is a positive font size and a paint
        // engine bound to a real pixel format — without those the
        // font cannot actually render anything.
        return _fontSize > 0 && _paintEngine.pixelFormat().isValid();
}

String Font::effectiveFilename() const {
        return _fontFilename.isEmpty() ? String(kDefaultFontFilename) : _fontFilename;
}

StringList Font::effectiveFallbacks() const {
        if (!_fontFallbacks.isEmpty()) return _fontFallbacks;
        StringList out;
        for (const char *p : kDefaultFontFallbacks) out.pushToBack(String(p));
        return out;
}

PROMEKI_NAMESPACE_END
