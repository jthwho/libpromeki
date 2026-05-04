# TUI Widget Completion

**Phase:** 5
**Dependencies:** Minimal (existing TUI framework). Mostly independent of other phases.
**Library:** `promeki-tui`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Each widget follows existing patterns: derives from `TuiWidget`, uses `PROMEKI_OBJECT`, implements `paintEvent()`, `keyEvent()`, `sizeHint()`. Verified via `tui-demo` (add new tabs for new widgets).

---

## 5A. Selection and Input Widgets

---

### TuiRadioButton

Exclusive selection radio button.

**Files:**
- [ ] `include/promeki/tui/tuiradiobutton.h`
- [ ] `src/tui/tuiradiobutton.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `void setText(const String &text)`
- [ ] `String text() const`
- [ ] `bool isChecked() const`
- [ ] `void setChecked(bool checked)`
- [ ] `PROMEKI_SIGNAL(toggled, bool)` — emitted when state changes
- [ ] `paintEvent()`: render `( ) text` / `(*) text` with TuiStyle
- [ ] `keyEvent()`: Space/Enter toggles
- [ ] `sizeHint()`: text width + prefix
- [ ] Focus handling: visual indication when focused
- [ ] Add to `tui-demo`

---

### TuiButtonGroup

ObjectBase managing radio button mutual exclusion. Not a widget.

**Files:**
- [ ] `include/promeki/tui/tuibuttongroup.h`
- [ ] `src/tui/tuibuttongroup.cpp`

**Implementation checklist:**
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `void addButton(TuiRadioButton *button)`
- [ ] `void removeButton(TuiRadioButton *button)`
- [ ] `List<TuiRadioButton *> buttons() const`
- [ ] `TuiRadioButton *checkedButton() const`
- [ ] `void setExclusive(bool exclusive)` — default true
- [ ] `bool isExclusive() const`
- [ ] `PROMEKI_SIGNAL(buttonToggled, TuiRadioButton *, bool)`
- [ ] Internal: connect to each button's `toggled` signal, uncheck others when one is checked

---

### TuiComboBox

Dropdown selection using TuiListView internally.

**Files:**
- [ ] `include/promeki/tui/tuicombobox.h`
- [ ] `src/tui/tuicombobox.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `void addItem(const String &text)`
- [ ] `void addItems(const List<String> &items)`
- [ ] `void insertItem(int index, const String &text)`
- [ ] `void removeItem(int index)`
- [ ] `void clear()`
- [ ] `int count() const`
- [ ] `String itemText(int index) const`
- [ ] `int currentIndex() const`
- [ ] `void setCurrentIndex(int index)`
- [ ] `String currentText() const`
- [ ] `PROMEKI_SIGNAL(currentIndexChanged, int)`
- [ ] `PROMEKI_SIGNAL(currentTextChanged, String)`
- [ ] `paintEvent()`: render `[current item v]` in collapsed state; show dropdown overlay when open
- [ ] `keyEvent()`: Enter/Space opens dropdown, Up/Down navigates, Enter selects, Escape cancels
- [ ] `sizeHint()`: width of widest item + frame
- [ ] Internal: use TuiListView for dropdown display
- [ ] Add to `tui-demo`

---

### TuiSpinBox

Numeric input with increment/decrement.

