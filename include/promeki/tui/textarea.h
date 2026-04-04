/**
 * @file      tui/textarea.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Multi-line text editing/display widget.
 * @ingroup tui_widgets
 *
 */
class TuiTextArea : public TuiWidget {
        PROMEKI_OBJECT(TuiTextArea, TuiWidget)
        public:
                TuiTextArea(ObjectBase *parent = nullptr);
                ~TuiTextArea() override;

                void setText(const String &text);
                String text() const;

                void setReadOnly(bool readOnly) { _readOnly = readOnly; }
                bool isReadOnly() const { return _readOnly; }

                void appendLine(const String &line);

                Size2Di32 sizeHint() const override;

                PROMEKI_SIGNAL(textChanged)

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void keyEvent(KeyEvent *e) override;

        private:
                StringList      _lines;
                int             _cursorRow = 0;
                int             _cursorCol = 0;
                int             _scrollRow = 0;
                int             _scrollCol = 0;
                bool            _readOnly = false;
};

PROMEKI_NAMESPACE_END
