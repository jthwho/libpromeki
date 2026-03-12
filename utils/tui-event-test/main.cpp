/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/application.h>
#include <promeki/tui/widget.h>
#include <promeki/tui/label.h>
#include <promeki/tui/layout.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/frame.h>
#include <promeki/tui/listview.h>
#include <promeki/tui/statusbar.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>
#include <promeki/datetime.h>

using namespace promeki;

class EventTestWidget : public TuiWidget {
        PROMEKI_OBJECT(EventTestWidget, TuiWidget)
        public:
                EventTestWidget(ObjectBase *parent = nullptr) : TuiWidget(parent) {
                        setFocusPolicy(StrongFocus);
                        setFocus();

                        _layout = new TuiVBoxLayout(this);

                        _titleLabel = new TuiLabel("Event Loop / Input Test", this);
                        _titleLabel->setAlignment(AlignCenter);
                        _layout->addWidget(_titleLabel);

                        // Horizontal layout for keyboard and mouse frames side by side
                        _infoRow = new TuiHBoxLayout(this);

                        // Keyboard info frame
                        _keyFrame = new TuiFrame("Keyboard", this);
                        auto *keyLayout = new TuiVBoxLayout(this);
                        keyLayout->setSpacing(0);

                        _keyNameLabel = new TuiLabel("Key: --", this);
                        keyLayout->addWidget(_keyNameLabel);

                        _keyModLabel = new TuiLabel("Modifiers: None", this);
                        keyLayout->addWidget(_keyModLabel);

                        _keyTextLabel = new TuiLabel("Text: --", this);
                        keyLayout->addWidget(_keyTextLabel);

                        _keyCodeLabel = new TuiLabel("Code: --", this);
                        keyLayout->addWidget(_keyCodeLabel);

                        _keyFrame->setLayout(keyLayout);
                        _keyFrame->setSizePolicy(SizeExpanding);
                        _infoRow->addWidget(_keyFrame);

                        // Mouse info frame
                        _mouseFrame = new TuiFrame("Mouse", this);
                        auto *mouseLayout = new TuiVBoxLayout(this);
                        mouseLayout->setSpacing(0);

                        _mousePosLabel = new TuiLabel("Position: --, --", this);
                        mouseLayout->addWidget(_mousePosLabel);

                        _mouseButtonLabel = new TuiLabel("Button: None", this);
                        mouseLayout->addWidget(_mouseButtonLabel);

                        _mouseActionLabel = new TuiLabel("Action: --", this);
                        mouseLayout->addWidget(_mouseActionLabel);

                        _mouseModLabel = new TuiLabel("Modifiers: None", this);
                        mouseLayout->addWidget(_mouseModLabel);

                        _mouseButtonsLabel = new TuiLabel("Buttons: None", this);
                        mouseLayout->addWidget(_mouseButtonsLabel);

                        _mouseFrame->setLayout(mouseLayout);
                        _mouseFrame->setSizePolicy(SizeExpanding);
                        _infoRow->addWidget(_mouseFrame);

                        // Wrap the HBox in a fixed-height widget placeholder.
                        // The VBox layout needs a widget, not a sub-layout, so we
                        // create a container widget that holds the HBox.
                        _infoContainer = new TuiWidget(this);
                        _infoContainer->setLayout(_infoRow);
                        _layout->addWidget(_infoContainer);

                        // Event log frame with scrollable list
                        _logFrame = new TuiFrame("Event Log", this);
                        _logList = new TuiListView(this);
                        _logList->setFocusPolicy(NoFocus);
                        _logList->setSizePolicy(SizeExpanding);
                        auto *logLayout = new TuiVBoxLayout(this);
                        logLayout->addWidget(_logList);
                        _logFrame->setLayout(logLayout);
                        _logFrame->setSizePolicy(SizeExpanding);
                        _layout->addWidget(_logFrame);

                        // Status bar
                        _statusBar = new TuiStatusBar(this);
                        _statusBar->setPermanentMessage("Ctrl+Q: quit | Scroll: mouse wheel or PgUp/PgDn");
                        _layout->addWidget(_statusBar);

                        setLayout(_layout);

                        // Start a timer to show event loop is running
                        _tickTimerId = startTimer(1000);
                }

