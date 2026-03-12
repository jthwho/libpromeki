/**
 * @file      statusbar.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Bottom-of-screen status line.
 */
class TuiStatusBar : public TuiWidget {
        PROMEKI_OBJECT(TuiStatusBar, TuiWidget)
        public:
                TuiStatusBar(ObjectBase *parent = nullptr);
                ~TuiStatusBar() override;

                void showMessage(const String &message, int timeoutMs = 0);
                void clearMessage();

                const String &message() const { return _message; }

                void setPermanentMessage(const String &message);
                const String &permanentMessage() const { return _permanentMessage; }

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void timerEvent(TimerEvent *e) override;

        private:
                String  _message;
                String  _permanentMessage;
                int     _messageTimerId = -1;
};

PROMEKI_NAMESPACE_END
