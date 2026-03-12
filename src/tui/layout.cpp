/**
 * @file      layout.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <promeki/tui/layout.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

// TuiLayout base class

TuiLayout::TuiLayout(ObjectBase *parent) : ObjectBase(parent) {
}

TuiLayout::~TuiLayout() = default;

void TuiLayout::addWidget(TuiWidget *widget) {
        _widgets += widget;
}

void TuiLayout::removeWidget(TuiWidget *widget) {
        _widgets.removeFirst(widget);
}

void TuiLayout::addLayout(TuiLayout *layout) {
        _layouts += layout;
}

void TuiLayout::setMargins(int top, int right, int bottom, int left) {
        _marginTop = top;
        _marginRight = right;
        _marginBottom = bottom;
        _marginLeft = left;
}

Rect2Di32 TuiLayout::contentRect(const Rect2Di32 &available) const {
        return Rect2Di32(
                available.x() + _marginLeft,
                available.y() + _marginTop,
                std::max(0, available.width() - _marginLeft - _marginRight),
                std::max(0, available.height() - _marginTop - _marginBottom)
        );
}

Size2Di32 TuiLayout::sizeHint() const {
        int maxW = 0, maxH = 0;
        for(size_t i = 0; i < _widgets.size(); ++i) {
                if(!_widgets[i]) continue;
                auto hint = _widgets[i]->sizeHint();
                maxW = std::max(maxW, hint.width());
                maxH = std::max(maxH, hint.height());
        }
        maxW += _marginLeft + _marginRight;
        maxH += _marginTop + _marginBottom;
        return Size2Di32(maxW, maxH);
}

// TuiBoxLayout

TuiBoxLayout::TuiBoxLayout(TuiBoxDirection direction, ObjectBase *parent)
        : TuiLayout(parent), _direction(direction) {
}

void TuiBoxLayout::addStretch(int factor) {
        _stretchFactors += factor;
        // Add a null widget placeholder
        _widgets += nullptr;
}

void TuiBoxLayout::setStretch(int index, int factor) {
        while(_stretchFactors.size() <= static_cast<size_t>(index)) {
                _stretchFactors += 0;
        }
        _stretchFactors[index] = factor;
}

void TuiBoxLayout::calculateLayout(const Rect2Di32 &available) {
        Rect2Di32 content = contentRect(available);
        if(content.isEmpty() || _widgets.isEmpty()) return;

        bool horizontal = (_direction == LeftToRight || _direction == RightToLeft);
        int totalSpace = horizontal ? content.width() : content.height();
        int crossSpace = horizontal ? content.height() : content.width();
        int itemCount = _widgets.size();
        int totalSpacing = spacing() * (itemCount - 1);
        int availableForItems = std::max(0, totalSpace - totalSpacing);

        // Ensure stretch factors list matches widget count
        while(_stretchFactors.size() < _widgets.size()) {
                _stretchFactors += 0;
        }

        // First pass: calculate minimum sizes and total stretch
        List<int> minSizes;
        int totalMin = 0;
        int totalStretch = 0;
        for(size_t i = 0; i < _widgets.size(); ++i) {
                int minSize = 0;
                if(_widgets[i] != nullptr) {
                        Size2Di32 hint = _widgets[i]->sizeHint();
                        Size2Di32 minHint = _widgets[i]->minimumSizeHint();
                        minSize = horizontal ? std::max(minHint.width(), 1)
                                             : std::max(minHint.height(), 1);
                        // For non-expanding widgets, prefer their hint
                        if(_stretchFactors[i] == 0 && _widgets[i]->sizePolicy() != SizeExpanding) {
                                minSize = horizontal ? hint.width() : hint.height();
                        }
                }
                minSizes += minSize;
                totalMin += minSize;
                totalStretch += _stretchFactors[i];
        }

        // Second pass: distribute remaining space
        int remaining = availableForItems - totalMin;
        List<int> sizes = minSizes;

        if(remaining > 0 && totalStretch > 0) {
                for(size_t i = 0; i < _widgets.size(); ++i) {
                        if(_stretchFactors[i] > 0) {
                                int extra = remaining * _stretchFactors[i] / totalStretch;
                                sizes[i] += extra;
                        }
                }
        } else if(remaining > 0 && totalStretch == 0) {
                // Distribute evenly among expanding widgets
                int expandCount = 0;
                for(size_t i = 0; i < _widgets.size(); ++i) {
                        if(_widgets[i] && _widgets[i]->sizePolicy() == SizeExpanding) {
                                expandCount++;
                        }
                }
                if(expandCount > 0) {
                        int extra = remaining / expandCount;
                        for(size_t i = 0; i < _widgets.size(); ++i) {
                                if(_widgets[i] && _widgets[i]->sizePolicy() == SizeExpanding) {
                                        sizes[i] += extra;
                                }
                        }
                }
        }

        // Third pass: position widgets
        int pos = horizontal ? content.x() : content.y();
        bool reverse = (_direction == RightToLeft || _direction == BottomToTop);
        if(reverse) {
                pos = horizontal ? content.x() + content.width() : content.y() + content.height();
        }

        for(size_t i = 0; i < _widgets.size(); ++i) {
                if(_widgets[i] == nullptr) {
                        // Stretch spacer
                        if(reverse) pos -= sizes[i];
                        else pos += sizes[i];
                        if(!reverse) pos += spacing();
                        else pos -= spacing();
                        continue;
                }

                Rect2Di32 widgetRect;
                if(horizontal) {
                        int wx = reverse ? pos - sizes[i] : pos;
                        widgetRect = Rect2Di32(wx, content.y(), sizes[i], crossSpace);
                } else {
                        int wy = reverse ? pos - sizes[i] : pos;
                        widgetRect = Rect2Di32(content.x(), wy, crossSpace, sizes[i]);
                }

                // Clamp to max size
                Size2Di32 maxSize = _widgets[i]->maximumSize();
                if(widgetRect.width() > maxSize.width()) {
                        widgetRect.setWidth(maxSize.width());
                }
                if(widgetRect.height() > maxSize.height()) {
                        widgetRect.setHeight(maxSize.height());
                }

                _widgets[i]->setGeometry(widgetRect);

                if(reverse) {
                        pos -= sizes[i] + spacing();
                } else {
                        pos += sizes[i] + spacing();
                }
        }
}

Size2Di32 TuiBoxLayout::sizeHint() const {
        bool horizontal = (_direction == LeftToRight || _direction == RightToLeft);
        int mt, mr, mb, ml;
        margins(mt, mr, mb, ml);
        int mainAxis = 0;
        int crossAxis = 0;
        int count = 0;
        for(size_t i = 0; i < _widgets.size(); ++i) {
                if(!_widgets[i]) continue;
                auto hint = _widgets[i]->sizeHint();
                int main = horizontal ? hint.width() : hint.height();
                int cross = horizontal ? hint.height() : hint.width();
                mainAxis += main;
                crossAxis = std::max(crossAxis, cross);
                count++;
        }
        if(count > 1) mainAxis += spacing() * (count - 1);
        int w = horizontal ? mainAxis + ml + mr : crossAxis + ml + mr;
        int h = horizontal ? crossAxis + mt + mb : mainAxis + mt + mb;
        return Size2Di32(w, h);
}

// TuiGridLayout

TuiGridLayout::TuiGridLayout(ObjectBase *parent) : TuiLayout(parent) {
}

void TuiGridLayout::addWidget(TuiWidget *widget, int row, int col,
                              int rowSpan, int colSpan) {
        _items += GridItem{widget, row, col, rowSpan, colSpan};
        TuiLayout::addWidget(widget);
        if(row + rowSpan > _rowCount) _rowCount = row + rowSpan;
        if(col + colSpan > _colCount) _colCount = col + colSpan;
}

void TuiGridLayout::setRowStretch(int row, int factor) {
        _rowStretch[row] = factor;
}

void TuiGridLayout::setColumnStretch(int col, int factor) {
        _colStretch[col] = factor;
}

void TuiGridLayout::setRowMinimumHeight(int row, int height) {
        _rowMinHeight[row] = height;
}

void TuiGridLayout::setColumnMinimumWidth(int col, int width) {
        _colMinWidth[col] = width;
}

void TuiGridLayout::calculateLayout(const Rect2Di32 &available) {
        if(_rowCount == 0 || _colCount == 0 || _items.isEmpty()) return;

        Rect2Di32 content = contentRect(available);

        // Calculate column widths
        List<int> colWidths;
        colWidths.resize(_colCount, 0);
        for(int c = 0; c < _colCount; ++c) {
                auto it = _colMinWidth.find(c);
                if(it != _colMinWidth.end()) colWidths[c] = it->second;
                else colWidths[c] = 1;
        }

        // Calculate row heights
        List<int> rowHeights;
        rowHeights.resize(_rowCount, 0);
        for(int r = 0; r < _rowCount; ++r) {
                auto it = _rowMinHeight.find(r);
                if(it != _rowMinHeight.end()) rowHeights[r] = it->second;
                else rowHeights[r] = 1;
        }

        // Distribute remaining width
        int totalColWidth = 0;
        for(int c = 0; c < _colCount; ++c) totalColWidth += colWidths[c];
        int remainingW = content.width() - totalColWidth - spacing() * (_colCount - 1);
        if(remainingW > 0) {
                int totalColStretch = 0;
                for(int c = 0; c < _colCount; ++c) {
                        auto it = _colStretch.find(c);
                        totalColStretch += (it != _colStretch.end()) ? it->second : 1;
                }
                if(totalColStretch > 0) {
                        for(int c = 0; c < _colCount; ++c) {
                                auto it = _colStretch.find(c);
                                int stretch = (it != _colStretch.end()) ? it->second : 1;
                                colWidths[c] += remainingW * stretch / totalColStretch;
                        }
                }
        }

        // Distribute remaining height
        int totalRowHeight = 0;
        for(int r = 0; r < _rowCount; ++r) totalRowHeight += rowHeights[r];
        int remainingH = content.height() - totalRowHeight - spacing() * (_rowCount - 1);
        if(remainingH > 0) {
                int totalRowStretch = 0;
                for(int r = 0; r < _rowCount; ++r) {
                        auto it = _rowStretch.find(r);
                        totalRowStretch += (it != _rowStretch.end()) ? it->second : 1;
                }
                if(totalRowStretch > 0) {
                        for(int r = 0; r < _rowCount; ++r) {
                                auto it = _rowStretch.find(r);
                                int stretch = (it != _rowStretch.end()) ? it->second : 1;
                                rowHeights[r] += remainingH * stretch / totalRowStretch;
                        }
                }
        }

        // Calculate positions
        List<int> colX;
        colX.resize(_colCount, 0);
        colX[0] = content.x();
        for(int c = 1; c < _colCount; ++c) {
                colX[c] = colX[c - 1] + colWidths[c - 1] + spacing();
        }

        List<int> rowY;
        rowY.resize(_rowCount, 0);
        rowY[0] = content.y();
        for(int r = 1; r < _rowCount; ++r) {
                rowY[r] = rowY[r - 1] + rowHeights[r - 1] + spacing();
        }

        // Position widgets
        for(size_t i = 0; i < _items.size(); ++i) {
                const GridItem &item = _items[i];
                int x = colX[item.col];
                int y = rowY[item.row];
                int w = 0;
                for(int c = item.col; c < item.col + item.colSpan && c < _colCount; ++c) {
                        w += colWidths[c];
                        if(c > item.col) w += spacing();
                }
                int h = 0;
                for(int r = item.row; r < item.row + item.rowSpan && r < _rowCount; ++r) {
                        h += rowHeights[r];
                        if(r > item.row) h += spacing();
                }
                item.widget->setGeometry(Rect2Di32(x, y, w, h));
        }
}

PROMEKI_NAMESPACE_END
