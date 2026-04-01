/**
 * @file      tui/widget.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/widget.h>

PROMEKI_NAMESPACE_BEGIN

class TuiApplication;

// Backward-compatible type aliases so existing TUI code compiles
// unchanged.  The actual types now live in core/widget.h.
using TuiPaintEvent = PaintEvent;
using TuiResizeEvent = ResizeEvent;
using TuiFocusPolicy = FocusPolicy;
using TuiSizePolicy = SizePolicy;

/**
 * @brief TUI-specific widget base class.
 * @ingroup tui_core
 *
 * Thin subclass of the core Widget that adds TUI-specific
 * behavior: notifying TuiApplication when the widget is dirty
 * so that the screen can be repainted.
 *
 * All geometry, visibility, focus, size policy, layout, and
 * event dispatch functionality is inherited from Widget.
 */
class TuiWidget : public Widget {
        PROMEKI_OBJECT(TuiWidget, Widget)
        public:
                /**
                 * @brief Constructs a TuiWidget.
                 * @param parent Optional parent widget.
                 */
                TuiWidget(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~TuiWidget() override;

                /**
                 * @brief Marks the widget as needing a repaint.
                 *
                 * Overrides Widget::update() to also notify TuiApplication
                 * that the screen needs repainting.
                 */
                void update() override;

        private:
                friend class TuiApplication;
};

PROMEKI_NAMESPACE_END
