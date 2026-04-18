/**
 * @file      progressbar.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/progressbar.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/tuisubsystem.h>

PROMEKI_NAMESPACE_BEGIN

TuiProgressBar::TuiProgressBar(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(NoFocus);
}

TuiProgressBar::~TuiProgressBar() = default;

void TuiProgressBar::setValue(int value) {
        if(value < _min) value = _min;
        if(value > _max) value = _max;
        if(_value == value) return;
        _value = value;
        update();
}

void TuiProgressBar::setRange(int min, int max) {
        _min = min;
        _max = max;
        if(_value < _min) _value = _min;
        if(_value > _max) _value = _max;
        update();
}

Size2Di32 TuiProgressBar::sizeHint() const {
        return Size2Di32(20, 1);
}

void TuiProgressBar::paintEvent(TuiPaintEvent *) {
        TuiSubsystem *app = TuiSubsystem::instance();
        if(!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        int range = _max - _min;
        double fraction = (range > 0) ? static_cast<double>(_value - _min) / range : 0.0;
        int filledWidth = static_cast<int>(width() * fraction);

        const TuiPalette &pal = app->palette();
        bool focused = hasFocus();
        bool enabled = isEnabled();

        TuiStyle filledBg = pal.style(TuiPalette::ProgressFilled, focused, enabled);
        TuiStyle emptyBg = pal.style(TuiPalette::ProgressEmpty, focused, enabled);

        // Draw filled portion (use the bg color as both fg and bg for a solid block)
        Color filledColor = filledBg.background();
        painter.setStyle(TuiStyle(filledColor, filledColor));
        painter.fillRect(Rect2Di32(0, 0, filledWidth, height()));

        // Draw empty portion
        Color emptyColor = emptyBg.background();
        painter.setStyle(TuiStyle(emptyColor, emptyColor));
        painter.fillRect(Rect2Di32(filledWidth, 0, width() - filledWidth, height()));

        // Draw percentage text with palette-driven colors at the fill boundary
        if(width() >= 5) {
                String pct = String::number(static_cast<int>(fraction * 100)) + "%";
                int textX = (width() - static_cast<int>(pct.length())) / 2;
                TuiStyle filledText = pal.style(TuiPalette::ProgressFilledText, focused, enabled);
                TuiStyle emptyText = pal.style(TuiPalette::ProgressEmptyText, focused, enabled);
                for(size_t ci = 0; ci < pct.length(); ++ci) {
                        int cx = textX + static_cast<int>(ci);
                        if(cx < filledWidth) {
                                painter.setStyle(filledText.merged(TuiStyle::fromBackground(filledColor)));
                        } else {
                                painter.setStyle(emptyText.merged(TuiStyle::fromBackground(emptyColor)));
                        }
                        painter.drawChar(cx, 0, static_cast<char32_t>(pct.str()[ci]));
                }
        }
}

PROMEKI_NAMESPACE_END