**Files:**
- [ ] `include/promeki/tui/tuispinbox.h`
- [ ] `src/tui/tuispinbox.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `int value() const`
- [ ] `void setValue(int value)`
- [ ] `int minimum() const`, `void setMinimum(int)`
- [ ] `int maximum() const`, `void setMaximum(int)`
- [ ] `void setRange(int min, int max)`
- [ ] `int singleStep() const`, `void setSingleStep(int)`
- [ ] `String prefix() const`, `void setPrefix(const String &)` — e.g., "$"
- [ ] `String suffix() const`, `void setSuffix(const String &)` — e.g., " Hz"
- [ ] `PROMEKI_SIGNAL(valueChanged, int)`
- [ ] `paintEvent()`: render `[< value >]` with prefix/suffix
- [ ] `keyEvent()`: Up/Right increments, Down/Left decrements, type digits for direct input
- [ ] `sizeHint()`: prefix + max digits + suffix + arrows
- [ ] Clamp value to min/max
- [ ] Add to `tui-demo`

---

### TuiSlider

Horizontal/vertical slider.

**Files:**
- [ ] `include/promeki/tui/tuislider.h`
- [ ] `src/tui/tuislider.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `enum Orientation { Horizontal, Vertical }`
- [ ] `void setOrientation(Orientation)`
- [ ] `Orientation orientation() const`
- [ ] `int value() const`
- [ ] `void setValue(int value)`
- [ ] `int minimum() const`, `void setMinimum(int)`
- [ ] `int maximum() const`, `void setMaximum(int)`
- [ ] `void setRange(int min, int max)`
- [ ] `int singleStep() const`, `void setSingleStep(int)`
- [ ] `int pageStep() const`, `void setPageStep(int)`
- [ ] `PROMEKI_SIGNAL(valueChanged, int)`
- [ ] `PROMEKI_SIGNAL(sliderMoved, int)` — emitted during drag
- [ ] `paintEvent()`: render track `[===|-----]` with handle position
- [ ] `keyEvent()`: Left/Down decreases, Right/Up increases, PgUp/PgDn for page step, Home/End for min/max
- [ ] `sizeHint()`: minimum track length
- [ ] Add to `tui-demo`

---

## 5B. Data Display Widgets

---

### TuiTreeView

Hierarchical tree with expand/collapse.

**Files:**
- [ ] `include/promeki/tui/tuitreeview.h`
- [ ] `src/tui/tuitreeview.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] Nested `TreeItem` class:
  - [ ] `String text() const`, `setText(const String &)`
  - [ ] `TreeItem *parent() const`
  - [ ] `List<TreeItem *> children() const`
  - [ ] `TreeItem *addChild(const String &text)`
  - [ ] `void removeChild(TreeItem *child)`
  - [ ] `bool isExpanded() const`, `setExpanded(bool)`
  - [ ] `int childCount() const`
  - [ ] `int depth() const`
- [ ] `TreeItem *rootItem()` — invisible root
- [ ] `TreeItem *addTopLevelItem(const String &text)`
- [ ] `TreeItem *currentItem() const`
- [ ] `void setCurrentItem(TreeItem *item)`
- [ ] `void expandAll()`, `void collapseAll()`
- [ ] `PROMEKI_SIGNAL(currentItemChanged, TreeItem *)`
- [ ] `PROMEKI_SIGNAL(itemExpanded, TreeItem *)`
- [ ] `PROMEKI_SIGNAL(itemCollapsed, TreeItem *)`
- [ ] `PROMEKI_SIGNAL(itemActivated, TreeItem *)` — Enter key
- [ ] `paintEvent()`: render tree with indentation, expand/collapse indicators (`v` / `>`)
- [ ] `keyEvent()`: Up/Down navigates, Left collapses/goes to parent, Right expands/goes to child, Enter activates
- [ ] Scrolling: vertical scroll when tree exceeds visible height
- [ ] `sizeHint()`: based on visible item count and max text width
- [ ] Add to `tui-demo`

---

### TuiTableView

Grid with column headers, row selection, scrolling.

**Files:**
- [ ] `include/promeki/tui/tuitableview.h`
- [ ] `src/tui/tuitableview.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `void setColumnCount(int count)`
- [ ] `void setRowCount(int count)`
- [ ] `int columnCount() const`, `int rowCount() const`
- [ ] `void setHeaderText(int column, const String &text)`
- [ ] `String headerText(int column) const`
- [ ] `void setCellText(int row, int column, const String &text)`
- [ ] `String cellText(int row, int column) const`
- [ ] `void setColumnWidth(int column, int width)`
- [ ] `int columnWidth(int column) const`
- [ ] `void setAutoResizeColumns(bool enable)` — auto-fit to content
- [ ] `int currentRow() const`, `void setCurrentRow(int)`
- [ ] `int currentColumn() const`, `void setCurrentColumn(int)`
- [ ] `void insertRow(int row)`
- [ ] `void removeRow(int row)`
- [ ] `void clear()`
- [ ] `PROMEKI_SIGNAL(currentCellChanged, int row, int column)`
- [ ] `PROMEKI_SIGNAL(cellActivated, int row, int column)` — Enter key
- [ ] `paintEvent()`: render grid with header row, column separators, row highlight
- [ ] `keyEvent()`: Arrow keys navigate cells, Enter activates, Tab moves to next column
- [ ] Horizontal and vertical scrolling
- [ ] `sizeHint()`: based on column widths and visible row count
- [ ] Add to `tui-demo`

