/**
 * @file      font.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/font.h>

PROMEKI_NAMESPACE_BEGIN

Font::Font(const PaintEngine &pe) : _paintEngine(pe) {

}

Font::~Font() {

}

void Font::onStateChanged() {

}

void Font::setFontFilename(const String &val) {
        if(_fontFilename == val) return;
        _fontFilename = val;
        onStateChanged();
}

void Font::setFontSize(int val) {
        if(_fontSize == val) return;
        _fontSize = val;
        onStateChanged();
}

void Font::setForegroundColor(const Color &color) {
        if(_fg == color) return;
        _fg = color;
        onStateChanged();
}

void Font::setBackgroundColor(const Color &color) {
        if(_bg == color) return;
        _bg = color;
        onStateChanged();
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
        const bool formatChanged =
                (_paintEngine.pixelDesc() != pe.pixelDesc());
        _paintEngine = pe;
        if(formatChanged) onStateChanged();
}

void Font::setKerningEnabled(bool val) {
        if(_kerning == val) return;
        _kerning = val;
        onStateChanged();
}

bool Font::isValid() const {
        return !_fontFilename.isEmpty() && _fontSize > 0;
}

PROMEKI_NAMESPACE_END
