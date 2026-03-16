/**
 * @file      tui/layout.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/rect.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

class TuiWidget;

/**
 * @brief Abstract base class for TUI layout managers.
 * @ingroup tui_core
 *
 * A layout positions child widgets within the available rectangle.
 * Layouts can be nested by adding sub-layouts.
 */
class TuiLayout : public ObjectBase {
        PROMEKI_OBJECT(TuiLayout, ObjectBase)
        public:
                TuiLayout(ObjectBase *parent = nullptr);
                virtual ~TuiLayout();

                /** @brief Adds a widget to this layout. */
                virtual void addWidget(TuiWidget *widget);

                /** @brief Removes a widget from this layout. */
                void removeWidget(TuiWidget *widget);

                /** @brief Adds a nested layout. */
                virtual void addLayout(TuiLayout *layout);

                /** @brief Sets the margins (top, right, bottom, left). */
                void setMargins(int top, int right, int bottom, int left);

                /** @brief Sets uniform margins. */
                void setMargins(int margin) { setMargins(margin, margin, margin, margin); }

                /** @brief Sets the spacing between items. */
                void setSpacing(int spacing) { _spacing = spacing; }

                /** @brief Returns the spacing. */
                int spacing() const { return _spacing; }

                /** @brief Returns the margins. */
                void margins(int &top, int &right, int &bottom, int &left) const {
                        top = _marginTop;
                        right = _marginRight;
                        bottom = _marginBottom;
                        left = _marginLeft;
                }

                /**
                 * @brief Calculates and applies the layout to the given rectangle.
                 * @param available The available rectangle.
                 */
                virtual void calculateLayout(const Rect2Di32 &available) = 0;

                /**
                 * @brief Returns the preferred size for this layout based on its children.
                 */
                virtual Size2Di32 sizeHint() const;

                /** @brief Returns the list of managed widgets. */
                const List<TuiWidget *> &widgets() const { return _widgets; }

                /** @brief Returns the list of sub-layouts. */
                const List<TuiLayout *> &layouts() const { return _layouts; }

        protected:
                /**
                 * @brief Returns the content rect after subtracting margins.
                 */
                Rect2Di32 contentRect(const Rect2Di32 &available) const;

                List<TuiWidget *>       _widgets;
                List<TuiLayout *>       _layouts;

        private:
                int     _spacing = 0;
                int     _marginTop = 0;
                int     _marginRight = 0;
                int     _marginBottom = 0;
                int     _marginLeft = 0;
};

/**
 * @brief Box layout direction.
 */
enum TuiBoxDirection {
        LeftToRight,
        RightToLeft,
        TopToBottom,
        BottomToTop
};

/**
 * @brief Layout that arranges widgets in a horizontal or vertical line.
 *
 * Items are added in order via addWidget(), addLayout(), or addStretch().
 * All three are placed into a single ordered list so that sub-layouts
 * participate in size negotiation and positioning just like widgets.
 */
class TuiBoxLayout : public TuiLayout {
        PROMEKI_OBJECT(TuiBoxLayout, TuiLayout)
        public:
                /**
                 * @brief Constructs a TuiBoxLayout.
                 * @param direction The layout direction.
                 * @param parent Optional parent.
                 */
                TuiBoxLayout(TuiBoxDirection direction, ObjectBase *parent = nullptr);

                /** @brief Returns the direction. */
                TuiBoxDirection direction() const { return _direction; }

                /** @brief Adds a widget to the ordered item list. */
                void addWidget(TuiWidget *widget) override;

                /** @brief Adds a sub-layout to the ordered item list. */
                void addLayout(TuiLayout *layout) override;

                /** @brief Adds a stretch item with the given factor. */
                void addStretch(int factor = 1);

                /** @brief Sets the stretch factor for the item at the given index. */
                void setStretch(int index, int factor);

                /** @brief Calculates and applies the layout. */
                void calculateLayout(const Rect2Di32 &available) override;

                /** @brief Returns the preferred size based on children and direction. */
                Size2Di32 sizeHint() const override;

        private:
                /** @brief A single item in the box layout's ordered list. */
                struct Item {
                        enum Type { WidgetItem, LayoutItem, StretchItem };
                        Type            type;
                        TuiWidget       *widget = nullptr;
                        TuiLayout       *layout = nullptr;
                        int             stretchFactor = 0;
                };

                TuiBoxDirection         _direction;
                List<Item>              _items;
};

/**
 * @brief Horizontal box layout (left to right).
 */
class TuiHBoxLayout : public TuiBoxLayout {
        PROMEKI_OBJECT(TuiHBoxLayout, TuiBoxLayout)
        public:
                TuiHBoxLayout(ObjectBase *parent = nullptr)
                        : TuiBoxLayout(LeftToRight, parent) {}
};

/**
 * @brief Vertical box layout (top to bottom).
 */
class TuiVBoxLayout : public TuiBoxLayout {
        PROMEKI_OBJECT(TuiVBoxLayout, TuiBoxLayout)
        public:
                TuiVBoxLayout(ObjectBase *parent = nullptr)
                        : TuiBoxLayout(TopToBottom, parent) {}
};

/**
 * @brief Grid layout with rows and columns.
 */
class TuiGridLayout : public TuiLayout {
        PROMEKI_OBJECT(TuiGridLayout, TuiLayout)
        public:
                TuiGridLayout(ObjectBase *parent = nullptr);

                /**
                 * @brief Adds a widget at the given row/column with optional span.
                 */
                void addWidget(TuiWidget *widget, int row, int col,
                               int rowSpan = 1, int colSpan = 1);

                /** @brief Sets the stretch factor for a row. */
                void setRowStretch(int row, int factor);

                /** @brief Sets the stretch factor for a column. */
                void setColumnStretch(int col, int factor);

                /** @brief Sets the minimum height for a row. */
                void setRowMinimumHeight(int row, int height);

                /** @brief Sets the minimum width for a column. */
                void setColumnMinimumWidth(int col, int width);

                /** @brief Calculates and applies the layout. */
                void calculateLayout(const Rect2Di32 &available) override;

        private:
                struct GridItem {
                        TuiWidget       *widget;
                        int             row;
                        int             col;
                        int             rowSpan;
                        int             colSpan;
                };
                List<GridItem>          _items;
                Map<int, int>           _rowStretch;
                Map<int, int>           _colStretch;
                Map<int, int>           _rowMinHeight;
                Map<int, int>           _colMinWidth;
                int                     _rowCount = 0;
                int                     _colCount = 0;
};

PROMEKI_NAMESPACE_END
