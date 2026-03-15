/**
 * @file      tui/widget.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/rect.h>
#include <promeki/core/size2d.h>
#include <promeki/core/color.h>

PROMEKI_NAMESPACE_BEGIN

class TuiLayout;
class TuiPainter;
class KeyEvent;
class MouseEvent;

/**
 * @brief Event delivered when a TUI widget needs to repaint.
 */
class TuiPaintEvent : public Event {
        public:
                static const Type Paint;
                TuiPaintEvent() : Event(Paint) {}
};

/**
 * @brief Event delivered when a TUI widget is resized.
 */
class TuiResizeEvent : public Event {
        public:
                static const Type Resize;
                TuiResizeEvent(const Size2Di32 &size)
                        : Event(Resize), _size(size) {}
                const Size2Di32 &size() const { return _size; }
        private:
                Size2Di32 _size;
};

/**
 * @brief Focus policy for TUI widgets.
 */
enum TuiFocusPolicy {
        NoFocus,        ///< Widget cannot receive focus.
        TabFocus,       ///< Widget can receive focus via Tab.
        ClickFocus,     ///< Widget can receive focus via mouse click.
        StrongFocus     ///< Widget can receive focus via Tab or mouse click.
};

/**
 * @brief Size policy for TUI widget layout.
 */
enum TuiSizePolicy {
        SizeFixed,              ///< Fixed size, does not grow or shrink.
        SizeMinimum,            ///< Can grow, prefers minimum size.
        SizeMaximum,            ///< Can shrink, prefers maximum size.
        SizePreferred,          ///< Prefers sizeHint(), can grow or shrink.
        SizeExpanding,          ///< Takes all available space.
        SizeMinimumExpanding    ///< Prefers minimum but takes extra space.
};

/**
 * @brief Base class for all TUI widgets.
 *
 * Derives from ObjectBase to inherit signals/slots, parent/child
 * relationships, event dispatch, and timer support.  Provides geometry
 * management, focus handling, visibility control, and virtual methods
 * for paint/key/mouse/resize events.
 */
class TuiWidget : public ObjectBase {
        PROMEKI_OBJECT(TuiWidget, ObjectBase)
        public:
                /**
                 * @brief Constructs a TuiWidget.
                 * @param parent Optional parent widget.
                 */
                TuiWidget(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~TuiWidget();

                /** @brief Returns the geometry relative to parent. */
                const Rect2Di32 &geometry() const { return _geometry; }

                /** @brief Sets the geometry. */
                void setGeometry(const Rect2Di32 &rect);

                /** @brief Returns the X coordinate relative to parent. */
                int x() const { return _geometry.x(); }

                /** @brief Returns the Y coordinate relative to parent. */
                int y() const { return _geometry.y(); }

                /** @brief Returns the width. */
                int width() const { return _geometry.width(); }

                /** @brief Returns the height. */
                int height() const { return _geometry.height(); }

                /** @brief Returns the size. */
                Size2Di32 size() const {
                        return Size2Di32(_geometry.width(), _geometry.height());
                }

                /** @brief Returns the minimum size constraint. */
                const Size2Di32 &minimumSize() const { return _minimumSize; }

                /** @brief Sets the minimum size constraint. */
                void setMinimumSize(const Size2Di32 &size) { _minimumSize = size; }

                /** @brief Returns the maximum size constraint. */
                const Size2Di32 &maximumSize() const { return _maximumSize; }

                /** @brief Sets the maximum size constraint. */
                void setMaximumSize(const Size2Di32 &size) { _maximumSize = size; }

                /** @brief Returns the size policy. */
                TuiSizePolicy sizePolicy() const { return _sizePolicy; }

                /** @brief Sets the size policy. */
                void setSizePolicy(TuiSizePolicy policy) { _sizePolicy = policy; }

                /** @brief Returns true if the widget's own visibility flag is set. */
                bool isVisible() const { return _visible; }

                /**
                 * @brief Returns true if the widget and all its ancestors are visible.
                 *
                 * A widget is effectively visible only if its own visibility flag
                 * is set AND every TuiWidget ancestor is also visible.
                 */
                bool isEffectivelyVisible() const;

                /** @brief Shows the widget. */
                void show();

                /** @brief Hides the widget. */
                void hide();

                /** @brief Sets visibility. */
                void setVisible(bool visible);

                /** @brief Returns true if the widget is enabled. */
                bool isEnabled() const { return _enabled; }

                /** @brief Sets enabled state. */
                void setEnabled(bool enabled) { _enabled = enabled; }

                /** @brief Returns the focus policy. */
                TuiFocusPolicy focusPolicy() const { return _focusPolicy; }

                /** @brief Sets the focus policy. */
                void setFocusPolicy(TuiFocusPolicy policy) { _focusPolicy = policy; }

                /** @brief Returns true if this widget has focus. */
                bool hasFocus() const { return _focused; }

                /** @brief Requests focus for this widget. */
                void setFocus();

                /** @brief Marks the widget as needing a repaint. */
                void update();

                /** @brief Returns true if the widget needs repainting. */
                bool isDirty() const { return _dirty; }

                /** @brief Clears the dirty flag. */
                void clearDirty() { _dirty = false; }

                /** @brief Returns the layout, if one is set. */
                TuiLayout *layout() const { return _layout; }

                /** @brief Sets the layout for this widget. */
                void setLayout(TuiLayout *layout);

                /**
                 * @brief Maps a local point to the parent's coordinate system.
                 */
                Point2Di32 mapToParent(const Point2Di32 &p) const;

                /**
                 * @brief Maps a parent point to local coordinates.
                 */
                Point2Di32 mapFromParent(const Point2Di32 &p) const;

                /**
                 * @brief Maps a local point to global (screen) coordinates.
                 */
                Point2Di32 mapToGlobal(const Point2Di32 &p) const;

                /**
                 * @brief Maps a global (screen) point to local coordinates.
                 */
                Point2Di32 mapFromGlobal(const Point2Di32 &p) const;

                /** @brief Returns the preferred size. Override in subclasses. */
                virtual Size2Di32 sizeHint() const;

                /** @brief Returns the minimum size hint. Override in subclasses. */
                virtual Size2Di32 minimumSizeHint() const;

                // Signals
                PROMEKI_SIGNAL(resized, Size2Di32)
                PROMEKI_SIGNAL(visibilityChanged, bool)

        protected:
                /** @brief Called to paint the widget. Override in subclasses. */
                virtual void paintEvent(TuiPaintEvent *e);

                /** @brief Called on keyboard input. Override in subclasses. */
                virtual void keyEvent(KeyEvent *e);

                /** @brief Called on mouse input. Override in subclasses. */
                virtual void mouseEvent(MouseEvent *e);

                /** @brief Called when the widget is resized. Override in subclasses. */
                virtual void resizeEvent(TuiResizeEvent *e);

                /** @brief Called when the widget gains focus. */
                virtual void focusInEvent(Event *e);

                /** @brief Called when the widget loses focus. */
                virtual void focusOutEvent(Event *e);

                /** @brief Event dispatch override. */
                void event(Event *e) override;

        private:
                friend class TuiApplication;

                Rect2Di32                  _geometry;
                Size2Di32     _minimumSize;
                Size2Di32     _maximumSize = Size2Di32(9999, 9999);
                TuiSizePolicy           _sizePolicy = SizePreferred;
                TuiFocusPolicy          _focusPolicy = NoFocus;
                bool                    _visible = true;
                bool                    _enabled = true;
                bool                    _focused = false;
                bool                    _dirty = true;
                TuiLayout               *_layout = nullptr;
};

PROMEKI_NAMESPACE_END
