/**
 * @file      widget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/widget.h>
#include <promeki/core/layout.h>
#include <promeki/core/keyevent.h>
#include <promeki/core/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

const Event::Type PaintEvent::Paint = Event::registerType();
const Event::Type ResizeEvent::Resize = Event::registerType();

Widget::Widget(ObjectBase *parent) : ObjectBase(parent) {
}

Widget::~Widget() = default;

void Widget::setGeometry(const Rect2Di32 &rect) {
        if(_geometry == rect) return;
        Size2Di32 oldSize = size();
        _geometry = rect;
        Size2Di32 newSize = size();
        if(oldSize.width() != newSize.width() || oldSize.height() != newSize.height()) {
                ResizeEvent e(newSize);
                resizeEvent(&e);
                resizedSignal.emit(newSize);
        }
        update();
}

void Widget::show() {
        setVisible(true);
}

void Widget::hide() {
        setVisible(false);
}

bool Widget::isEffectivelyVisible() const {
        if(!_visible) return false;
        Widget *pw = dynamic_cast<Widget *>(parent());
        if(pw) return pw->isEffectivelyVisible();
        return true;
}

void Widget::setVisible(bool visible) {
        if(_visible == visible) return;
        _visible = visible;
        visibilityChangedSignal.emit(visible);
        update();
}

void Widget::setFocus() {
        if(_focusPolicy == NoFocus) return;
        _focused = true;
        Event e(Event::InvalidType);
        focusInEvent(&e);
        update();
}

void Widget::update() {
        _dirty = true;
}

void Widget::setLayout(Layout *layout) {
        _layout = layout;
}

Point2Di32 Widget::mapToParent(const Point2Di32 &p) const {
        return Point2Di32(p.x() + _geometry.x(), p.y() + _geometry.y());
}

Point2Di32 Widget::mapFromParent(const Point2Di32 &p) const {
        return Point2Di32(p.x() - _geometry.x(), p.y() - _geometry.y());
}

Point2Di32 Widget::mapToGlobal(const Point2Di32 &p) const {
        Point2Di32 result = mapToParent(p);
        Widget *pw = dynamic_cast<Widget *>(parent());
        if(pw) return pw->mapToGlobal(result);
        return result;
}

Point2Di32 Widget::mapFromGlobal(const Point2Di32 &p) const {
        Widget *pw = dynamic_cast<Widget *>(parent());
        Point2Di32 local = pw ? pw->mapFromGlobal(p) : p;
        return mapFromParent(local);
}

Size2Di32 Widget::sizeHint() const {
        if(_layout) return _layout->sizeHint();
        return Size2Di32(10, 1);
}

Size2Di32 Widget::minimumSizeHint() const {
        return Size2Di32(1, 1);
}

void Widget::paintEvent(PaintEvent *) {
}

void Widget::keyEvent(KeyEvent *) {
}

void Widget::mouseEvent(MouseEvent *) {
}

void Widget::resizeEvent(ResizeEvent *) {
        if(_layout) {
                _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
        }
}

void Widget::focusInEvent(Event *) {
}

void Widget::focusOutEvent(Event *) {
}

void Widget::event(Event *e) {
        if(e->type() == PaintEvent::Paint) {
                paintEvent(static_cast<PaintEvent *>(e));
                e->accept();
        } else if(e->type() == ResizeEvent::Resize) {
                resizeEvent(static_cast<ResizeEvent *>(e));
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
