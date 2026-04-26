/**
 * @file      tui/frame.h
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
 * @brief Container widget with a border and optional title.
 * @ingroup tui_widgets
 *
 * @par Thread Safety
 * Thread-affine — see @ref TuiWidget.
 */
class TuiFrame : public TuiWidget {
        PROMEKI_OBJECT(TuiFrame, TuiWidget)
        public:
                TuiFrame(const String &title = String(), ObjectBase *parent = nullptr);
                ~TuiFrame() override;

                void setTitle(const String &title);
                const String &title() const { return _title; }

                /**
                 * @brief Returns the content area inside the border.
                 */
                Rect2Di32 contentRect() const;

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(PaintEvent *e) override;
                void resizeEvent(ResizeEvent *e) override;

        private:
                String _title;
};

PROMEKI_NAMESPACE_END
