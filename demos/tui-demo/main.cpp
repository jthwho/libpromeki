/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/application.h>
#include <promeki/tui/widget.h>
#include <promeki/tui/label.h>
#include <promeki/tui/button.h>
#include <promeki/tui/checkbox.h>
#include <promeki/tui/layout.h>
#include <promeki/tui/painter.h>
#include <promeki/tui/palette.h>
#include <promeki/tui/frame.h>
#include <promeki/tui/statusbar.h>
#include <promeki/tui/lineedit.h>
#include <promeki/tui/textarea.h>
#include <promeki/tui/listview.h>
#include <promeki/tui/progressbar.h>
#include <promeki/tui/tabwidget.h>
#include <promeki/tui/splitter.h>
#include <promeki/timerevent.h>

using namespace promeki;

// ── Tab 1: Basic Widgets ─────────────────────────────────────────────

class BasicWidgetsTab : public TuiWidget {
        PROMEKI_OBJECT(BasicWidgetsTab, TuiWidget)
        public:
                BasicWidgetsTab(TuiStatusBar *statusBar, ObjectBase *parent = nullptr)
                        : TuiWidget(parent), _statusBar(statusBar) {
                        _layout = new TuiVBoxLayout(this);
                        _layout->setMargins(1);
                        _layout->setSpacing(1);

                        _titleLabel = new TuiLabel("Basic Widgets", this);
                        _titleLabel->setAlignment(AlignCenter);
                        _layout->addWidget(_titleLabel);

                        // Labels in a frame
                        _labelFrame = new TuiFrame("Labels", this);
                        auto *labelLayout = new TuiVBoxLayout(_labelFrame);
                        _leftLabel = new TuiLabel("Left-aligned label", _labelFrame);
                        _leftLabel->setAlignment(AlignLeft);
                        labelLayout->addWidget(_leftLabel);
                        _centerLabel = new TuiLabel("Center-aligned label", _labelFrame);
                        _centerLabel->setAlignment(AlignCenter);
                        labelLayout->addWidget(_centerLabel);
                        _rightLabel = new TuiLabel("Right-aligned label", _labelFrame);
                        _rightLabel->setAlignment(AlignRight);
                        labelLayout->addWidget(_rightLabel);
                        _labelFrame->setLayout(labelLayout);
                        _layout->addWidget(_labelFrame);

                        // Buttons
                        auto *buttonRow = new TuiHBoxLayout(this);
                        buttonRow->setSpacing(2);
                        _helloButton = new TuiButton("Hello", this);
                        buttonRow->addWidget(_helloButton);
                        _worldButton = new TuiButton("World", this);
                        buttonRow->addWidget(_worldButton);
                        _quitButton = new TuiButton("Quit", this);
                        buttonRow->addWidget(_quitButton);
                        buttonRow->addStretch();
                        _layout->addLayout(buttonRow);

                        // Checkboxes
                        auto *checkRow = new TuiHBoxLayout(this);
                        checkRow->setSpacing(2);
                        _optionA = new TuiCheckBox("Option A", this);
                        checkRow->addWidget(_optionA);
                        _optionB = new TuiCheckBox("Option B", this);
                        _optionB->setChecked(true);
                        checkRow->addWidget(_optionB);
                        _optionC = new TuiCheckBox("Option C", this);
                        checkRow->addWidget(_optionC);
                        checkRow->addStretch();
                        _layout->addLayout(checkRow);

                        // Progress bar
                        _progressLabel = new TuiLabel("Progress: 0%", this);
                        _layout->addWidget(_progressLabel);
                        _progressBar = new TuiProgressBar(this);
                        _progressBar->setRange(0, 100);
                        _progressBar->setValue(0);
                        _layout->addWidget(_progressBar);

                        _layout->addStretch();

                        setLayout(_layout);

                        ObjectBase::connect(
                                &_helloButton->clickedSignal,
                                &helloSlot
                        );
                        ObjectBase::connect(
                                &_worldButton->clickedSignal,
                                &worldSlot
                        );
                        ObjectBase::connect(
                                &_quitButton->clickedSignal,
                                &quitSlot
                        );
                        ObjectBase::connect(
                                &_optionA->toggledSignal,
                                &checkboxToggledSlot
                        );
                        ObjectBase::connect(
                                &_optionB->toggledSignal,
                                &checkboxToggledSlot
                        );
                        ObjectBase::connect(
                                &_optionC->toggledSignal,
                                &checkboxToggledSlot
                        );

                        // Auto-advance progress bar on a timer
                        _timerId = startTimer(200);
                }

