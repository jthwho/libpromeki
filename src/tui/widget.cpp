/**
 * @file      widget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/widget.h>
#include <promeki/tui/application.h>
#include <promeki/tui/layout.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

const Event::Type TuiPaintEvent::Paint = Event::registerType();
const Event::Type TuiResizeEvent::Resize = Event::registerType();

TuiWidget::TuiWidget(ObjectBase *parent) : ObjectBase(parent) {
}

TuiWidget::~TuiWidget() = default;

void TuiWidget::setGeometry(const Rect2Di32 &rect) {
        if(_geometry == rect) return;
        Size2Di32 oldSize = size();
        _geometry = rect;
        Size2Di32 newSize = size();
        if(oldSize.width() != newSize.width() || oldSize.height() != newSize.height()) {
                TuiResizeEvent e(newSize);
                resizeEvent(&e);
                resizedSignal.emit(newSize);
        }
        update();
}

void TuiWidget::show() {
        setVisible(true);
}

void TuiWidget::hide() {
        setVisible(false);
}

bool TuiWidget::isEffectivelyVisible() const {
        if(!_visible) return false;
        TuiWidget *pw = dynamic_cast<TuiWidget *>(parent());
        if(pw) return pw->isEffectivelyVisible();
        return true;
}

void TuiWidget::setVisible(bool visible) {
        if(_visible == visible) return;
        _visible = visible;
        visibilityChangedSignal.emit(visible);
        update();
}

void TuiWidget::setFocus() {
        if(_focusPolicy == NoFocus) return;
        _focused = true;
        Event e(Event::InvalidType);
        focusInEvent(&e);
        update();
}

void TuiWidget::update() {
        _dirty = true;
        TuiApplication *app = TuiApplication::instance();
        if(app) app->markNeedsRepaint();
}

void TuiWidget::setLayout(TuiLayout *layout) {
        _layout = layout;
}

Point2Di32 TuiWidget::mapToParent(const Point2Di32 &p) const {
        return Point2Di32(p.x() + _geometry.x(), p.y() + _geometry.y());
}

Point2Di32 TuiWidget::mapFromParent(const Point2Di32 &p) const {
        return Point2Di32(p.x() - _geometry.x(), p.y() - _geometry.y());
}

Point2Di32 TuiWidget::mapToGlobal(const Point2Di32 &p) const {
        Point2Di32 result = mapToParent(p);
        TuiWidget *pw = dynamic_cast<TuiWidget *>(parent());
        if(pw) return pw->mapToGlobal(result);
        return result;
}

Point2Di32 TuiWidget::mapFromGlobal(const Point2Di32 &p) const {
        TuiWidget *pw = dynamic_cast<TuiWidget *>(parent());
        Point2Di32 local = pw ? pw->mapFromGlobal(p) : p;
        return mapFromParent(local);
}

Size2Di32 TuiWidget::sizeHint() const {
        if(_layout) return _layout->sizeHint();
        return Size2Di32(10, 1);
}

Size2Di32 TuiWidget::minimumSizeHint() const {
        return Size2Di32(1, 1);
}

void TuiWidget::paintEvent(TuiPaintEvent *) {
}

void TuiWidget::keyEvent(KeyEvent *) {
}

void TuiWidget::mouseEvent(MouseEvent *) {
}

void TuiWidget::resizeEvent(TuiResizeEvent *) {
        if(_layout) {
                _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
        }
}

void TuiWidget::focusInEvent(Event *) {
}

void TuiWidget::focusOutEvent(Event *) {
}

void TuiWidget::event(Event *e) {
        if(e->type() == TuiPaintEvent::Paint) {
                paintEvent(static_cast<TuiPaintEvent *>(e));
                e->accept();
        } else if(e->type() == TuiResizeEvent::Resize) {
                resizeEvent(static_cast<TuiResizeEvent *>(e));
                e->accept();
        } else if(e->type() == KeyEvent::KeyPress || e->type() == KeyEvent::KeyRelease) {
                keyEvent(static_cast<KeyEvent *>(e));
        } else if(e->type() == MouseEvent::Mouse) {
                mouseEvent(static_cast<MouseEvent *>(e));
        } else {
                ObjectBase::event(e);
        }
}

PROMEKI_NAMESPACE_END
