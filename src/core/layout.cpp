/**
 * @file      layout.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <promeki/layout.h>
#include <promeki/widget.h>

PROMEKI_NAMESPACE_BEGIN

// Layout base class

Layout::Layout(ObjectBase *parent) : ObjectBase(parent) {}

Layout::~Layout() = default;

void Layout::addWidget(Widget *widget) {
        _widgets += widget;
}

void Layout::removeWidget(Widget *widget) {
        _widgets.removeFirst(widget);
}

void Layout::addLayout(Layout *layout) {
        _layouts += layout;
}

void Layout::setMargins(int top, int right, int bottom, int left) {
        _marginTop = top;
        _marginRight = right;
        _marginBottom = bottom;
        _marginLeft = left;
}

Rect2Di32 Layout::contentRect(const Rect2Di32 &available) const {
        return Rect2Di32(available.x() + _marginLeft, available.y() + _marginTop,
                         std::max(0, available.width() - _marginLeft - _marginRight),
                         std::max(0, available.height() - _marginTop - _marginBottom));
}

Size2Di32 Layout::sizeHint() const {
        int maxW = 0, maxH = 0;
        for (size_t i = 0; i < _widgets.size(); ++i) {
                if (!_widgets[i]) continue;
                auto hint = _widgets[i]->sizeHint();
                maxW = std::max(maxW, hint.width());
                maxH = std::max(maxH, hint.height());
        }
        maxW += _marginLeft + _marginRight;
        maxH += _marginTop + _marginBottom;
        return Size2Di32(maxW, maxH);
}

// BoxLayout

BoxLayout::BoxLayout(BoxDirection direction, ObjectBase *parent) : Layout(parent), _direction(direction) {}

void BoxLayout::addWidget(Widget *widget) {
        Layout::addWidget(widget);
        Item item;
        item.type = Item::WidgetItem;
        item.widget = widget;
        _items += item;
}

void BoxLayout::addLayout(Layout *layout) {
        Layout::addLayout(layout);
        Item item;
        item.type = Item::LayoutItem;
        item.layout = layout;
        _items += item;
}

void BoxLayout::addStretch(int factor) {
        Item item;
        item.type = Item::StretchItem;
        item.stretchFactor = factor;
        _items += item;
}

void BoxLayout::setStretch(int index, int factor) {
        if (index >= 0 && static_cast<size_t>(index) < _items.size()) {
                _items[index].stretchFactor = factor;
        }
}

void BoxLayout::calculateLayout(const Rect2Di32 &available) {
        Rect2Di32 content = contentRect(available);
        if (content.isEmpty() || _items.isEmpty()) return;

        bool horizontal = (_direction == LeftToRight || _direction == RightToLeft);
        int  totalSpace = horizontal ? content.width() : content.height();
        int  crossSpace = horizontal ? content.height() : content.width();
        int  itemCount = static_cast<int>(_items.size());
        int  totalSpacing = spacing() * (itemCount - 1);
        int  availableForItems = std::max(0, totalSpace - totalSpacing);

        // First pass: calculate minimum sizes and total stretch
        List<int> minSizes;
        int       totalMin = 0;
        int       totalStretch = 0;
        for (size_t i = 0; i < _items.size(); ++i) {
                const Item &item = _items[i];
                int         minSize = 0;
                bool        expanding = false;

                if (item.type == Item::WidgetItem && item.widget) {
                        Size2Di32 hint = item.widget->sizeHint();
                        Size2Di32 minHint = item.widget->minimumSizeHint();
                        expanding = (item.widget->sizePolicy() == SizeExpanding);
                        if (expanding) {
                                minSize = horizontal ? std::max(minHint.width(), 1) : std::max(minHint.height(), 1);
                        } else {
                                minSize = horizontal ? hint.width() : hint.height();
                        }
                } else if (item.type == Item::LayoutItem && item.layout) {
                        Size2Di32 hint = item.layout->sizeHint();
                        minSize = horizontal ? std::max(hint.width(), 1) : std::max(hint.height(), 1);
                        expanding = (item.stretchFactor > 0);
                }

                minSizes += minSize;
                totalMin += minSize;

                int sf = item.stretchFactor;
                if (sf == 0 && expanding) sf = 1;
                totalStretch += sf;
        }

        // Second pass: distribute remaining space
        int       remaining = availableForItems - totalMin;
        List<int> sizes = minSizes;

        if (remaining > 0 && totalStretch > 0) {
                for (size_t i = 0; i < _items.size(); ++i) {
                        const Item &item = _items[i];
                        int         sf = item.stretchFactor;
                        bool        expanding = false;
                        if (item.type == Item::WidgetItem && item.widget)
                                expanding = (item.widget->sizePolicy() == SizeExpanding);
                        else if (item.type == Item::LayoutItem)
                                expanding = (item.stretchFactor > 0);
                        if (sf == 0 && expanding) sf = 1;
                        if (sf > 0) {
                                int extra = remaining * sf / totalStretch;
                                sizes[i] += extra;
                        }
                }
        }

        // Third pass: position items
        int  pos = horizontal ? content.x() : content.y();
        bool reverse = (_direction == RightToLeft || _direction == BottomToTop);
        if (reverse) {
                pos = horizontal ? content.x() + content.width() : content.y() + content.height();
        }

        for (size_t i = 0; i < _items.size(); ++i) {
                const Item &item = _items[i];

                if (item.type == Item::StretchItem) {
                        if (reverse)
                                pos -= sizes[i];
                        else
                                pos += sizes[i];
                        if (!reverse)
                                pos += spacing();
                        else
                                pos -= spacing();
                        continue;
                }

                Rect2Di32 itemRect;
                if (horizontal) {
                        int wx = reverse ? pos - sizes[i] : pos;
                        itemRect = Rect2Di32(wx, content.y(), sizes[i], crossSpace);
                } else {
                        int wy = reverse ? pos - sizes[i] : pos;
                        itemRect = Rect2Di32(content.x(), wy, crossSpace, sizes[i]);
                }

                if (item.type == Item::WidgetItem && item.widget) {
                        Size2Di32 maxSize = item.widget->maximumSize();
                        if (itemRect.width() > maxSize.width()) itemRect.setWidth(maxSize.width());
                        if (itemRect.height() > maxSize.height()) itemRect.setHeight(maxSize.height());
                        item.widget->setGeometry(itemRect);
                } else if (item.type == Item::LayoutItem && item.layout) {
                        item.layout->calculateLayout(itemRect);
                }

                if (reverse) {
                        pos -= sizes[i] + spacing();
                } else {
                        pos += sizes[i] + spacing();
                }
        }
}

Size2Di32 BoxLayout::sizeHint() const {
        bool horizontal = (_direction == LeftToRight || _direction == RightToLeft);
        int  mt, mr, mb, ml;
        margins(mt, mr, mb, ml);
        int mainAxis = 0;
        int crossAxis = 0;
        int count = 0;
        for (size_t i = 0; i < _items.size(); ++i) {
                const Item &item = _items[i];
                Size2Di32   hint;
                if (item.type == Item::WidgetItem && item.widget) {
                        hint = item.widget->sizeHint();
                } else if (item.type == Item::LayoutItem && item.layout) {
                        hint = item.layout->sizeHint();
                } else {
                        continue;
                }
                int main = horizontal ? hint.width() : hint.height();
                int cross = horizontal ? hint.height() : hint.width();
                mainAxis += main;
                crossAxis = std::max(crossAxis, cross);
                count++;
        }
        if (count > 1) mainAxis += spacing() * (count - 1);
        int w = horizontal ? mainAxis + ml + mr : crossAxis + ml + mr;
        int h = horizontal ? crossAxis + mt + mb : mainAxis + mt + mb;
        return Size2Di32(w, h);
}

// GridLayout

GridLayout::GridLayout(ObjectBase *parent) : Layout(parent) {}

void GridLayout::addWidget(Widget *widget, int row, int col, int rowSpan, int colSpan) {
        _items += GridItem{widget, row, col, rowSpan, colSpan};
        Layout::addWidget(widget);
        if (row + rowSpan > _rowCount) _rowCount = row + rowSpan;
        if (col + colSpan > _colCount) _colCount = col + colSpan;
}

void GridLayout::setRowStretch(int row, int factor) {
        _rowStretch[row] = factor;
}

void GridLayout::setColumnStretch(int col, int factor) {
        _colStretch[col] = factor;
}

void GridLayout::setRowMinimumHeight(int row, int height) {
        _rowMinHeight[row] = height;
}

void GridLayout::setColumnMinimumWidth(int col, int width) {
        _colMinWidth[col] = width;
}

void GridLayout::calculateLayout(const Rect2Di32 &available) {
        if (_rowCount == 0 || _colCount == 0 || _items.isEmpty()) return;

        Rect2Di32 content = contentRect(available);

        // Calculate column widths
        List<int> colWidths;
        colWidths.resize(_colCount, 0);
        for (int c = 0; c < _colCount; ++c) {
                auto it = _colMinWidth.find(c);
                if (it != _colMinWidth.end())
                        colWidths[c] = it->second;
                else
                        colWidths[c] = 1;
        }

        // Calculate row heights
        List<int> rowHeights;
        rowHeights.resize(_rowCount, 0);
        for (int r = 0; r < _rowCount; ++r) {
                auto it = _rowMinHeight.find(r);
                if (it != _rowMinHeight.end())
                        rowHeights[r] = it->second;
                else
                        rowHeights[r] = 1;
        }

        // Distribute remaining width
        int totalColWidth = 0;
        for (int c = 0; c < _colCount; ++c) totalColWidth += colWidths[c];
        int remainingW = content.width() - totalColWidth - spacing() * (_colCount - 1);
        if (remainingW > 0) {
                int totalColStretch = 0;
                for (int c = 0; c < _colCount; ++c) {
                        auto it = _colStretch.find(c);
                        totalColStretch += (it != _colStretch.end()) ? it->second : 1;
                }
                if (totalColStretch > 0) {
                        for (int c = 0; c < _colCount; ++c) {
                                auto it = _colStretch.find(c);
                                int  stretch = (it != _colStretch.end()) ? it->second : 1;
                                colWidths[c] += remainingW * stretch / totalColStretch;
                        }
                }
        }

        // Distribute remaining height
        int totalRowHeight = 0;
        for (int r = 0; r < _rowCount; ++r) totalRowHeight += rowHeights[r];
        int remainingH = content.height() - totalRowHeight - spacing() * (_rowCount - 1);
        if (remainingH > 0) {
                int totalRowStretch = 0;
                for (int r = 0; r < _rowCount; ++r) {
                        auto it = _rowStretch.find(r);
                        totalRowStretch += (it != _rowStretch.end()) ? it->second : 1;
                }
                if (totalRowStretch > 0) {
                        for (int r = 0; r < _rowCount; ++r) {
                                auto it = _rowStretch.find(r);
                                int  stretch = (it != _rowStretch.end()) ? it->second : 1;
                                rowHeights[r] += remainingH * stretch / totalRowStretch;
                        }
                }
        }

        // Calculate positions
        List<int> colX;
        colX.resize(_colCount, 0);
        colX[0] = content.x();
        for (int c = 1; c < _colCount; ++c) {
                colX[c] = colX[c - 1] + colWidths[c - 1] + spacing();
        }

        List<int> rowY;
        rowY.resize(_rowCount, 0);
        rowY[0] = content.y();
        for (int r = 1; r < _rowCount; ++r) {
                rowY[r] = rowY[r - 1] + rowHeights[r - 1] + spacing();
        }

        // Position widgets
        for (size_t i = 0; i < _items.size(); ++i) {
                const GridItem &item = _items[i];
                int             x = colX[item.col];
                int             y = rowY[item.row];
                int             w = 0;
                for (int c = item.col; c < item.col + item.colSpan && c < _colCount; ++c) {
                        w += colWidths[c];
                        if (c > item.col) w += spacing();
                }
                int h = 0;
                for (int r = item.row; r < item.row + item.rowSpan && r < _rowCount; ++r) {
                        h += rowHeights[r];
                        if (r > item.row) h += spacing();
                }
                item.widget->setGeometry(Rect2Di32(x, y, w, h));
        }
}

PROMEKI_NAMESPACE_END
