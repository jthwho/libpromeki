/**
 * @file      scrollarea.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Container that scrolls content larger than its viewport.
 */
class TuiScrollArea : public TuiWidget {
        PROMEKI_OBJECT(TuiScrollArea, TuiWidget)
        public:
                TuiScrollArea(ObjectBase *parent = nullptr);
                ~TuiScrollArea() override;

                void setContentWidget(TuiWidget *widget);
                TuiWidget *contentWidget() const { return _contentWidget; }

                int scrollX() const { return _scrollX; }
                int scrollY() const { return _scrollY; }

                void setScrollX(int val);
                void setScrollY(int val);
                void scrollTo(int x, int y);

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(TuiPaintEvent *e) override;
                void keyEvent(KeyEvent *e) override;
                void resizeEvent(TuiResizeEvent *e) override;

        private:
                TuiWidget       *_contentWidget = nullptr;
                int             _scrollX = 0;
                int             _scrollY = 0;

                void clampScroll();
};

PROMEKI_NAMESPACE_END
