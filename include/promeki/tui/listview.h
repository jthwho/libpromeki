/**
 * @file      tui/listview.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

class TuiPainter;
class TuiPalette;

/**
 * @brief Scrollable list of items with keyboard and mouse navigation.
 * @ingroup tui_widgets
 *
 * Supports single selection via keyboard (Up/Down/PageUp/PageDown/Home/End)
 * and mouse (click to select, double-click to activate). Mouse wheel and
 * scrollbar interaction scroll the viewport independently of the selection.
 * The Enter key activates the current item via the itemActivated signal.
 */
class TuiListView : public TuiWidget {
        PROMEKI_OBJECT(TuiListView, TuiWidget)
        public:
                TuiListView(ObjectBase *parent = nullptr);
                ~TuiListView() override;

                void addItem(const String &item);
                void insertItem(int index, const String &item);
                void setItems(const StringList &items);
                void clear();

                int currentIndex() const { return _currentIndex; }
                void setCurrentIndex(int index);

                String currentItem() const;
                int count() const { return static_cast<int>(_items.size()); }

                /** @brief Returns the current scroll offset. */
                int scrollOffset() const { return _scrollOffset; }

                /**
                 * @brief Scrolls the viewport by the given number of items.
                 *
                 * Moves only the viewport scroll offset without changing the current
                 * selection (following Qt convention). The current index may end up
                 * off-screen after scrolling; use ensureVisible() to bring it back
                 * into view if needed.
                 *
                 * @param delta Number of items to scroll (positive = down, negative = up).
                 */
                void scrollBy(int delta);

                /**
                 * @brief Adjusts the scroll offset so that the given index is visible.
                 * @param index The item index to make visible.
                 */
                void ensureVisible(int index);

                Size2Di32 sizeHint() const override;

                PROMEKI_SIGNAL(currentItemChanged, int)
                PROMEKI_SIGNAL(itemActivated, int)

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void keyEvent(KeyEvent *e) override;
                void mouseEvent(MouseEvent *e) override;

        private:
                StringList      _items;
                int             _currentIndex = -1;
                int             _scrollOffset = 0;
                bool            _dragging = false;
                int             _dragOffset = 0;        ///< Offset within thumb when drag started.

                int contentWidth() const;
                void paintScrollbar(TuiPainter &painter, const TuiPalette &pal);
                int thumbPos() const;
                int thumbSize() const;
                int trackHeight() const;
};

PROMEKI_NAMESPACE_END
