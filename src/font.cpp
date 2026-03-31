/**
 * @file      font.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/font.h>

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
        const PixelFormat *oldPf = _paintEngine.pixelFormat();
        _paintEngine = pe;
        if(pe.pixelFormat() != oldPf) onStateChanged();
}

void Font::setKerningEnabled(bool val) {
        if(_kerning == val) return;
        _kerning = val;
        onStateChanged();
}

bool Font::isValid() const {
        return !_fontFilename.isEmpty() && _fontSize > 0 && _paintEngine.pixelFormat() != nullptr;
}

PROMEKI_NAMESPACE_END