---

## 5C. Container and Dialog Widgets

---

### TuiGroupBox

Frame with title and optional checkbox.

**Files:**
- [ ] `include/promeki/tui/tuigroupbox.h`
- [ ] `src/tui/tuigroupbox.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiFrame` (or `TuiWidget` with frame drawing)
- [ ] `void setTitle(const String &title)`
- [ ] `String title() const`
- [ ] `void setCheckable(bool checkable)`
- [ ] `bool isCheckable() const`
- [ ] `bool isChecked() const`, `void setChecked(bool)`
- [ ] `PROMEKI_SIGNAL(toggled, bool)` — when checkbox changes
- [ ] `paintEvent()`: render frame with title embedded in top border, optional checkbox
- [ ] When unchecked, child widgets are disabled/dimmed
- [ ] Add to `tui-demo`

---

### TuiStackedWidget

Shows one child widget at a time by index.

**Files:**
- [ ] `include/promeki/tui/tuistackedwidget.h`
- [ ] `src/tui/tuistackedwidget.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `int addWidget(TuiWidget *widget)` — returns index
- [ ] `void insertWidget(int index, TuiWidget *widget)`
- [ ] `void removeWidget(TuiWidget *widget)`
- [ ] `int currentIndex() const`
- [ ] `void setCurrentIndex(int index)`
- [ ] `TuiWidget *currentWidget() const`
- [ ] `void setCurrentWidget(TuiWidget *widget)`
- [ ] `int count() const`
- [ ] `TuiWidget *widget(int index) const`
- [ ] `PROMEKI_SIGNAL(currentChanged, int)`
- [ ] `paintEvent()`: only paint the current widget
- [ ] `sizeHint()`: max of all children's size hints
- [ ] Add to `tui-demo`

---

### TuiDialog

Modal/modeless dialog with nested EventLoop.

**Files:**
- [ ] `include/promeki/tui/tuidialog.h`
- [ ] `src/tui/tuidialog.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `enum DialogCode { Accepted, Rejected }`
- [ ] `void setTitle(const String &title)`
- [ ] `String title() const`
- [ ] `DialogCode exec()` — modal: runs nested EventLoop, returns on accept/reject
- [ ] `void open()` — modeless: show without blocking
- [ ] `void accept()` — close with Accepted
- [ ] `void reject()` — close with Rejected
- [ ] `DialogCode result() const`
- [ ] `PROMEKI_SIGNAL(accepted)`
- [ ] `PROMEKI_SIGNAL(rejected)`
- [ ] `PROMEKI_SIGNAL(finished, DialogCode)`
- [ ] `paintEvent()`: render as overlay with title bar, border, content area
- [ ] `keyEvent()`: Escape rejects
- [ ] Modal: captures all input, dims background
- [ ] Add to `tui-demo`

---

### TuiMessageBox

Static convenience dialogs.

**Files:**
- [ ] `include/promeki/tui/tuimessagebox.h`
- [ ] `src/tui/tuimessagebox.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiDialog`
- [ ] `enum Button { Ok, Cancel, Yes, No }`
- [ ] `enum Icon { Information, Warning, Critical, Question }`
- [ ] `static Button information(const String &title, const String &text)`
- [ ] `static Button warning(const String &title, const String &text)`
- [ ] `static Button critical(const String &title, const String &text)`
- [ ] `static Button question(const String &title, const String &text)` — Yes/No buttons
- [ ] `void setIcon(Icon icon)`
- [ ] `void setText(const String &text)`
- [ ] `void setButtons(List<Button> buttons)`
- [ ] `paintEvent()`: render icon + text + button row
- [ ] `keyEvent()`: Tab between buttons, Enter selects, Y/N shortcuts for question
- [ ] Add to `tui-demo`

---

### TuiFileDialog

File/directory selection dialog.