        protected:
                void paintEvent(TuiPaintEvent *) override {
                        TuiApplication *app = TuiApplication::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);
                        const TuiPalette &pal = app->palette();
                        painter.setForeground(pal.color(TuiPalette::WindowText, false, isEnabled()));
                        painter.setBackground(pal.color(TuiPalette::Window, false, isEnabled()));
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));
                }

                void resizeEvent(TuiResizeEvent *) override {
                        if(_layout) _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
                }

                void keyEvent(KeyEvent *e) override {
                        // Let scroll keys navigate the log list
                        if(e->key() == KeyEvent::Key_PageUp) {
                                _logList->scrollBy(-_logList->height());
                                _autoScroll = (_logList->currentIndex() == 0);
                        } else if(e->key() == KeyEvent::Key_PageDown) {
                                _logList->scrollBy(_logList->height());
                                _autoScroll = (_logList->currentIndex() == 0);
                        }

                        _keyNameLabel->setText(String("Key: ") + KeyEvent::keyName(e->key()));
                        String mods = KeyEvent::modifierString(e->modifiers());
                        _keyModLabel->setText(String("Modifiers: ") + (mods.isEmpty() ? "None" : mods));
                        _keyTextLabel->setText(String("Text: \"") + e->text() + "\"");
                        _keyCodeLabel->setText(String("Code: ") + String::number(static_cast<int>(e->key())));

                        String logEntry = "KEY " + KeyEvent::modifierString(e->modifiers()) + KeyEvent::keyName(e->key());
                        if(!e->text().isEmpty()) {
                                logEntry += " (\"" + e->text() + "\")";
                        }
                        addLog(logEntry);

                        TuiWidget::keyEvent(e);
                }

                void mouseEvent(MouseEvent *e) override {
                        _mousePosLabel->setText(
                                String("Position: ") +
                                String::number(e->x()) + ", " +
                                String::number(e->y()));
                        _mouseButtonLabel->setText(
                                String("Button: ") + MouseEvent::buttonName(e->button()));
                        _mouseActionLabel->setText(
                                String("Action: ") + MouseEvent::actionName(e->action()));
                        String mods = KeyEvent::modifierString(e->modifiers());
                        _mouseModLabel->setText(
                                String("Modifiers: ") + (mods.isEmpty() ? "None" : mods));
                        _mouseButtonsLabel->setText(
                                String("Buttons: ") + MouseEvent::buttonsString(e->buttons()));

                        // Let scroll events navigate the log list
                        if(e->action() == MouseEvent::ScrollUp) {
                                _logList->scrollBy(-1);
                                _autoScroll = (_logList->currentIndex() == 0);
                        } else if(e->action() == MouseEvent::ScrollDown) {
                                _logList->scrollBy(1);
                                _autoScroll = (_logList->currentIndex() == 0);
                        }

                        // Only log presses, releases, and scrolls (not every move)
                        if(e->action() != MouseEvent::Move) {
                                String logEntry = "MOUSE " +
                                        MouseEvent::actionName(e->action()) + " " +
                                        MouseEvent::buttonName(e->button()) +
                                        " at (" + String::number(e->x()) +
                                        "," + String::number(e->y()) + ")";
                                if(e->modifiers() != MouseEvent::NoModifier) {
                                        logEntry += " " + KeyEvent::modifierString(e->modifiers());
                                }
                                addLog(logEntry);
                        }

                        TuiWidget::mouseEvent(e);
                }

                void timerEvent(TimerEvent *e) override {
                        _tickCount++;
                        _statusBar->setPermanentMessage(
                                String("Tick: ") + String::number(_tickCount) +
                                " | Events: " + String::number(_logList->count()) +
                                " | Ctrl+Q: quit | Scroll: wheel/PgUp/PgDn");
                        TuiWidget::timerEvent(e);
                }

        private:
                TuiVBoxLayout   *_layout = nullptr;
                TuiLabel        *_titleLabel = nullptr;

                TuiHBoxLayout   *_infoRow = nullptr;
                TuiWidget       *_infoContainer = nullptr;

                TuiFrame        *_keyFrame = nullptr;
                TuiLabel        *_keyNameLabel = nullptr;
                TuiLabel        *_keyModLabel = nullptr;
                TuiLabel        *_keyTextLabel = nullptr;
                TuiLabel        *_keyCodeLabel = nullptr;

                TuiFrame        *_mouseFrame = nullptr;
                TuiLabel        *_mousePosLabel = nullptr;
                TuiLabel        *_mouseButtonLabel = nullptr;
                TuiLabel        *_mouseActionLabel = nullptr;
                TuiLabel        *_mouseModLabel = nullptr;
                TuiLabel        *_mouseButtonsLabel = nullptr;

                TuiFrame        *_logFrame = nullptr;
                TuiListView     *_logList = nullptr;

                TuiStatusBar    *_statusBar = nullptr;

                int             _tickTimerId = -1;
                int             _tickCount = 0;
                bool            _autoScroll = true;

                void addLog(const String &entry) {
                        String timestamped = DateTime::now().toString("%T.3") + " " + entry;
                        _logList->insertItem(0, timestamped);
                        if(_autoScroll) {
                                _logList->setCurrentIndex(0);
                        }
                }
};

int main(int argc, char **argv) {
        TuiApplication app(argc, argv);

        EventTestWidget root;
        app.setRootWidget(&root);

        return app.exec();
}
