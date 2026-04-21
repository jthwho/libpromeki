/**
 * @file      widget.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/rect.h>
#include <promeki/size2d.h>
#include <promeki/point.h>
#include <promeki/event.h>

PROMEKI_NAMESPACE_BEGIN

class Layout;
class KeyEvent;
class MouseEvent;

/**
 * @brief Event delivered when a widget needs to repaint.
 * @ingroup widget
 */
class PaintEvent : public Event {
        public:
                /** @brief Event type ID for PaintEvent. */
                static const Type Paint;

                /** @brief Constructs a PaintEvent. */
                PaintEvent() : Event(Paint) {}
};

/**
 * @brief Event delivered when a widget is resized.
 * @ingroup widget
 */
class ResizeEvent : public Event {
        public:
                /** @brief Event type ID for ResizeEvent. */
                static const Type Resize;

                /**
                 * @brief Constructs a ResizeEvent.
                 * @param size The new widget size.
                 */
                ResizeEvent(const Size2Di32 &size)
                        : Event(Resize), _size(size) {}

                /** @brief Returns the new size. */
                const Size2Di32 &size() const { return _size; }

        private:
                Size2Di32 _size;
};

/**
 * @brief Focus policy for widgets.
 * @ingroup widget
 */
enum FocusPolicy {
        NoFocus,        ///< Widget cannot receive focus.
        TabFocus,       ///< Widget can receive focus via Tab or equivalent.
        ClickFocus,     ///< Widget can receive focus via direct activation.
        StrongFocus     ///< Widget can receive focus via Tab or direct activation.
};

/**
 * @brief Size policy for widget layout negotiation.
 * @ingroup widget
 */
enum SizePolicy {
        SizeFixed,              ///< Fixed size, does not grow or shrink.
        SizeMinimum,            ///< Can grow, prefers minimum size.
        SizeMaximum,            ///< Can shrink, prefers maximum size.
        SizePreferred,          ///< Prefers sizeHint(), can grow or shrink.
        SizeExpanding,          ///< Takes all available space.
        SizeMinimumExpanding    ///< Prefers minimum but takes extra space.
};

/**
 * @brief Base class for all widgets across UI backends.
 * @ingroup widget
 *
 * Widget provides the generic infrastructure shared by all UI
 * backends (TUI, SDL, etc.): geometry management, visibility,
 * enabled state, focus handling, size policy negotiation, dirty
 * tracking, layout ownership, coordinate mapping, and virtual
 * methods for event dispatch.
 *
 * Painting is intentionally NOT part of this class — it is
 * fundamentally different between backends (character cells vs.
 * pixels).  Each backend provides its own painter and paint event
 * dispatch.
 *
 * Derives from ObjectBase to inherit signals/slots, parent/child
 * relationships, event dispatch, and timer support.
 */
class Widget : public ObjectBase {
        PROMEKI_OBJECT(Widget, ObjectBase)
        public:
                /**
                 * @brief Constructs a Widget.
                 * @param parent Optional parent widget.
                 */
                Widget(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~Widget();

                /** @brief Returns the geometry relative to parent. */
                const Rect2Di32 &geometry() const { return _geometry; }

                /** @brief Sets the geometry. Emits resized signal if size changed. */
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
                SizePolicy sizePolicy() const { return _sizePolicy; }

                /** @brief Sets the size policy. */
                void setSizePolicy(SizePolicy policy) { _sizePolicy = policy; }

                /** @brief Returns true if the widget's own visibility flag is set. */
                bool isVisible() const { return _visible; }

                /**
                 * @brief Returns true if the widget and all its ancestors are visible.
                 *
                 * A widget is effectively visible only if its own visibility flag
                 * is set AND every Widget ancestor is also visible.
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
                FocusPolicy focusPolicy() const { return _focusPolicy; }

                /** @brief Sets the focus policy. */
                void setFocusPolicy(FocusPolicy policy) { _focusPolicy = policy; }

                /** @brief Returns true if this widget has focus. */
                bool hasFocus() const { return _focused; }

                /** @brief Requests focus for this widget. */
                void setFocus();

                /**
                 * @brief Directly sets or clears the focused state.
                 *
                 * Used by application classes to manage focus across widgets.
                 * Does NOT call focusInEvent/focusOutEvent — the caller is
                 * responsible for dispatching those.
                 *
                 * @param focused The new focus state.
                 */
                void setFocused(bool focused) { _focused = focused; }

                /**
                 * @brief Marks the widget as needing a repaint.
                 *
                 * The base implementation sets the dirty flag.  Backend
                 * subclasses (e.g. TuiWidget) override this to also notify
                 * their application of the repaint need.
                 */
                virtual void update();

                /** @brief Returns true if the widget needs repainting. */
                bool isDirty() const { return _dirty; }

                /** @brief Clears the dirty flag. */
                void clearDirty() { _dirty = false; }

                /** @brief Returns the layout, if one is set. */
                Layout *layout() const { return _layout; }

                /** @brief Sets the layout for this widget. */
                void setLayout(Layout *layout);

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

                /**
                 * @brief Sends an event to this widget for processing.
                 *
                 * Public interface for delivering events to a widget
                 * from outside its class hierarchy (e.g. from a window
                 * or application).  Calls the protected event() method.
                 *
                 * @param e The event to deliver.
                 */
                void sendEvent(Event *e) { event(e); }

                /** @brief Signal emitted when the widget is resized. */
                PROMEKI_SIGNAL(resized, Size2Di32)

                /** @brief Signal emitted when visibility changes. */
                PROMEKI_SIGNAL(visibilityChanged, bool)

        protected:
                /** @brief Called to paint the widget. Override in subclasses. */
                virtual void paintEvent(PaintEvent *e);

                /** @brief Called on key-down events. Override in subclasses. */
                virtual void keyPressEvent(KeyEvent *e);

                /** @brief Called on key-up events. Override in subclasses. */
                virtual void keyReleaseEvent(KeyEvent *e);

                /** @brief Called on mouse input. Override in subclasses. */
                virtual void mouseEvent(MouseEvent *e);

                /** @brief Called when the widget is resized. Override in subclasses. */
                virtual void resizeEvent(ResizeEvent *e);

                /** @brief Called when the widget gains focus. */
                virtual void focusInEvent(Event *e);

                /** @brief Called when the widget loses focus. */
                virtual void focusOutEvent(Event *e);

                /** @brief Event dispatch override. */
                void event(Event *e) override;

        private:
                friend class Layout;

                Rect2Di32       _geometry;
                Size2Di32       _minimumSize;
                Size2Di32       _maximumSize = Size2Di32(9999, 9999);
                SizePolicy      _sizePolicy = SizePreferred;
                FocusPolicy     _focusPolicy = NoFocus;
                bool            _visible = true;
                bool            _enabled = true;
                bool            _focused = false;
                bool            _dirty = true;
                Layout          *_layout = nullptr;
};

PROMEKI_NAMESPACE_END
