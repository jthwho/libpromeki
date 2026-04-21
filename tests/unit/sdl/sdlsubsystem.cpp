/**
 * @file      sdlsubsystem.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/application.h>
#include <promeki/eventloop.h>
#include <promeki/elapsedtimer.h>
#include <promeki/widget.h>
#include <promeki/keyevent.h>

#include <chrono>
#include <thread>

using namespace promeki;

TEST_CASE("SdlSubsystem: worker-thread quit wakes Application::exec()") {
        // Construct an Application + SdlSubsystem on the stack.
        // SdlSubsystem does SDL_Init and installs its IoSource on the
        // main EventLoop; a worker-thread quit must wake the loop
        // through the EventLoop's own wake fd, independent of any
        // SDL events arriving.  Regressions the old wake-callback
        // bridge was trying to handle.
        char arg0[] = "sdlsubsystem-test";
        char *argv[] = { arg0 };
        Application  app(1, argv);
        SdlSubsystem sdl;

        REQUIRE(Application::mainEventLoop() != nullptr);

        std::thread worker([] {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                Application::quit(7);
        });

        ElapsedTimer t;
        t.start();
        const int rc = app.exec();
        worker.join();

        CHECK(rc == 7);
        CHECK(t.elapsed() < 1500);
}

TEST_CASE("SdlSubsystem: instance() returns the constructed subsystem") {
        char arg0[] = "sdlsubsystem-test";
        char *argv[] = { arg0 };
        Application  app(1, argv);

        CHECK(SdlSubsystem::instance() == nullptr);
        {
                SdlSubsystem sdl;
                CHECK(SdlSubsystem::instance() == &sdl);
        }
        CHECK(SdlSubsystem::instance() == nullptr);
}

namespace {

// Test Widget that tracks which KeyEvents reach its keyPressEvent
// handler.  Optionally accepts the event to halt propagation.
class TrackingWidget : public Widget {
        public:
                TrackingWidget(bool shouldAccept = false,
                               ObjectBase *parent = nullptr)
                        : Widget(parent), _shouldAccept(shouldAccept) {}

                int pressCount() const { return _pressCount; }
                int releaseCount() const { return _releaseCount; }
                KeyEvent::Key lastKey() const { return _lastKey; }

        protected:
                void keyPressEvent(KeyEvent *e) override {
                        ++_pressCount;
                        _lastKey = e->key();
                        if(_shouldAccept) e->accept();
                }

                void keyReleaseEvent(KeyEvent *e) override {
                        ++_releaseCount;
                        if(_shouldAccept) e->accept();
                }

        private:
                bool           _shouldAccept;
                int            _pressCount = 0;
                int            _releaseCount = 0;
                KeyEvent::Key  _lastKey = KeyEvent::Key_Unknown;
};

} // namespace

TEST_CASE("Widget: sendEvent dispatches KeyPress to keyPressEvent") {
        TrackingWidget w;
        KeyEvent ke(KeyEvent::KeyPress, KeyEvent::Key_Space);
        w.sendEvent(&ke);
        CHECK(w.pressCount() == 1);
        CHECK(w.releaseCount() == 0);
        CHECK(w.lastKey() == KeyEvent::Key_Space);
}

TEST_CASE("Widget: sendEvent dispatches KeyRelease to keyReleaseEvent") {
        TrackingWidget w;
        KeyEvent ke(KeyEvent::KeyRelease, KeyEvent::Key_Space);
        w.sendEvent(&ke);
        CHECK(w.pressCount() == 0);
        CHECK(w.releaseCount() == 1);
}

TEST_CASE("Widget: default keyPressEvent leaves event unaccepted") {
        TrackingWidget w(/*shouldAccept=*/false);
        KeyEvent ke(KeyEvent::KeyPress, KeyEvent::Key_Up);
        w.sendEvent(&ke);
        CHECK_FALSE(ke.isAccepted());
}

TEST_CASE("Widget: keyPressEvent can call accept to claim the event") {
        TrackingWidget w(/*shouldAccept=*/true);
        KeyEvent ke(KeyEvent::KeyPress, KeyEvent::Key_Down);
        w.sendEvent(&ke);
        CHECK(ke.isAccepted());
}

TEST_CASE("SdlSubsystem: focusedWidget tracking") {
        char arg0[] = "sdlsubsystem-test";
        char *argv[] = { arg0 };
        Application  app(1, argv);
        SdlSubsystem sdl;

        CHECK(sdl.focusedWidget() == nullptr);

        TrackingWidget w1;
        sdl.setFocusedWidget(&w1);
        CHECK(sdl.focusedWidget() == &w1);

        TrackingWidget w2;
        sdl.setFocusedWidget(&w2);
        CHECK(sdl.focusedWidget() == &w2);

        sdl.setFocusedWidget(nullptr);
        CHECK(sdl.focusedWidget() == nullptr);
}

TEST_CASE("Key event propagates up parent chain when unaccepted") {
        // Mirror the SDLEventPump routing logic: start at the focused
        // widget and walk up until someone accepts.
        TrackingWidget grandparent(/*accept=*/true);
        TrackingWidget parent(/*accept=*/false, &grandparent);
        TrackingWidget child(/*accept=*/false, &parent);

        KeyEvent ke(KeyEvent::KeyPress, KeyEvent::Key_Right);
        Widget *target = &child;
        while(target != nullptr) {
                target->sendEvent(&ke);
                if(ke.isAccepted()) break;
                target = dynamic_cast<Widget *>(target->parent());
        }

        CHECK(child.pressCount() == 1);
        CHECK(parent.pressCount() == 1);
        CHECK(grandparent.pressCount() == 1);
        CHECK(ke.isAccepted());
}

TEST_CASE("Key event stops propagating when a widget accepts") {
        TrackingWidget grandparent(/*accept=*/true);
        TrackingWidget parent(/*accept=*/true, &grandparent);
        TrackingWidget child(/*accept=*/false, &parent);

        KeyEvent ke(KeyEvent::KeyPress, KeyEvent::Key_Space);
        Widget *target = &child;
        while(target != nullptr) {
                target->sendEvent(&ke);
                if(ke.isAccepted()) break;
                target = dynamic_cast<Widget *>(target->parent());
        }

        CHECK(child.pressCount() == 1);
        CHECK(parent.pressCount() == 1);
        // Grandparent must not see the event — parent accepted it.
        CHECK(grandparent.pressCount() == 0);
}
