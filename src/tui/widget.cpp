/**
 * @file      widget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/widget.h>
#include <promeki/tui/tuisubsystem.h>

PROMEKI_NAMESPACE_BEGIN

TuiWidget::TuiWidget(ObjectBase *parent) : Widget(parent) {
}

TuiWidget::~TuiWidget() = default;

void TuiWidget::update() {
        Widget::update();
        TuiSubsystem *app = TuiSubsystem::instance();
        if(app) app->markNeedsRepaint();
}

PROMEKI_NAMESPACE_END
