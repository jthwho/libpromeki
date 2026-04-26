/**
 * @file      listview.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/listview.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/tuisubsystem.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

TuiListView::TuiListView(ObjectBase *parent) : TuiWidget(parent) {
        setFocusPolicy(StrongFocus);
}

TuiListView::~TuiListView() = default;

void TuiListView::addItem(const String &item) {
        _items += item;
        if (_currentIndex < 0) _currentIndex = 0;
        update();
}

void TuiListView::insertItem(int index, const String &item) {
        int idx = std::clamp(index, 0, static_cast<int>(_items.size()));
        _items.insert(static_cast<size_t>(idx), item);
        if (_currentIndex < 0)
                _currentIndex = 0;
        else if (_currentIndex >= idx)
                _currentIndex++;
        if (_scrollOffset > idx) _scrollOffset++;
        update();
}

void TuiListView::setItems(const StringList &items) {
        _items = items;
        _currentIndex = _items.isEmpty() ? -1 : 0;
        _scrollOffset = 0;
        update();
}

void TuiListView::clear() {
        _items.clear();
        _currentIndex = -1;
        _scrollOffset = 0;
        update();
}

void TuiListView::setCurrentIndex(int index) {
        if (index < 0 || index >= static_cast<int>(_items.size())) return;
        if (_currentIndex == index) return;
        _currentIndex = index;
        ensureVisible(index);
        currentItemChangedSignal.emit(_currentIndex);
        update();
}

String TuiListView::currentItem() const {
        if (_currentIndex < 0 || static_cast<size_t>(_currentIndex) >= _items.size()) {
                return String();
        }
        return _items[_currentIndex];
}

Size2Di32 TuiListView::sizeHint() const {
        return Size2Di32(20, 10);
}

void TuiListView::scrollBy(int delta) {
        int itemCount = static_cast<int>(_items.size());
        int h = height();
        int maxOffset = std::max(0, itemCount - h);
        int newOffset = std::clamp(_scrollOffset + delta, 0, maxOffset);
        if (newOffset != _scrollOffset) {
                _scrollOffset = newOffset;
                update();
        }
}

int TuiListView::contentWidth() const {
        return std::max(1, width() - 1); // Reserve 1 column for scrollbar
}

int TuiListView::trackHeight() const {
        return std::max(0, height() - 2);
}

int TuiListView::thumbSize() const {
        int h = height();
        int trackH = trackHeight();
        int itemCount = static_cast<int>(_items.size());
        if (trackH <= 0 || itemCount <= 0 || itemCount <= h) return trackH;
        return std::max(1, trackH * h / itemCount);
}

int TuiListView::thumbPos() const {
        int h = height();
        int trackH = trackHeight();
        int itemCount = static_cast<int>(_items.size());
        if (trackH <= 0 || itemCount <= h) return 0;
        int ts = thumbSize();
        int maxOffset = itemCount - h;
        if (maxOffset <= 0) return 0;
        int pos = _scrollOffset * (trackH - ts) / maxOffset;
        return std::clamp(pos, 0, trackH - ts);
}

void TuiListView::ensureVisible(int index) {
        int oldOffset = _scrollOffset;
        if (index < _scrollOffset) _scrollOffset = index;
        if (index >= _scrollOffset + height()) _scrollOffset = index - height() + 1;
        if (_scrollOffset != oldOffset) update();
}

void TuiListView::paintEvent(PaintEvent *) {
        TuiSubsystem *app = TuiSubsystem::instance();
        if (!app) return;

        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
        Rect2Di32  clipRect(screenPos.x(), screenPos.y(), width(), height());
        TuiPainter painter(app->screen(), clipRect);

        const TuiPalette &pal = app->palette();
        bool              focused = hasFocus();
        bool              enabled = isEnabled();
        int               cw = contentWidth();
        int               itemCount = static_cast<int>(_items.size());

        // Normal item style
        TuiStyle normalStyle =
                pal.style(TuiPalette::Text, focused, enabled).merged(pal.style(TuiPalette::Base, focused, enabled));
        // Highlighted item style
        TuiStyle hlStyle = pal.style(TuiPalette::HighlightedText, focused, enabled)
                                   .merged(pal.style(TuiPalette::Highlight, focused, enabled));

        // Draw list content
        painter.setStyle(normalStyle);
        painter.fillRect(Rect2Di32(0, 0, cw, height()));

        for (int row = 0; row < height(); ++row) {
                int idx = _scrollOffset + row;
                if (idx < 0 || idx >= itemCount) continue;

                if (idx == _currentIndex) {
                        painter.setStyle(hlStyle);
                        painter.fillRect(Rect2Di32(0, row, cw, 1));
                } else {
                        painter.setStyle(normalStyle);
                }

                String display = _items[idx];
                if (static_cast<int>(display.length()) > cw) {
                        display = display.substr(0, cw);
                }
                painter.drawText(0, row, display);
        }

        // Draw scrollbar
        paintScrollbar(painter, pal);
}

void TuiListView::paintScrollbar(TuiPainter &painter, const TuiPalette &pal) {
        int sbx = width() - 1;
        int h = height();
        if (h < 3 || sbx < 0) return;

        bool focused = hasFocus();
        bool enabled = isEnabled();
        int  itemCount = static_cast<int>(_items.size());

        // Scrollbar track
        TuiStyle trackStyle =
                pal.style(TuiPalette::Mid, focused, enabled).merged(pal.style(TuiPalette::Window, focused, enabled));
        painter.setStyle(trackStyle);

        // Up arrow
        painter.drawChar(sbx, 0, U'\u25B2'); // up triangle
        // Down arrow
        painter.drawChar(sbx, h - 1, U'\u25BC'); // down triangle

        // Track area
        int trackH = h - 2;
        if (trackH <= 0) return;

        for (int i = 0; i < trackH; ++i) {
                painter.drawChar(sbx, 1 + i, U'\u2502'); // vertical line
        }

        // Thumb
        if (itemCount > 0) {
                int      ts = thumbSize();
                int      tp = thumbPos();
                TuiStyle thumbStyle = pal.style(TuiPalette::WindowText, focused, enabled)
                                              .merged(pal.style(TuiPalette::Mid, focused, enabled));
                painter.setStyle(thumbStyle);
                for (int i = 0; i < ts; ++i) {
                        painter.drawChar(sbx, 1 + tp + i, U'\u2588'); // full block
                }
        }
}

void TuiListView::keyPressEvent(KeyEvent *e) {
        // Let Ctrl-modified keys propagate (e.g. Ctrl+Left/Right for tab switching)
        if (e->isCtrl()) return;
        switch (e->key()) {
                case KeyEvent::Key_Up:
                        if (_currentIndex > 0) setCurrentIndex(_currentIndex - 1);
                        e->accept();
                        break;
                case KeyEvent::Key_Down:
                        if (_currentIndex < static_cast<int>(_items.size()) - 1) setCurrentIndex(_currentIndex + 1);
                        e->accept();
                        break;
                case KeyEvent::Key_PageUp:
                        setCurrentIndex(std::max(0, _currentIndex - height()));
                        e->accept();
                        break;
                case KeyEvent::Key_PageDown:
                        setCurrentIndex(std::min(static_cast<int>(_items.size()) - 1, _currentIndex + height()));
                        e->accept();
                        break;
                case KeyEvent::Key_Home:
                        setCurrentIndex(0);
                        e->accept();
                        break;
                case KeyEvent::Key_End:
                        setCurrentIndex(static_cast<int>(_items.size()) - 1);
                        e->accept();
                        break;
                case KeyEvent::Key_Enter:
                        if (_currentIndex >= 0) itemActivatedSignal.emit(_currentIndex);
                        e->accept();
                        break;
                default: break;
        }
}

void TuiListView::mouseEvent(MouseEvent *e) {
        if (e->action() == MouseEvent::ScrollUp) {
                scrollBy(-1);
                e->accept();
                return;
        }
        if (e->action() == MouseEvent::ScrollDown) {
                scrollBy(1);
                e->accept();
                return;
        }

        Point2Di32 local = mapFromGlobal(e->pos());
        int        sbx = width() - 1;
        int        h = height();
        int        itemCount = static_cast<int>(_items.size());

        if (e->action() == MouseEvent::Release) {
                _dragging = false;
                e->accept();
                return;
        }

        if (e->action() == MouseEvent::Move && _dragging) {
                // Drag thumb: map mouse Y to scroll offset
                int trackH = trackHeight();
                int ts = thumbSize();
                if (trackH > ts && itemCount > h) {
                        int trackPos = local.y() - 1 - _dragOffset;
                        int maxOffset = itemCount - h;
                        int maxThumbPos = trackH - ts;
                        int newOffset = std::clamp(trackPos * maxOffset / maxThumbPos, 0, maxOffset);
                        if (newOffset != _scrollOffset) {
                                _scrollOffset = newOffset;
                                update();
                        }
                }
                e->accept();
                return;
        }

        if (e->action() == MouseEvent::Press || e->action() == MouseEvent::DoubleClick) {
                if (local.x() == sbx) {
                        if (local.y() == 0) {
                                // Up arrow
                                scrollBy(-1);
                        } else if (local.y() == h - 1) {
                                // Down arrow
                                scrollBy(1);
                        } else {
                                // Track area
                                int trackY = local.y() - 1; // Position within track
                                int tp = thumbPos();
                                int ts = thumbSize();
                                if (trackY >= tp && trackY < tp + ts) {
                                        // Click on thumb: start drag
                                        _dragging = true;
                                        _dragOffset = trackY - tp;
                                } else if (trackY < tp) {
                                        // Click above thumb: page up
                                        scrollBy(-h);
                                } else {
                                        // Click below thumb: page down
                                        scrollBy(h);
                                }
                        }
                        e->accept();
                } else {
                        // Click in content area to select item
                        int idx = _scrollOffset + local.y();
                        if (idx >= 0 && idx < itemCount) {
                                setCurrentIndex(idx);
                                if (e->action() == MouseEvent::DoubleClick) {
                                        itemActivatedSignal.emit(idx);
                                }
                        }
                        e->accept();
                }
        }
}

PROMEKI_NAMESPACE_END
