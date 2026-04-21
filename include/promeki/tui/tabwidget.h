/**
 * @file      tui/tabwidget.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Tabbed container with keyboard-switchable tabs.
 * @ingroup tui_widgets
 *
 */
class TuiTabWidget : public TuiWidget {
        PROMEKI_OBJECT(TuiTabWidget, TuiWidget)
        public:
                TuiTabWidget(ObjectBase *parent = nullptr);
                ~TuiTabWidget() override;

                void addTab(TuiWidget *widget, const String &title);
                void removeTab(int index);

                int currentIndex() const { return _currentIndex; }
                void setCurrentIndex(int index);

                TuiWidget *currentWidget() const;
                int count() const { return static_cast<int>(_tabs.size()); }

                Size2Di32 sizeHint() const override;

                PROMEKI_SIGNAL(currentChanged, int)

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void keyPressEvent(KeyEvent *e) override;
                void mouseEvent(MouseEvent *e) override;
                void resizeEvent(TuiResizeEvent *e) override;
                void focusInEvent(Event *e) override;
                void focusOutEvent(Event *e) override;

        private:
                struct Tab {
                        TuiWidget       *widget;
                        String          title;
                };
                struct TabPos {
                        int startX;
                        int endX;
                };
                List<Tab>       _tabs;
                List<TabPos>    _tabPositions;
                int             _currentIndex = -1;

                void updateTabGeometry();
};

PROMEKI_NAMESPACE_END