**Files:**
- [ ] `include/promeki/tui/tuifiledialog.h`
- [ ] `src/tui/tuifiledialog.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiDialog`
- [ ] `enum Mode { OpenFile, SaveFile, OpenDirectory }`
- [ ] `void setMode(Mode mode)`
- [ ] `void setDirectory(const FilePath &dir)` — starting directory
- [ ] `void setNameFilter(const String &filter)` — e.g., "*.txt"
- [ ] `void setNameFilters(const List<String> &filters)`
- [ ] `FilePath selectedFile() const`
- [ ] `List<FilePath> selectedFiles() const` — for multi-select
- [ ] `void setMultiSelect(bool enable)`
- [ ] `static FilePath getOpenFileName(const String &title, const FilePath &dir = {}, const String &filter = {})`
- [ ] `static FilePath getSaveFileName(const String &title, const FilePath &dir = {}, const String &filter = {})`
- [ ] `static FilePath getExistingDirectory(const String &title, const FilePath &dir = {})`
- [ ] `paintEvent()`: directory listing, path bar, filter selector, filename input
- [ ] `keyEvent()`: navigate directory listing, type filename, Enter selects, Escape cancels
- [ ] Add to `tui-demo`

---

### TuiToolBar

Horizontal row of action-driven buttons.

**Files:**
- [ ] `include/promeki/tui/tuitoolbar.h`
- [ ] `src/tui/tuitoolbar.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `void addAction(TuiAction *action)` — (TuiAction needs to exist or be created)
- [ ] `void addSeparator()`
- [ ] `void removeAction(TuiAction *action)`
- [ ] `List<TuiAction *> actions() const`
- [ ] `void setOrientation(enum { Horizontal, Vertical })` — default Horizontal
- [ ] `paintEvent()`: render actions as `[label]` buttons with separators `|`
- [ ] `keyEvent()`: Left/Right navigates, Enter/Space activates
- [ ] `sizeHint()`: sum of action widths + separators
- [ ] Add to `tui-demo`

**Dependency note:** May need a `TuiAction` class if not already present:
- [ ] `TuiAction`: text, shortcut, enabled, checkable. Signal: `triggered()`.

---

### TuiTooltip

Overlay popup on focused widget after delay.

**Files:**
- [ ] `include/promeki/tui/tuitooltip.h`
- [ ] `src/tui/tuitooltip.cpp`

**Implementation checklist:**
- [ ] Utility class (possibly not a full widget)
- [ ] `static void showText(const String &text, int x, int y, unsigned int durationMs = 3000)`
- [ ] `static void hide()`
- [ ] Integrate with TuiWidget:
  - [ ] `TuiWidget::setToolTip(const String &text)`
  - [ ] `TuiWidget::toolTip() const`
  - [ ] Show tooltip after focus dwell time (e.g., 1 second)
  - [ ] Hide on key press or focus change
- [ ] `paintEvent()`: render as small overlay box with text
- [ ] Timer-based show/hide via EventLoop
- [ ] Add to `tui-demo`

---

### TuiCalendar

Month-view date picker.

**Files:**
- [ ] `include/promeki/tui/tuicalendar.h`
- [ ] `src/tui/tuicalendar.cpp`

**Implementation checklist:**
- [ ] Derive from `TuiWidget`, use `PROMEKI_OBJECT`
- [ ] `void setSelectedDate(const DateTime &date)` — or a Date type
- [ ] `DateTime selectedDate() const`
- [ ] `void setMinimumDate(const DateTime &date)`
- [ ] `void setMaximumDate(const DateTime &date)`
- [ ] `int currentMonth() const`, `int currentYear() const`
- [ ] `void showNextMonth()`, `void showPreviousMonth()`
- [ ] `void showNextYear()`, `void showPreviousYear()`
- [ ] `PROMEKI_SIGNAL(dateSelected, DateTime)`
- [ ] `PROMEKI_SIGNAL(currentPageChanged, int year, int month)`
- [ ] `paintEvent()`: render month grid:
  - [ ] Header: `< March 2026 >`
  - [ ] Day-of-week row: `Mo Tu We Th Fr Sa Su`
  - [ ] Day numbers in grid, highlight selected, dim out-of-month days
- [ ] `keyEvent()`: Arrow keys navigate days, PgUp/PgDn changes month, Enter selects
- [ ] `sizeHint()`: 7 columns x 6 rows + header (22 wide x 9 tall minimum)
- [ ] Add to `tui-demo`
