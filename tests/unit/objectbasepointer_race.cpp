/**
 * @file      objectbasepointer_race.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Regression coverage for the ObjectBasePtr cross-thread copy race.
 * The bug was that the copy constructor / converting copy ctor / copy
 * assignment performed the source-pointer load (writing to @c p) and
 * the @c _pointerMap registration as two separate critical sections.
 * A concurrent @c ObjectBase::runCleanup that fired between those two
 * steps would null every existing tracker but miss the not-yet-linked
 * new one, leaving it dangling-non-null.  The framework then happily
 * reported @c isValid()==true and the consumer dereferenced freed
 * memory — which is what blew up the MediaPipeline stress test inside
 * @c MediaIOStatsCollector::onCommandCompleted.
 */

#include <atomic>
#include <cstdint>
#include <thread>

#include <doctest/doctest.h>
#include <promeki/objectbase.h>

using namespace promeki;

namespace {

class CanaryObj : public ObjectBase {
                PROMEKI_OBJECT(CanaryObj, ObjectBase);

        public:
                CanaryObj() = default;
                ~CanaryObj() override = default;
};

} // namespace

TEST_CASE("ObjectBasePtr: cross-thread copy/destroy race") {
        // Direct post-condition check: once the tracked object has
        // been fully destroyed, every ObjectBasePtr that referred to
        // it MUST report a null @c p.  The previous racy copy ctor
        // could leave a freshly-copied tracker with a stale non-null
        // @c p when @c runCleanup happened to fire between the
        // source load and the @c _pointerMap registration —
        // observable here as a non-null @c data() after the join.
        //
        // The race window is narrow (the buggy code spans only a
        // handful of instructions).  We hold a persistent copier
        // thread to amortise scheduler hops, run many iterations,
        // and pin the publisher's destruction to land between
        // "copier loaded other.p" and "copier acquired the global
        // mutex".  With the fix this stays at zero across many runs;
        // with the bug it fires regularly under load.
        constexpr int kIters = 200000;

        std::atomic<uint64_t> bugCount{0};
        std::atomic<bool>     stop{false};
        std::atomic<int>      pubPhase{0};   // bumped per iteration to release copier
        std::atomic<int>      copPhase{0};   // bumped after copier's copy completes
        ObjectBasePtr<CanaryObj> shared;
        ObjectBasePtr<CanaryObj> copierLocal;

        std::thread copier([&]() {
                int seen = 0;
                while (!stop.load(std::memory_order_acquire)) {
                        while (pubPhase.load(std::memory_order_acquire) == seen) {
                                if (stop.load(std::memory_order_acquire)) return;
                        }
                        seen = pubPhase.load(std::memory_order_acquire);
                        copierLocal = shared;
                        copPhase.store(seen, std::memory_order_release);
                }
        });

        for (int i = 1; i <= kIters; ++i) {
                alignas(CanaryObj) unsigned char storage[sizeof(CanaryObj)];
                auto                            *obj = new (storage) CanaryObj();

                shared = ObjectBasePtr<CanaryObj>(obj);
                copierLocal = ObjectBasePtr<CanaryObj>();

                pubPhase.store(i, std::memory_order_release);
                // Yield to give the copier a chance to enter the
                // racy load.  The destructor below is what closes
                // the window — if we destroy before the copier has
                // even started, the bug can't surface this iter.
                std::this_thread::yield();
                obj->~CanaryObj();

                // Wait for copier to confirm it finished the copy.
                while (copPhase.load(std::memory_order_acquire) != i) std::this_thread::yield();

                if (copierLocal.data() != nullptr) {
                        bugCount.fetch_add(1, std::memory_order_relaxed);
                }
        }

        stop.store(true, std::memory_order_release);
        pubPhase.fetch_add(1, std::memory_order_release);
        copier.join();

        CHECK(bugCount.load() == 0);
}