                PROMEKI_SLOT(hello);
                PROMEKI_SLOT(world);
                PROMEKI_SLOT(quit);
                PROMEKI_SLOT(checkboxToggled, bool);

        protected:
                void timerEvent(TimerEvent *) override {
                        advanceProgress();
                }

                void paintEvent(TuiPaintEvent *) override {
                        TuiApplication *app = TuiApplication::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);
                        const TuiPalette &pal = app->palette();
                        painter.setStyle(pal.style(TuiPalette::WindowText, false, isEnabled())
                                .merged(pal.style(TuiPalette::Window, false, isEnabled())));
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));
                }

                void resizeEvent(TuiResizeEvent *) override {
                        if(_layout) _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
                }

        private:
                TuiStatusBar    *_statusBar;
                TuiVBoxLayout   *_layout;
                TuiLabel        *_titleLabel;
                TuiFrame        *_labelFrame;
                TuiLabel        *_leftLabel;
                TuiLabel        *_centerLabel;
                TuiLabel        *_rightLabel;
                TuiButton       *_helloButton;
                TuiButton       *_worldButton;
                TuiButton       *_quitButton;
                TuiCheckBox     *_optionA;
                TuiCheckBox     *_optionB;
                TuiCheckBox     *_optionC;
                TuiLabel        *_progressLabel;
                TuiProgressBar  *_progressBar;
                int             _progressValue = 0;
                int             _timerId = -1;

                void advanceProgress() {
                        _progressValue = (_progressValue + 1) % 101;
                        _progressBar->setValue(_progressValue);
                        _progressLabel->setText(
                                String("Progress: ") + String::number(_progressValue) + "%");
                }
};

void BasicWidgetsTab::hello() {
        _statusBar->showMessage("Hello button clicked!", 3000);
}

void BasicWidgetsTab::world() {
        _statusBar->showMessage("World button clicked!", 3000);
}

void BasicWidgetsTab::quit() {
        TuiApplication *app = TuiApplication::instance();
        if(app) app->quit(0);
}

void BasicWidgetsTab::checkboxToggled(bool checked) {
        String msg = String("Checkbox toggled: ") + (checked ? "checked" : "unchecked");
        _statusBar->showMessage(msg, 3000);
}

// ── Tab 2: Text Input ────────────────────────────────────────────────

class TextInputTab : public TuiWidget {
        PROMEKI_OBJECT(TextInputTab, TuiWidget)
        public:
                TextInputTab(TuiStatusBar *statusBar, ObjectBase *parent = nullptr)
                        : TuiWidget(parent), _statusBar(statusBar) {
                        _layout = new TuiVBoxLayout(this);
                        _layout->setMargins(1);
                        _layout->setSpacing(1);

                        _titleLabel = new TuiLabel("Text Input Widgets", this);
                        _titleLabel->setAlignment(AlignCenter);
                        _layout->addWidget(_titleLabel);

                        // Line edit
                        _lineEditLabel = new TuiLabel("Single-line input:", this);
                        _layout->addWidget(_lineEditLabel);

                        _lineEdit = new TuiLineEdit(String(), this);
                        _lineEdit->setPlaceholder("Type something here...");
                        _layout->addWidget(_lineEdit);

                        // Second line edit
                        _nameLabel = new TuiLabel("Name:", this);
                        _layout->addWidget(_nameLabel);

                        _nameEdit = new TuiLineEdit(String(), this);
                        _nameEdit->setPlaceholder("Enter your name");
                        _layout->addWidget(_nameEdit);

                        // Text area
                        _textAreaLabel = new TuiLabel("Multi-line editor:", this);
                        _layout->addWidget(_textAreaLabel);

                        _textArea = new TuiTextArea(this);
                        _textArea->setText("This is a multi-line text area.\n"
                                           "You can type and edit text here.\n"
                                           "Use arrow keys to navigate.\n"
                                           "Press Enter to add new lines.");
                        _textArea->setSizePolicy(SizeExpanding);
                        _layout->addWidget(_textArea);

                        setLayout(_layout);

                        ObjectBase::connect(
                                &_lineEdit->returnPressedSignal,
                                &lineEditReturnSlot
                        );
                }

                PROMEKI_SLOT(lineEditReturn);

