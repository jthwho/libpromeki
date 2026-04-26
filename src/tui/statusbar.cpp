/**
 * @file      statusbar.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/statusbar.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/tuisubsystem.h>
#include <promeki/timerevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiStatusBar::TuiStatusBar(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(NoFocus);
}

TuiStatusBar::~TuiStatusBar() = default;

void TuiStatusBar::showMessage(const String &message, int timeoutMs) {
        _message = message;
        if (_messageTimerId >= 0) {
                stopTimer(_messageTimerId);
                _messageTimerId = -1;
        }
        if (timeoutMs > 0) {
                _messageTimerId = startTimer(timeoutMs, true);
        }
        update();
}

void TuiStatusBar::clearMessage() {
        _message.clear();
        if (_messageTimerId >= 0) {
                stopTimer(_messageTimerId);
                _messageTimerId = -1;
        }
        update();
}

void TuiStatusBar::setPermanentMessage(const String &message) {
        _permanentMessage = message;
        update();
}

Size2Di32 TuiStatusBar::sizeHint() const {
        return Size2Di32(40, 1);
}

void TuiStatusBar::paintEvent(PaintEvent *) {
        TuiSubsystem *app = TuiSubsystem::instance();
        if (!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32  clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        TuiStyle          s = pal.style(TuiPalette::StatusBarText, false, isEnabled())
                             .merged(pal.style(TuiPalette::StatusBar, false, isEnabled()));
        painter.setStyle(s);
        painter.fillRect(Rect2Di32(0, 0, width(), height()));

        // Display temporary message if set, otherwise permanent
        const String &display = _message.isEmpty() ? _permanentMessage : _message;
        if (!display.isEmpty()) {
                String text = display;
                if (static_cast<int>(text.length()) > width()) {
                        text = text.substr(0, width());
                }
                painter.drawText(0, 0, text);
        }
}

void TuiStatusBar::timerEvent(TimerEvent *e) {
        if (e->timerId() == _messageTimerId) {
                clearMessage();
                e->accept();
        }
}

PROMEKI_NAMESPACE_END
