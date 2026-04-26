/**
 * @file      tui/splitter.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Resizable split between two child widgets.
 * @ingroup tui_widgets
 *
 * @par Thread Safety
 * Thread-affine — see @ref TuiWidget.
 */
class TuiSplitter : public TuiWidget {
        PROMEKI_OBJECT(TuiSplitter, TuiWidget)
        public:
                enum Orientation { Horizontal, Vertical };

                TuiSplitter(Orientation orientation = Horizontal,
                            ObjectBase *parent = nullptr);
                ~TuiSplitter() override;

                void setFirstWidget(TuiWidget *widget);
                void setSecondWidget(TuiWidget *widget);

                TuiWidget *firstWidget() const { return _first; }
                TuiWidget *secondWidget() const { return _second; }

                /** @brief Sets the split position (0.0 to 1.0). */
                void setSplitRatio(double ratio);
                double splitRatio() const { return _splitRatio; }

                Orientation orientation() const { return _orientation; }

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(PaintEvent *e) override;
                void resizeEvent(ResizeEvent *e) override;
                void keyPressEvent(KeyEvent *e) override;
                void mouseEvent(MouseEvent *e) override;

        private:
                Orientation     _orientation;
                TuiWidget       *_first = nullptr;
                TuiWidget       *_second = nullptr;
                double          _splitRatio = 0.5;
                bool            _dragging = false;

                void updateChildGeometry();
};

PROMEKI_NAMESPACE_END