        protected:
                void paintEvent(TuiPaintEvent *) override {
                        TuiApplication *app = TuiApplication::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);
                        const TuiPalette &pal = app->palette();
                        painter.setStyle(pal.style(TuiPalette::WindowText, false, isEnabled())
                                .merged(pal.style(TuiPalette::Window, false, isEnabled())));
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));
                }

                void resizeEvent(TuiResizeEvent *) override {
                        if(_layout) _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
                }

        private:
                TuiStatusBar    *_statusBar;
                TuiVBoxLayout   *_layout;
                TuiLabel        *_titleLabel;
                TuiLabel        *_lineEditLabel;
                TuiLineEdit     *_lineEdit;
                TuiLabel        *_nameLabel;
                TuiLineEdit     *_nameEdit;
                TuiLabel        *_textAreaLabel;
                TuiTextArea     *_textArea;
};

void TextInputTab::lineEditReturn() {
        _statusBar->showMessage(
                String("Input submitted: ") + _lineEdit->text(), 3000);
}

// ── Tab 3: List View ─────────────────────────────────────────────────

class ListViewTab : public TuiWidget {
        PROMEKI_OBJECT(ListViewTab, TuiWidget)
        public:
                ListViewTab(TuiStatusBar *statusBar, ObjectBase *parent = nullptr)
                        : TuiWidget(parent), _statusBar(statusBar) {
                        _layout = new TuiHBoxLayout(this);
                        _layout->setMargins(1);
                        _layout->setSpacing(2);

                        // Left: list view
                        auto *leftCol = new TuiVBoxLayout(this);
                        leftCol->setSpacing(1);

                        _listLabel = new TuiLabel("Select an item:", this);
                        leftCol->addWidget(_listLabel);

                        _listView = new TuiListView(this);
                        _listView->addItem("Apple");
                        _listView->addItem("Banana");
                        _listView->addItem("Cherry");
                        _listView->addItem("Date");
                        _listView->addItem("Elderberry");
                        _listView->addItem("Fig");
                        _listView->addItem("Grape");
                        _listView->addItem("Honeydew");
                        _listView->addItem("Kiwi");
                        _listView->addItem("Lemon");
                        _listView->addItem("Mango");
                        _listView->addItem("Nectarine");
                        _listView->addItem("Orange");
                        _listView->addItem("Papaya");
                        _listView->addItem("Quince");
                        _listView->setSizePolicy(SizeExpanding);
                        leftCol->addWidget(_listView);
                        _layout->addLayout(leftCol);

                        // Right: details
                        auto *rightCol = new TuiVBoxLayout(this);
                        rightCol->setSpacing(1);

                        _detailLabel = new TuiLabel("Details:", this);
                        rightCol->addWidget(_detailLabel);

                        _detailText = new TuiTextArea(this);
                        _detailText->setReadOnly(true);
                        _detailText->setText("Select a fruit from the list\n"
                                             "to see information about it.");
                        _detailText->setSizePolicy(SizeExpanding);
                        rightCol->addWidget(_detailText);
                        _layout->addLayout(rightCol);

                        setLayout(_layout);

                        ObjectBase::connect(
                                &_listView->currentItemChangedSignal,
                                &itemChangedSlot
                        );
                        ObjectBase::connect(
                                &_listView->itemActivatedSignal,
                                &itemActivatedSlot
                        );
                }

                PROMEKI_SLOT(itemChanged, int);
                PROMEKI_SLOT(itemActivated, int);

        protected:
                void paintEvent(TuiPaintEvent *) override {
                        TuiApplication *app = TuiApplication::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);
                        const TuiPalette &pal = app->palette();
                        painter.setStyle(pal.style(TuiPalette::WindowText, false, isEnabled())
                                .merged(pal.style(TuiPalette::Window, false, isEnabled())));
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));
                }

                void resizeEvent(TuiResizeEvent *) override {
                        if(_layout) _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
                }

        private:
                TuiStatusBar    *_statusBar;
                TuiHBoxLayout   *_layout;
                TuiLabel        *_listLabel;
                TuiListView     *_listView;
                TuiLabel        *_detailLabel;
                TuiTextArea     *_detailText;
};

void ListViewTab::itemChanged(int index) {
        String item = _listView->currentItem();
        _detailText->setText(
                String("Selected: ") + item + "\n"
                "Index: " + String::number(index) + "\n"
                "\nPress Enter to activate.");
}

void ListViewTab::itemActivated(int index) {
        String item = _listView->currentItem();
        _statusBar->showMessage(
                String("Activated: ") + item +
                " (index " + String::number(index) + ")", 3000);
}

