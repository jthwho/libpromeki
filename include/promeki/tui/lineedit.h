/**
 * @file      tui/lineedit.h
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
 * @brief Single-line text input widget.
 * @ingroup tui_widgets
 *
 */
class TuiLineEdit : public TuiWidget {
        PROMEKI_OBJECT(TuiLineEdit, TuiWidget)
        public:
                TuiLineEdit(const String &text = String(), ObjectBase *parent = nullptr);
                ~TuiLineEdit() override;

                void setText(const String &text);
                const String &text() const { return _text; }

                void setPlaceholder(const String &text) { _placeholder = text; update(); }
                const String &placeholder() const { return _placeholder; }

                Size2Di32 sizeHint() const override;

                PROMEKI_SIGNAL(textChanged, const String &)
                PROMEKI_SIGNAL(returnPressed)

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void keyPressEvent(KeyEvent *e) override;
                void focusInEvent(Event *e) override;
                void focusOutEvent(Event *e) override;

        private:
                String  _text;
                String  _placeholder;
                int     _cursorPos = 0;
                int     _scrollOffset = 0;
};

PROMEKI_NAMESPACE_END
