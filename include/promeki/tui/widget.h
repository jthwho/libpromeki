/**
 * @file      tui/widget.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/widget.h>

PROMEKI_NAMESPACE_BEGIN

class TuiSubsystem;

/**
 * @brief TUI-specific widget base class.
 * @ingroup tui_core
 *
 * Thin subclass of the core Widget that adds TUI-specific
 * behavior: notifying TuiSubsystem when the widget is dirty
 * so that the screen can be repainted.
 *
 * All geometry, visibility, focus, size policy, layout, and
 * event dispatch functionality is inherited from Widget.
 *
 * @par Thread Safety
 * Thread-affine via @ref ObjectBase.  All TUI widgets must be created and
 * accessed from the thread that owns the TUI @ref EventLoop (typically the
 * thread that drives @ref TuiSubsystem).  Cross-thread interaction is
 * supported only through @ref ObjectBase signal/slot dispatch.
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
                 * Overrides Widget::update() to also notify TuiSubsystem
                 * that the screen needs repainting.
                 */
                void update() override;

        private:
                friend class TuiSubsystem;
};

PROMEKI_NAMESPACE_END
