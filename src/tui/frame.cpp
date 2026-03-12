/**
 * @file      frame.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/frame.h>
#include <promeki/tui/layout.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/application.h>

PROMEKI_NAMESPACE_BEGIN

TuiFrame::TuiFrame(const String &title, ObjectBase *parent)
        : TuiWidget(parent), _title(title) {
}

TuiFrame::~TuiFrame() = default;

void TuiFrame::setTitle(const String &title) {
        if(_title == title) return;
        _title = title;
        update();
}

Rect2Di32 TuiFrame::contentRect() const {
        return Rect2Di32(x() + 1, y() + 1,
                      std::max(0, width() - 2), std::max(0, height() - 2));
}

Size2Di32 TuiFrame::sizeHint() const {
        if(layout()) {
                int contentHeight = 0;
                int maxWidth = 0;
                const auto &widgets = layout()->widgets();
                int mt, mr, mb, ml;
                layout()->margins(mt, mr, mb, ml);
                for(size_t i = 0; i < widgets.size(); ++i) {
                        if(!widgets[i]) continue;
                        auto hint = widgets[i]->sizeHint();
                        contentHeight += hint.height();
                        maxWidth = std::max(maxWidth, hint.width());
                        if(i > 0) contentHeight += layout()->spacing();
                }
                contentHeight += mt + mb;
                maxWidth += ml + mr;
                // +2 for the border on each axis
                int w = std::max(maxWidth + 2, static_cast<int>(_title.length()) + 4);
                return Size2Di32(w, contentHeight + 2);
        }
        return Size2Di32(
                static_cast<int>(_title.length()) + 4, 4);
}

void TuiFrame::paintEvent(TuiPaintEvent *) {
        TuiApplication *app = TuiApplication::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        painter.setForeground(pal.color(TuiPalette::Mid, hasFocus(), isEnabled()));
        painter.setBackground(pal.color(TuiPalette::Window, hasFocus(), isEnabled()));
        painter.fillRect(Rect2Di32(0, 0, width(), height()));
        painter.drawRect(Rect2Di32(0, 0, width(), height()));
        painter.setForeground(pal.color(TuiPalette::WindowText, hasFocus(), isEnabled()));

        // Draw title
        if(!_title.isEmpty() && width() > 4) {
                String display = String(" ") + _title + " ";
                if(static_cast<int>(display.length()) > width() - 2) {
                        display = display.substr(0, width() - 2);
                }
                painter.drawText(1, 0, display);
        }
}

void TuiFrame::resizeEvent(TuiResizeEvent *) {
        if(layout()) {
                layout()->calculateLayout(contentRect());
        }
}

PROMEKI_NAMESPACE_END
