/**
 * @file      fontpainter.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

class FontPainter {
        public:
                FontPainter();
                ~FontPainter();

                void setPaintEngine(const PaintEngine &val) {
                        _paintEngine = val;
                        return;
                }

                void setFontFilename(const String &val) {
                        _fontFilename = val;
                        return;
                }

                bool drawText(const String &text, int x, int y, int pointSize = 12) const;

        private:
                PaintEngine     _paintEngine;
                String          _fontFilename;
};

PROMEKI_NAMESPACE_END

