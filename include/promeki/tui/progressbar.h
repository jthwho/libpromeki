/**
 * @file      tui/progressbar.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/tui/widget.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Horizontal progress indicator.
 */
class TuiProgressBar : public TuiWidget {
        PROMEKI_OBJECT(TuiProgressBar, TuiWidget)
        public:
                TuiProgressBar(ObjectBase *parent = nullptr);
                ~TuiProgressBar() override;

                void setValue(int value);
                int value() const { return _value; }

                void setRange(int min, int max);
                int minimum() const { return _min; }
                int maximum() const { return _max; }

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(TuiPaintEvent *e) override;

        private:
                int _value = 0;
                int _min = 0;
                int _max = 100;
};

PROMEKI_NAMESPACE_END
