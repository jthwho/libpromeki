/**
 * @file      inputparser.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/inputparser.h>
#include <promeki/core/keyevent.h>
#include <promeki/core/mouseevent.h>

using namespace promeki;

TEST_CASE("TuiInputParser: printable ASCII characters") {
        TuiInputParser parser;

        SUBCASE("single letter") {
                auto events = parser.feed("a", 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].type == TuiInputParser::ParsedEvent::Key);
                CHECK(events[0].key == static_cast<KeyEvent::Key>('a'));
                CHECK(events[0].text == "a");
                CHECK(events[0].modifiers == 0);
        }

        SUBCASE("multiple letters") {
                auto events = parser.feed("abc", 3);
                REQUIRE(events.size() == 3);
                CHECK(events[0].key == static_cast<KeyEvent::Key>('a'));
                CHECK(events[1].key == static_cast<KeyEvent::Key>('b'));
                CHECK(events[2].key == static_cast<KeyEvent::Key>('c'));
        }

        SUBCASE("digits and symbols") {
                auto events = parser.feed("1!", 2);
                REQUIRE(events.size() == 2);
                CHECK(events[0].key == static_cast<KeyEvent::Key>('1'));
                CHECK(events[1].key == static_cast<KeyEvent::Key>('!'));
        }

        SUBCASE("space") {
                auto events = parser.feed(" ", 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == static_cast<KeyEvent::Key>(' '));
                CHECK(events[0].text == " ");
        }
}

TEST_CASE("TuiInputParser: control characters") {
        TuiInputParser parser;

        SUBCASE("enter") {
                auto events = parser.feed("\r", 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Enter);
        }

        SUBCASE("newline") {
                auto events = parser.feed("\n", 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Enter);
        }

        SUBCASE("tab") {
                auto events = parser.feed("\t", 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Tab);
        }

        SUBCASE("backspace (127)") {
                char bs = 127;
                auto events = parser.feed(&bs, 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Backspace);
        }

        SUBCASE("backspace (0x08)") {
                char bs = '\b';
                auto events = parser.feed(&bs, 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Backspace);
        }

        SUBCASE("Ctrl+A") {
                char ca = 1;
                auto events = parser.feed(&ca, 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == static_cast<KeyEvent::Key>('a'));
                CHECK(events[0].modifiers == KeyEvent::CtrlModifier);
        }

        SUBCASE("Ctrl+Q") {
                char cq = 17; // 'q' - 'a' + 1
                auto events = parser.feed(&cq, 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == static_cast<KeyEvent::Key>('q'));
                CHECK(events[0].modifiers == KeyEvent::CtrlModifier);
        }

        SUBCASE("Ctrl+Z") {
                char cz = 26;
                auto events = parser.feed(&cz, 1);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == static_cast<KeyEvent::Key>('z'));
                CHECK(events[0].modifiers == KeyEvent::CtrlModifier);
        }
}

TEST_CASE("TuiInputParser: escape sequences - arrow keys") {
        TuiInputParser parser;

        SUBCASE("up arrow") {
                const char *seq = "\033[A";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Up);
        }

        SUBCASE("down arrow") {
                const char *seq = "\033[B";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Down);
        }

        SUBCASE("right arrow") {
                const char *seq = "\033[C";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Right);
        }

        SUBCASE("left arrow") {
                const char *seq = "\033[D";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Left);
        }
}

TEST_CASE("TuiInputParser: escape sequences - navigation keys") {
        TuiInputParser parser;

        SUBCASE("home") {
                const char *seq = "\033[H";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Home);
        }

        SUBCASE("end") {
                const char *seq = "\033[F";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_End);
        }

        SUBCASE("insert") {
                const char *seq = "\033[2~";
                auto events = parser.feed(seq, 4);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Insert);
        }

        SUBCASE("delete") {
                const char *seq = "\033[3~";
                auto events = parser.feed(seq, 4);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Delete);
        }

        SUBCASE("page up") {
                const char *seq = "\033[5~";
                auto events = parser.feed(seq, 4);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_PageUp);
        }

        SUBCASE("page down") {
                const char *seq = "\033[6~";
                auto events = parser.feed(seq, 4);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_PageDown);
        }
}

TEST_CASE("TuiInputParser: function keys") {
        TuiInputParser parser;

        SUBCASE("F1 via SS3") {
                const char *seq = "\033OP";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_F1);
        }

        SUBCASE("F2 via SS3") {
                const char *seq = "\033OQ";
                auto events = parser.feed(seq, 3);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_F2);
        }

        SUBCASE("F5 via CSI") {
                const char *seq = "\033[15~";
                auto events = parser.feed(seq, 5);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_F5);
        }

        SUBCASE("F12 via CSI") {
                const char *seq = "\033[24~";
                auto events = parser.feed(seq, 5);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_F12);
        }
}

TEST_CASE("TuiInputParser: modifier keys") {
        TuiInputParser parser;

        SUBCASE("Shift+Up") {
                const char *seq = "\033[1;2A";
                auto events = parser.feed(seq, 6);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Up);
                CHECK((events[0].modifiers & KeyEvent::ShiftModifier) != 0);
        }

        SUBCASE("Alt+Right") {
                const char *seq = "\033[1;3C";
                auto events = parser.feed(seq, 6);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Right);
                CHECK((events[0].modifiers & KeyEvent::AltModifier) != 0);
        }

        SUBCASE("Ctrl+Left") {
                const char *seq = "\033[1;5D";
                auto events = parser.feed(seq, 6);
                REQUIRE(events.size() == 1);
                CHECK(events[0].key == KeyEvent::Key_Left);
                CHECK((events[0].modifiers & KeyEvent::CtrlModifier) != 0);
        }
}

TEST_CASE("TuiInputParser: Alt+key") {
        TuiInputParser parser;

        const char *seq = "\033a";
        auto events = parser.feed(seq, 2);
        REQUIRE(events.size() == 1);
        CHECK(events[0].key == static_cast<KeyEvent::Key>('a'));
        CHECK(events[0].modifiers == KeyEvent::AltModifier);
        CHECK(events[0].text == "a");
}

TEST_CASE("TuiInputParser: bare escape") {
        TuiInputParser parser;

        const char *seq = "\033";
        auto events = parser.feed(seq, 1);
        REQUIRE(events.size() == 1);
        CHECK(events[0].key == KeyEvent::Key_Escape);
}

TEST_CASE("TuiInputParser: SGR mouse events") {
        TuiInputParser parser;

        SUBCASE("left button press") {
                const char *seq = "\033[<0;10;5M";
                auto events = parser.feed(seq, 10);
                REQUIRE(events.size() == 1);
                CHECK(events[0].type == TuiInputParser::ParsedEvent::Mouse);
                CHECK(events[0].mouseButton == MouseEvent::LeftButton);
                CHECK(events[0].mouseAction == MouseEvent::Press);
                CHECK(events[0].mousePos.x() == 9);  // 0-based
                CHECK(events[0].mousePos.y() == 4);
        }

        SUBCASE("left button release") {
                const char *seq = "\033[<0;10;5m";
                auto events = parser.feed(seq, 10);
                REQUIRE(events.size() == 1);
                CHECK(events[0].type == TuiInputParser::ParsedEvent::Mouse);
                CHECK(events[0].mouseButton == MouseEvent::LeftButton);
                CHECK(events[0].mouseAction == MouseEvent::Release);
        }

        SUBCASE("right button press") {
                const char *seq = "\033[<2;1;1M";
                auto events = parser.feed(seq, 9);
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseButton == MouseEvent::RightButton);
                CHECK(events[0].mouseAction == MouseEvent::Press);
        }

        SUBCASE("scroll up") {
                const char *seq = "\033[<64;10;5M";
                auto events = parser.feed(seq, 11);
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseAction == MouseEvent::ScrollUp);
        }

        SUBCASE("scroll down") {
                const char *seq = "\033[<65;10;5M";
                auto events = parser.feed(seq, 11);
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseAction == MouseEvent::ScrollDown);
        }

        SUBCASE("mouse move with left button") {
                const char *seq = "\033[<32;10;5M";
                auto events = parser.feed(seq, 11);
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseAction == MouseEvent::Move);
                CHECK(events[0].mouseButton == MouseEvent::LeftButton);
        }
}

TEST_CASE("TuiInputParser: button state tracking") {
        TuiInputParser parser;

        SUBCASE("single press sets button state") {
                auto events = parser.feed("\033[<0;10;5M", 10);
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseButtons == MouseEvent::LeftButton);
        }

        SUBCASE("press then release clears button state") {
                auto events = parser.feed("\033[<0;10;5M", 10);  // press left
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseButtons == MouseEvent::LeftButton);

                events = parser.feed("\033[<0;10;5m", 10);  // release left
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseButtons == 0);
        }

        SUBCASE("two buttons pressed simultaneously") {
                auto events = parser.feed("\033[<0;10;5M", 10);  // press left
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseButtons == MouseEvent::LeftButton);

                events = parser.feed("\033[<2;10;5M", 10);  // press right
                REQUIRE(events.size() == 1);
                CHECK((events[0].mouseButtons & MouseEvent::LeftButton) != 0);
                CHECK((events[0].mouseButtons & MouseEvent::RightButton) != 0);

                events = parser.feed("\033[<0;10;5m", 10);  // release left
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseButtons == MouseEvent::RightButton);

                events = parser.feed("\033[<2;10;5m", 10);  // release right
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseButtons == 0);
        }

        SUBCASE("move preserves button state") {
                parser.feed("\033[<0;10;5M", 10);  // press left
                auto events = parser.feed("\033[<32;11;6M", 11);  // move with left
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseAction == MouseEvent::Move);
                CHECK(events[0].mouseButtons == MouseEvent::LeftButton);
        }

        SUBCASE("scroll does not affect button state") {
                parser.feed("\033[<0;10;5M", 10);  // press left
                auto events = parser.feed("\033[<64;10;5M", 11);  // scroll up
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseAction == MouseEvent::ScrollUp);
                CHECK(events[0].mouseButtons == MouseEvent::LeftButton);
        }

        SUBCASE("motion without buttons") {
                auto events = parser.feed("\033[<35;10;5M", 11);  // move no button
                REQUIRE(events.size() == 1);
                CHECK(events[0].mouseAction == MouseEvent::Move);
                CHECK(events[0].mouseButton == MouseEvent::NoButton);
                CHECK(events[0].mouseButtons == 0);
        }
}

TEST_CASE("TuiInputParser: mixed input sequence") {
        TuiInputParser parser;

        // 'a' followed by up arrow followed by 'b'
        const char *seq = "a\033[Ab";
        auto events = parser.feed(seq, 5);
        REQUIRE(events.size() == 3);
        CHECK(events[0].key == static_cast<KeyEvent::Key>('a'));
        CHECK(events[1].key == KeyEvent::Key_Up);
        CHECK(events[2].key == static_cast<KeyEvent::Key>('b'));
}

TEST_CASE("TuiInputParser: state preserved across feed calls") {
        TuiInputParser parser;

        // Split an escape sequence across two feed calls
        auto events1 = parser.feed("\033", 1);
        // Bare escape at end of buffer would normally emit Key_Escape,
        // so check that it did
        REQUIRE(events1.size() == 1);
        CHECK(events1[0].key == KeyEvent::Key_Escape);

        // Next call starts fresh
        auto events2 = parser.feed("a", 1);
        REQUIRE(events2.size() == 1);
        CHECK(events2[0].key == static_cast<KeyEvent::Key>('a'));
}
