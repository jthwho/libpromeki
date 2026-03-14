/**
 * @file      label.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/label.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/application.h>

PROMEKI_NAMESPACE_BEGIN

TuiLabel::TuiLabel(const String &text, ObjectBase *parent)
        : TuiWidget(parent), _text(text) {
        setFocusPolicy(NoFocus);
}

TuiLabel::~TuiLabel() = default;

void TuiLabel::setText(const String &text) {
        if(_text == text) return;
        _text = text;
        update();
}

Size2Di32 TuiLabel::sizeHint() const {
        return Size2Di32(static_cast<int>(_text.length()), 1);
}

void TuiLabel::paintEvent(TuiPaintEvent *) {
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        TuiStyle s = pal.style(TuiPalette::WindowText, hasFocus(), isEnabled())
                        .merged(pal.style(TuiPalette::Window, hasFocus(), isEnabled()));
        painter.setStyle(s);
        painter.fillRect(Rect2Di32(0, 0, width(), height()));

        // Draw text
        if(!_text.isEmpty()) {
                int textLen = static_cast<int>(_text.length());
                int xoff = 0;
                switch(_alignment) {
                        case AlignCenter:
                                xoff = (width() - textLen) / 2;
                                if(xoff < 0) xoff = 0;
                                break;
                        case AlignRight:
                                xoff = width() - textLen;
                                if(xoff < 0) xoff = 0;
                                break;
                        case AlignLeft:
                        default:
                                xoff = 0;
                                break;
                }
                painter.drawText(xoff, 0, _text);
        }
}

PROMEKI_NAMESPACE_END
