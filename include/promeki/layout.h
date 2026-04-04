/**
 * @file      layout.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/rect.h>
#include <promeki/size2d.h>
#include <promeki/list.h>
#include <promeki/map.h>

PROMEKI_NAMESPACE_BEGIN

class Widget;

/**
 * @brief Abstract base class for layout managers.
 * @ingroup widget
 *
 * A layout positions child widgets within the available rectangle.
 * Layouts can be nested by adding sub-layouts.  The layout system
 * is backend-agnostic — it operates purely on geometry and size
 * policies, making it usable by both TUI and SDL (or any other
 * UI backend).
 */
class Layout : public ObjectBase {
        PROMEKI_OBJECT(Layout, ObjectBase)
        public:
                Layout(ObjectBase *parent = nullptr);
                virtual ~Layout();

                /** @brief Adds a widget to this layout. */
                virtual void addWidget(Widget *widget);

                /** @brief Removes a widget from this layout. */
                void removeWidget(Widget *widget);

                /** @brief Adds a nested layout. */
                virtual void addLayout(Layout *layout);

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
                const List<Widget *> &widgets() const { return _widgets; }

                /** @brief Returns the list of sub-layouts. */
                const List<Layout *> &layouts() const { return _layouts; }

        protected:
                /**
                 * @brief Returns the content rect after subtracting margins.
                 */
                Rect2Di32 contentRect(const Rect2Di32 &available) const;

                List<Widget *>          _widgets;
                List<Layout *>          _layouts;

        private:
                int     _spacing = 0;
                int     _marginTop = 0;
                int     _marginRight = 0;
                int     _marginBottom = 0;
                int     _marginLeft = 0;
};

/**
 * @brief Box layout direction.
 * @ingroup widget
 */
enum BoxDirection {
        LeftToRight,
        RightToLeft,
        TopToBottom,
        BottomToTop
};

/**
 * @brief Layout that arranges widgets in a horizontal or vertical line.
 * @ingroup widget
 *
 * Items are added in order via addWidget(), addLayout(), or addStretch().
 * All three are placed into a single ordered list so that sub-layouts
 * participate in size negotiation and positioning just like widgets.
 */
class BoxLayout : public Layout {
        PROMEKI_OBJECT(BoxLayout, Layout)
        public:
                /**
                 * @brief Constructs a BoxLayout.
                 * @param direction The layout direction.
                 * @param parent Optional parent.
                 */
                BoxLayout(BoxDirection direction, ObjectBase *parent = nullptr);

                /** @brief Returns the direction. */
                BoxDirection direction() const { return _direction; }

                /** @brief Adds a widget to the ordered item list. */
                void addWidget(Widget *widget) override;

                /** @brief Adds a sub-layout to the ordered item list. */
                void addLayout(Layout *layout) override;

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
                        Widget          *widget = nullptr;
                        Layout          *layout = nullptr;
                        int             stretchFactor = 0;
                };

                BoxDirection            _direction;
                List<Item>              _items;
};

/**
 * @brief Horizontal box layout (left to right).
 * @ingroup widget
 */
class HBoxLayout : public BoxLayout {
        PROMEKI_OBJECT(HBoxLayout, BoxLayout)
        public:
                HBoxLayout(ObjectBase *parent = nullptr)
                        : BoxLayout(LeftToRight, parent) {}
};

/**
 * @brief Vertical box layout (top to bottom).
 * @ingroup widget
 */
class VBoxLayout : public BoxLayout {
        PROMEKI_OBJECT(VBoxLayout, BoxLayout)
        public:
                VBoxLayout(ObjectBase *parent = nullptr)
                        : BoxLayout(TopToBottom, parent) {}
};

/**
 * @brief Grid layout with rows and columns.
 * @ingroup widget
 */
class GridLayout : public Layout {
        PROMEKI_OBJECT(GridLayout, Layout)
        public:
                GridLayout(ObjectBase *parent = nullptr);

                /**
                 * @brief Adds a widget at the given row/column with optional span.
                 */
                void addWidget(Widget *widget, int row, int col,
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
                        Widget          *widget;
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
