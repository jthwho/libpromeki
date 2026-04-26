/**
 * @file      tui/menu.h
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
 * @brief A menu action item.
 * @ingroup tui_widgets
 *
 * @par Thread Safety
 * Thread-affine via @ref ObjectBase — must be created and used on the thread
 * that owns the parent menu.
 */
class TuiAction : public ObjectBase {
        PROMEKI_OBJECT(TuiAction, ObjectBase)
        public:
                TuiAction(const String &text, ObjectBase *parent = nullptr)
                        : ObjectBase(parent), _text(text) {}

                const String &text() const { return _text; }
                void setText(const String &text) { _text = text; }

                bool isEnabled() const { return _enabled; }
                void setEnabled(bool enabled) { _enabled = enabled; }

                PROMEKI_SIGNAL(triggered)

        private:
                String  _text;
                bool    _enabled = true;
};

/**
 * @brief Dropdown menu.
 *
 * @par Thread Safety
 * Thread-affine — see @ref TuiWidget.
 */
class TuiMenu : public TuiWidget {
        PROMEKI_OBJECT(TuiMenu, TuiWidget)
        public:
                TuiMenu(const String &title = String(), ObjectBase *parent = nullptr);
                ~TuiMenu() override;

                const String &title() const { return _title; }
                void setTitle(const String &title) { _title = title; update(); }

                TuiAction *addAction(const String &text);
                void addSeparator();

                const List<TuiAction *> &actions() const { return _actions; }

                int currentIndex() const { return _currentIndex; }
                void setCurrentIndex(int index);

                bool isOpen() const { return _open; }
                void open();
                void close();

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(PaintEvent *e) override;
                void keyPressEvent(KeyEvent *e) override;

        private:
                String                  _title;
                List<TuiAction *>       _actions;
                List<int>               _separators; // 1 = separator at this index
                int                     _currentIndex = 0;
                bool                    _open = false;
};

/**
 * @brief Top-of-screen menu bar with dropdown menus.
 *
 * @par Thread Safety
 * Thread-affine — see @ref TuiWidget.
 */
class TuiMenuBar : public TuiWidget {
        PROMEKI_OBJECT(TuiMenuBar, TuiWidget)
        public:
                TuiMenuBar(ObjectBase *parent = nullptr);
                ~TuiMenuBar() override;

                TuiMenu *addMenu(const String &title);
                const List<TuiMenu *> &menus() const { return _menus; }

                int currentIndex() const { return _currentIndex; }

                Size2Di32 sizeHint() const override;

        protected:
                void paintEvent(PaintEvent *e) override;
                void keyPressEvent(KeyEvent *e) override;

        private:
                List<TuiMenu *> _menus;
                int             _currentIndex = 0;
                bool            _active = false;
};

PROMEKI_NAMESPACE_END
