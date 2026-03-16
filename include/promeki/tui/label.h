/**
 * @file      tui/label.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Text alignment for TUI labels.
 * @ingroup tui_widgets
 *
 */
enum TuiAlignment {
        AlignLeft,
        AlignCenter,
        AlignRight
};

/**
 * @brief Displays text (single or multi-line).
 */
class TuiLabel : public TuiWidget {
        PROMEKI_OBJECT(TuiLabel, TuiWidget)
        public:
                TuiLabel(const String &text = String(), ObjectBase *parent = nullptr);
                ~TuiLabel() override;

                /** @brief Sets the display text. */
                void setText(const String &text);

                /** @brief Returns the display text. */
                const String &text() const { return _text; }

                /** @brief Sets the text alignment. */
                void setAlignment(TuiAlignment align) { _alignment = align; update(); }

                /** @brief Returns the text alignment. */
                TuiAlignment alignment() const { return _alignment; }

                /** @brief Enables or disables word wrapping. */
                void setWordWrap(bool wrap) { _wordWrap = wrap; update(); }

                /** @brief Returns true if word wrapping is enabled. */
                bool wordWrap() const { return _wordWrap; }

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(TuiPaintEvent *e) override;

        private:
                String          _text;
                TuiAlignment    _alignment = AlignLeft;
                bool            _wordWrap = false;
};

PROMEKI_NAMESPACE_END
