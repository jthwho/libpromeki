/**
 * @file      inputdiag.cpp
 * @brief     Diagnostic: shows parsed TUI input events on screen
 */

#include <promeki/tui/tuisubsystem.h>
#include <promeki/tui/widget.h>
#include <promeki/tui/label.h>
#include <promeki/tui/button.h>
#include <promeki/tui/layout.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/statusbar.h>

using namespace promeki;

class DiagWidget : public TuiWidget {
        PROMEKI_OBJECT(DiagWidget, TuiWidget)
        public:
                DiagWidget(ObjectBase *parent = nullptr) : TuiWidget(parent) {
                        setFocusPolicy(StrongFocus);
                }

                void addLine(const String &line) {
                        _lines += line;
                        if(static_cast<int>(_lines.size()) > 100) {
                                _lines.remove(0);
                        }
                        update();
                }

        protected:
                void paintEvent(PaintEvent *) override {
                        TuiSubsystem *app = TuiSubsystem::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);

                        painter.setForeground(Color::White);
                        painter.setBackground(Color::Black);
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));

                        // Show last N lines that fit
                        int startLine = std::max(0, static_cast<int>(_lines.size()) - height());
                        for(int row = 0; row < height(); ++row) {
                                int idx = startLine + row;
                                if(idx >= static_cast<int>(_lines.size())) break;
                                String display = _lines[idx];
                                if(static_cast<int>(display.length()) > width()) {
                                        display = display.substr(0, width());
                                }
                                painter.drawText(0, row, display);
                        }
                }

                void keyEvent(KeyEvent *e) override {
                        String msg = "KEY: code=" + String::number(static_cast<int>(e->key()));
                        msg += " mod=0x" + String::number(e->modifiers(), 16);
                        if(!e->text().isEmpty()) {
                                msg += " text=\"" + e->text() + "\"";
                        }

                        switch(e->key()) {
                                case KeyEvent::Key_Tab: msg += " [TAB]"; break;
                                case KeyEvent::Key_Enter: msg += " [ENTER]"; break;
                                case KeyEvent::Key_Escape: msg += " [ESC]"; break;
                                case KeyEvent::Key_Up: msg += " [UP]"; break;
                                case KeyEvent::Key_Down: msg += " [DOWN]"; break;
                                case KeyEvent::Key_Left: msg += " [LEFT]"; break;
                                case KeyEvent::Key_Right: msg += " [RIGHT]"; break;
                                case KeyEvent::Key_Backspace: msg += " [BKSP]"; break;
                                default: break;
                        }

                        addLine(msg);
                        e->accept();
                }

                void mouseEvent(MouseEvent *e) override {
                        String msg = "MOUSE: pos=(" + String::number(e->x()) +
                                "," + String::number(e->y()) + ")";
                        switch(e->action()) {
                                case MouseEvent::Press: msg += " PRESS"; break;
                                case MouseEvent::Release: msg += " RELEASE"; break;
                                case MouseEvent::Move: msg += " MOVE"; break;
                                case MouseEvent::ScrollUp: msg += " SCROLLUP"; break;
                                case MouseEvent::ScrollDown: msg += " SCROLLDN"; break;
                        }
                        addLine(msg);
                        e->accept();
                }

        private:
                StringList _lines;
};

class DiagRoot : public TuiWidget {
        PROMEKI_OBJECT(DiagRoot, TuiWidget)
        public:
                DiagRoot(ObjectBase *parent = nullptr) : TuiWidget(parent) {
                        _layout = new TuiVBoxLayout(this);
                        _layout->setSpacing(0);

                        _title = new TuiLabel("Input Diagnostic - press keys (Ctrl+Q to quit)", this);
                        _layout->addWidget(_title);

                        _diag = new DiagWidget(this);
                        _diag->setSizePolicy(SizeExpanding);
                        _layout->addWidget(_diag);

                        setLayout(_layout);
                }

                DiagWidget *diag() const { return _diag; }

        protected:
                void paintEvent(PaintEvent *) override {
                        TuiSubsystem *app = TuiSubsystem::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);
                        painter.setForeground(Color::White);
                        painter.setBackground(Color::Black);
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));
                }

                void resizeEvent(ResizeEvent *) override {
                        if(_layout) _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
                }

        private:
                TuiVBoxLayout   *_layout;
                TuiLabel        *_title;
                DiagWidget      *_diag;
};

int main(int argc, char **argv) {
        Application  app(argc, argv);
        TuiSubsystem tui;

        DiagRoot root;
        tui.setRootWidget(&root);
        tui.setFocusWidget(root.diag());

        return app.exec();
}
