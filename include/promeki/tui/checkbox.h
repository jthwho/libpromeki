/**
 * @file      tui/checkbox.h
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
 * @brief Toggleable checkbox with text label.
 * @ingroup tui_widgets
 *
 * Displays as `[x] Text` when checked or `[ ] Text` when unchecked.
 * Supports keyboard activation (Enter/Space) and mouse click to toggle.
 * Emits the toggled signal when the checked state changes.
 */
class TuiCheckBox : public TuiWidget {
        PROMEKI_OBJECT(TuiCheckBox, TuiWidget)
        public:
                TuiCheckBox(const String &text = String(), ObjectBase *parent = nullptr);
                ~TuiCheckBox() override;

                void setText(const String &text);
                const String &text() const { return _text; }

                bool isChecked() const { return _checked; }
                void setChecked(bool checked);

                void toggle();

                Size2Di32 sizeHint() const override;

                PROMEKI_SIGNAL(toggled, bool)

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void keyEvent(KeyEvent *e) override;
                void mouseEvent(MouseEvent *e) override;
                void focusInEvent(Event *e) override;
                void focusOutEvent(Event *e) override;

        private:
                String  _text;
                bool    _checked = false;
};

PROMEKI_NAMESPACE_END
