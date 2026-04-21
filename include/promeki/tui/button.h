/**
 * @file      tui/button.h
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
 * @brief Clickable button with text label.
 * @ingroup tui_widgets
 *
 */
class TuiButton : public TuiWidget {
        PROMEKI_OBJECT(TuiButton, TuiWidget)
        public:
                TuiButton(const String &text = String(), ObjectBase *parent = nullptr);
                ~TuiButton() override;

                /** @brief Sets the button text. */
                void setText(const String &text);

                /** @brief Returns the button text. */
                const String &text() const { return _text; }

                Size2Di32 sizeHint() const override;
                Size2Di32 minimumSizeHint() const override;

                /** @brief Emitted when the button is activated. */
                PROMEKI_SIGNAL(clicked)

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void keyPressEvent(KeyEvent *e) override;
                void mouseEvent(MouseEvent *e) override;
                void focusInEvent(Event *e) override;
                void focusOutEvent(Event *e) override;

        private:
                String _text;
                bool   _pressed = false;
};

PROMEKI_NAMESPACE_END
