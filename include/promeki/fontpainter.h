/*****************************************************************************
 * fontpainter.h
 * May 16, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

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