// ── Tab 4: Splitter Demo ────────────────────────────────────────────

class SplitterTab : public TuiWidget {
        PROMEKI_OBJECT(SplitterTab, TuiWidget)
        public:
                SplitterTab(ObjectBase *parent = nullptr) : TuiWidget(parent) {
                        _splitter = new TuiSplitter(TuiSplitter::Vertical, this);

                        _topArea = new TuiTextArea(this);
                        _topArea->setText("Top pane of a vertical splitter.\n"
                                          "This is a TuiSplitter widget with\n"
                                          "Vertical orientation.\n"
                                          "\n"
                                          "You can type in both panes.");
                        _splitter->setFirstWidget(_topArea);

                        _bottomArea = new TuiTextArea(this);
                        _bottomArea->setText("Bottom pane of the splitter.\n"
                                             "Tab between panes to switch focus.\n"
                                             "\n"
                                             "The split ratio is set to 0.4.");
                        _splitter->setSecondWidget(_bottomArea);
                        _splitter->setSplitRatio(0.4);
                }

        protected:
                void paintEvent(TuiPaintEvent *) override {
                        TuiApplication *app = TuiApplication::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);
                        const TuiPalette &pal = app->palette();
                        painter.setStyle(pal.style(TuiPalette::WindowText, false, isEnabled())
                                .merged(pal.style(TuiPalette::Window, false, isEnabled())));
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));
                }

                void resizeEvent(TuiResizeEvent *) override {
                        if(_splitter) {
                                _splitter->setGeometry(Rect2Di32(0, 0, width(), height()));
                        }
                }

        private:
                TuiSplitter     *_splitter;
                TuiTextArea     *_topArea;
                TuiTextArea     *_bottomArea;
};

// ── Root Widget ──────────────────────────────────────────────────────

class DemoWidget : public TuiWidget {
        PROMEKI_OBJECT(DemoWidget, TuiWidget)
        public:
                DemoWidget(ObjectBase *parent = nullptr) : TuiWidget(parent) {
                        _layout = new TuiVBoxLayout(this);

                        // Status bar at the bottom
                        _statusBar = new TuiStatusBar(this);
                        _statusBar->setPermanentMessage(
                                "Tab: navigate | Ctrl+Left/Right: switch tabs | Ctrl+Q: quit");

                        // Tab widget fills most of the screen
                        _tabWidget = new TuiTabWidget(this);
                        _tabWidget->setSizePolicy(SizeExpanding);

                        _basicTab = new BasicWidgetsTab(_statusBar, this);
                        _tabWidget->addTab(_basicTab, "Basics");

                        _textTab = new TextInputTab(_statusBar, this);
                        _tabWidget->addTab(_textTab, "Text Input");

                        _listTab = new ListViewTab(_statusBar, this);
                        _tabWidget->addTab(_listTab, "List View");

                        _splitterTab = new SplitterTab(this);
                        _tabWidget->addTab(_splitterTab, "Splitter");

                        _layout->addWidget(_tabWidget);
                        _layout->addWidget(_statusBar);
                        setLayout(_layout);
                }

        protected:
                void paintEvent(TuiPaintEvent *) override {
                        TuiApplication *app = TuiApplication::instance();
                        if(!app) return;
                        Point2Di32 screenPos = mapToGlobal(Point2Di32(0, 0));
                        Rect2Di32 clipRect(screenPos.x(), screenPos.y(), width(), height());
                        TuiPainter painter(app->screen(), clipRect);
                        const TuiPalette &pal = app->palette();
                        painter.setStyle(pal.style(TuiPalette::WindowText, false, isEnabled())
                                .merged(pal.style(TuiPalette::Window, false, isEnabled())));
                        painter.fillRect(Rect2Di32(0, 0, width(), height()));
                }

                void resizeEvent(TuiResizeEvent *) override {
                        if(_layout) _layout->calculateLayout(Rect2Di32(0, 0, width(), height()));
                }

        private:
                TuiVBoxLayout           *_layout;
                TuiTabWidget            *_tabWidget;
                TuiStatusBar            *_statusBar;
                BasicWidgetsTab         *_basicTab;
                TextInputTab            *_textTab;
                ListViewTab             *_listTab;
                SplitterTab             *_splitterTab;
};

int main(int argc, char **argv) {
        TuiApplication app(argc, argv);

        DemoWidget root;
        app.setRootWidget(&root);

        return app.exec();
}
