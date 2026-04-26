/**
 * @file      sharedptr.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/sharedptr.h>
#include <promeki/logger.h>

using namespace promeki;

PROMEKI_DEBUG(SharedPtrTest);

static std::atomic<int> objectsAlive = 0;

class Base {
                PROMEKI_SHARED(Base)
        public:
                int value = 0;

                Base() { objectsAlive++; }
                Base(int v) : value(v) { objectsAlive++; }
                Base(const Base &o) : value(o.value) { objectsAlive++; }
                virtual ~Base() { objectsAlive--; }

                virtual std::string getType() const { return "Base"; }
};

class Derived : public Base {
                PROMEKI_SHARED_DERIVED(Base, Derived)
        public:
                Derived() = default;
                Derived(int v) : Base(v) {}
                Derived(const Derived &o) = default;
                virtual ~Derived() = default;
                virtual std::string getType() const override { return "Derived"; }
};

class NonNative {
        public:
                int value = 0;

                NonNative() { objectsAlive++; }
                NonNative(int v) : value(v) { objectsAlive++; }
                NonNative(const NonNative &o) : value(o.value) { objectsAlive++; }
                ~NonNative() { objectsAlive--; }

                std::string getType() const { return "NonNative"; }
};

// ============================================================================
// Factory method tests
// ============================================================================

TEST_CASE("SharedPtr_Create") {
        objectsAlive = 0;
        auto ptr = SharedPtr<Base>::create(42);
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 42);
        CHECK(ptr->getType() == "Base");
        CHECK(objectsAlive == 1);

        // Default construction via create
        auto ptr2 = SharedPtr<Base>::create();
        CHECK(ptr2->value == 0);
        CHECK(objectsAlive == 2);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_CreateNonNative") {
        objectsAlive = 0;
        auto ptr = SharedPtr<NonNative>::create(42);
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 42);
        CHECK(ptr->getType() == "NonNative");
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_TakeOwnership") {
        objectsAlive = 0;
        Base *raw = new Base(99);
        CHECK(objectsAlive == 1);

        auto ptr = SharedPtr<Base>::takeOwnership(raw);
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 99);
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_TakeOwnershipNull") {
        auto ptr = SharedPtr<Base>::takeOwnership(nullptr);
        CHECK(ptr.isNull() == true);
        CHECK(ptr.referenceCount() == 0);
}

TEST_CASE("SharedPtr_TakeOwnershipNonNative") {
        objectsAlive = 0;
        NonNative *raw = new NonNative(55);
        CHECK(objectsAlive == 1);

        auto ptr = SharedPtr<NonNative>::takeOwnership(raw);
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 55);
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Native object basic tests
// ============================================================================

TEST_CASE("SharedPtr_NativeBasic") {
        objectsAlive = 0;
        auto ptr = SharedPtr<Base>::create();
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr.isNull() == false);
        CHECK((bool)ptr == true);
        CHECK(ptr->getType() == "Base");
        CHECK(ptr.referenceCount() == 1);
        CHECK(objectsAlive == 1);

        SharedPtr<Base> ptr2 = ptr;
        CHECK(ptr.referenceCount() == 2);
        CHECK(ptr2.referenceCount() == 2);
        CHECK(ptr2->getType() == "Base");
        CHECK(objectsAlive == 1);

        SharedPtr<Base> ptr3(ptr);
        CHECK(ptr.referenceCount() == 3);
        CHECK(ptr3.referenceCount() == 3);

        // Self-assignment should be a no-op
        ptr = ptr;
        CHECK(ptr.referenceCount() == 3);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(ptr.referenceCount() == 2);
        CHECK(ptr2.referenceCount() == 0);
        CHECK(ptr2.isNull() == true);
        CHECK((bool)ptr2 == false);
        CHECK(objectsAlive == 1);

        ptr.clear();
        ptr3.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Non-native object basic tests
// ============================================================================

TEST_CASE("SharedPtr_NonNativeBasic") {
        objectsAlive = 0;
        auto ptr = SharedPtr<NonNative>::create();
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr.isNull() == false);
        CHECK(ptr->getType() == "NonNative");
        CHECK(ptr.referenceCount() == 1);
        CHECK(objectsAlive == 1);

        SharedPtr<NonNative> ptr2 = ptr;
        CHECK(ptr.referenceCount() == 2);
        CHECK(ptr2.referenceCount() == 2);
        CHECK(ptr2->getType() == "NonNative");
        CHECK(objectsAlive == 1);

        SharedPtr<NonNative> ptr3(ptr);
        CHECK(ptr.referenceCount() == 3);
        CHECK(ptr3.referenceCount() == 3);

        ptr = ptr;
        CHECK(ptr.referenceCount() == 3);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(ptr.referenceCount() == 2);
        CHECK(ptr2.referenceCount() == 0);
        CHECK(ptr2.isNull() == true);
        CHECK(objectsAlive == 1);

        ptr.clear();
        ptr3.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Default construction (null state)
// ============================================================================

TEST_CASE("SharedPtr_DefaultConstruction") {
        SharedPtr<Base> ptr;
        CHECK(ptr.isNull() == true);
        CHECK(ptr.isValid() == false);
        CHECK((bool)ptr == false);
        CHECK(ptr.referenceCount() == 0);

        SharedPtr<NonNative> ptr2;
        CHECK(ptr2.isNull() == true);
        CHECK(ptr2.isValid() == false);
        CHECK(ptr2.referenceCount() == 0);
}

// ============================================================================
// Move semantics - native
// ============================================================================

TEST_CASE("SharedPtr_NativeMoveConstruct") {
        objectsAlive = 0;
        auto ptr = SharedPtr<Base>::create(42);
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 42);

        // Move construct — should transfer ownership, no refcount change
        SharedPtr<Base> ptr2(std::move(ptr));
        CHECK(ptr.isNull() == true);
        CHECK(ptr.referenceCount() == 0);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(ptr2->value == 42);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_NativeMoveAssign") {
        objectsAlive = 0;
        auto ptr = SharedPtr<Base>::create(10);
        auto ptr2 = SharedPtr<Base>::create(20);
        CHECK(objectsAlive == 2);

        // Move assign — old object in ptr2 should be released
        ptr2 = std::move(ptr);
        CHECK(ptr.isNull() == true);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(ptr2->value == 10);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Move semantics - non-native
// ============================================================================

TEST_CASE("SharedPtr_NonNativeMoveConstruct") {
        objectsAlive = 0;
        auto ptr = SharedPtr<NonNative>::create(42);
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 42);

        SharedPtr<NonNative> ptr2(std::move(ptr));
        CHECK(ptr.isNull() == true);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(ptr2->value == 42);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_NonNativeMoveAssign") {
        objectsAlive = 0;
        auto ptr = SharedPtr<NonNative>::create(10);
        auto ptr2 = SharedPtr<NonNative>::create(20);
        CHECK(objectsAlive == 2);

        ptr2 = std::move(ptr);
        CHECK(ptr.isNull() == true);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(ptr2->value == 10);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Swap
// ============================================================================

TEST_CASE("SharedPtr_Swap") {
        objectsAlive = 0;
        auto a = SharedPtr<Base>::create(1);
        auto b = SharedPtr<Base>::create(2);

        a.swap(b);
        CHECK(a->value == 2);
        CHECK(b->value == 1);
        CHECK(a.referenceCount() == 1);
        CHECK(b.referenceCount() == 1);
        CHECK(objectsAlive == 2);

        // Swap with null
        SharedPtr<Base> c;
        a.swap(c);
        CHECK(a.isNull() == true);
        CHECK(c->value == 2);

        c.clear();
        b.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Comparison operators
// ============================================================================

TEST_CASE("SharedPtr_CompareEqual") {
        objectsAlive = 0;
        auto            a = SharedPtr<Base>::create(1);
        SharedPtr<Base> b = a;
        auto            c = SharedPtr<Base>::create(2);
        SharedPtr<Base> null;

        // Same underlying object
        CHECK(a == b);
        CHECK(!(a != b));

        // Different underlying objects
        CHECK(a != c);
        CHECK(!(a == c));

        // Null comparisons
        CHECK(null == nullptr);
        CHECK(!(null != nullptr));
        CHECK(a != nullptr);
        CHECK(!(a == nullptr));

        // Two nulls
        SharedPtr<Base> null2;
        CHECK(null == null2);

        a.clear();
        b.clear();
        c.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_CompareAfterDetach") {
        objectsAlive = 0;
        auto            a = SharedPtr<Base>::create(1);
        SharedPtr<Base> b = a;
        CHECK(a == b);

        // After COW detach, they should no longer be equal
        b.modify()->value = 2;
        CHECK(a != b);

        a.clear();
        b.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Copy-on-write (native)
// ============================================================================

TEST_CASE("SharedPtr_NativeCopyOnWrite") {
        objectsAlive = 0;
        auto            ptr = SharedPtr<Base>::create(100);
        SharedPtr<Base> ptr2 = ptr;
        CHECK(ptr.referenceCount() == 2);
        CHECK(objectsAlive == 1);

        // modify() should detach ptr2 (copy the object)
        ptr2.modify()->value = 200;
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(objectsAlive == 2);
        CHECK(ptr->value == 100);
        CHECK(ptr2->value == 200);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_NativeCOWNoDetachWhenSingleRef") {
        objectsAlive = 0;
        auto ptr = SharedPtr<Base>::create(100);
        CHECK(ptr.referenceCount() == 1);

        // modify() when refcount is 1 should NOT create a copy
        const Base *before = ptr.ptr();
        ptr.modify()->value = 200;
        CHECK(ptr.ptr() == before);
        CHECK(ptr->value == 200);
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Copy-on-write (non-native)
// ============================================================================

TEST_CASE("SharedPtr_NonNativeCopyOnWrite") {
        objectsAlive = 0;
        auto                 ptr = SharedPtr<NonNative>::create(100);
        SharedPtr<NonNative> ptr2 = ptr;
        CHECK(ptr.referenceCount() == 2);
        CHECK(objectsAlive == 1);

        ptr2.modify()->value = 200;
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(objectsAlive == 2);
        CHECK(ptr->value == 100);
        CHECK(ptr2->value == 200);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// CopyOnWrite disabled
// ============================================================================

TEST_CASE("SharedPtr_NoCopyOnWrite") {
        objectsAlive = 0;
        using NoCOWPtr = SharedPtr<Base, false>;
        auto     ptr = NoCOWPtr::create(100);
        NoCOWPtr ptr2 = ptr;
        CHECK(ptr.referenceCount() == 2);

        // modify() with COW disabled should NOT detach
        ptr2.modify()->value = 200;
        CHECK(ptr.referenceCount() == 2);
        CHECK(objectsAlive == 1);
        // Both see the same modified data
        CHECK(ptr->value == 200);
        CHECK(ptr2->value == 200);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Polymorphic copy-on-write (native)
// ============================================================================

TEST_CASE("SharedPtr_NativePolymorphicCOW") {
        objectsAlive = 0;
        auto ptr = SharedPtr<Base>::takeOwnership(new Derived(50));
        CHECK(ptr->getType() == "Derived");
        CHECK(ptr->value == 50);

        SharedPtr<Base> ptr2 = ptr;
        CHECK(ptr.referenceCount() == 2);
        CHECK(objectsAlive == 1);

        // modify() should clone via Derived's _promeki_clone(), preserving type
        CHECK(ptr2.modify() != ptr.ptr());
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(objectsAlive == 2);
        CHECK(ptr2->getType() == "Derived");
        CHECK(ptr2->value == 50);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Explicit detach
// ============================================================================

TEST_CASE("SharedPtr_ExplicitDetach") {
        objectsAlive = 0;
        auto            ptr = SharedPtr<Base>::create(77);
        SharedPtr<Base> ptr2 = ptr;
        CHECK(ptr.referenceCount() == 2);

        ptr2.detach();
        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr2.referenceCount() == 1);
        CHECK(objectsAlive == 2);
        CHECK(ptr2->value == 77);

        // Detach on single ref should be no-op
        const Base *before = ptr.ptr();
        ptr.detach();
        CHECK(ptr.ptr() == before);
        CHECK(ptr.referenceCount() == 1);

        // Detach on null should be no-op
        SharedPtr<Base> null;
        null.detach();
        CHECK(null.isNull() == true);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Dereference operator
// ============================================================================

TEST_CASE("SharedPtr_DereferenceOperator") {
        objectsAlive = 0;
        auto        ptr = SharedPtr<Base>::create(99);
        const Base &ref = *ptr;
        CHECK(ref.value == 99);
        CHECK(ref.getType() == "Base");

        ptr.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Clear on null is safe
// ============================================================================

TEST_CASE("SharedPtr_ClearNull") {
        SharedPtr<Base> ptr;
        ptr.clear(); // Should not crash
        CHECK(ptr.isNull() == true);

        SharedPtr<NonNative> ptr2;
        ptr2.clear();
        CHECK(ptr2.isNull() == true);
}

// ============================================================================
// Assignment to null SharedPtr
// ============================================================================

TEST_CASE("SharedPtr_AssignToNull") {
        objectsAlive = 0;
        SharedPtr<Base> ptr;
        auto            ptr2 = SharedPtr<Base>::create(5);

        ptr = ptr2;
        CHECK(ptr.referenceCount() == 2);
        CHECK(ptr->value == 5);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Chain of copies
// ============================================================================

TEST_CASE("SharedPtr_ChainOfCopies") {
        objectsAlive = 0;
        auto            a = SharedPtr<Base>::create(1);
        SharedPtr<Base> b = a;
        SharedPtr<Base> c = b;
        SharedPtr<Base> d = c;
        CHECK(a.referenceCount() == 4);
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(a.referenceCount() == 3);
        c.clear();
        CHECK(a.referenceCount() == 2);
        d.clear();
        CHECK(a.referenceCount() == 1);
        a.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Reassignment releases old, acquires new
// ============================================================================

TEST_CASE("SharedPtr_Reassignment") {
        objectsAlive = 0;
        auto            a = SharedPtr<Base>::create(1);
        auto            b = SharedPtr<Base>::create(2);
        SharedPtr<Base> c = a;
        CHECK(a.referenceCount() == 2);
        CHECK(b.referenceCount() == 1);
        CHECK(objectsAlive == 2);

        // Reassign c from a's object to b's object
        c = b;
        CHECK(a.referenceCount() == 1);
        CHECK(b.referenceCount() == 2);
        CHECK(c.referenceCount() == 2);
        CHECK(c->value == 2);
        CHECK(objectsAlive == 2);

        a.clear();
        b.clear();
        c.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// IsSharedObject trait
// ============================================================================

TEST_CASE("SharedPtr_IsSharedObjectTrait") {
        CHECK(IsSharedObject<Base>::value == true);
        CHECK(IsSharedObject<Derived>::value == true);
        CHECK(IsSharedObject<NonNative>::value == false);
        CHECK(SharedPtr<Base>::isNative == true);
        CHECK(SharedPtr<NonNative>::isNative == false);
}

// ============================================================================
// constexpr checks
// ============================================================================

TEST_CASE("SharedPtr_StaticProperties") {
        CHECK((SharedPtr<Base, true>::isCopyOnWrite == true));
        CHECK((SharedPtr<Base, false>::isCopyOnWrite == false));
        CHECK((SharedPtr<NonNative, true>::isCopyOnWrite == true));
        CHECK((SharedPtr<NonNative, false>::isCopyOnWrite == false));
}

// ============================================================================
// Thread safety tests
// ============================================================================

static int threadFuncBasic(int tid, SharedPtr<Base> ptr) {
        CHECK(ptr.referenceCount() > 1);
        CHECK(ptr->getType() == "Base");
        return 0;
}

TEST_CASE("SharedPtr_ThreadSafe") {
        objectsAlive = 0;
        const int ThreadCount = 10;
        auto      ptr = SharedPtr<Base>::create();
        CHECK(ptr.referenceCount() == 1);

        std::thread *t[ThreadCount];
        for (int i = 0; i < ThreadCount; i++) {
                t[i] = new std::thread(threadFuncBasic, i, ptr);
        }
        for (int i = 0; i < ThreadCount; i++) {
                t[i]->join();
                delete t[i];
        }

        CHECK(ptr.referenceCount() == 1);
        ptr.clear();
        CHECK(objectsAlive == 0);
}

static void threadFuncStress(SharedPtr<Base> ptr, int iterations) {
        for (int i = 0; i < iterations; i++) {
                SharedPtr<Base> local = ptr;
                // Just touch the object to ensure it's alive
                (void)local->value;
        }
}

TEST_CASE("SharedPtr_ThreadStress") {
        objectsAlive = 0;
        const int ThreadCount = 8;
        const int Iterations = 10000;
        auto      ptr = SharedPtr<Base>::create(42);

        std::vector<std::thread> threads;
        for (int i = 0; i < ThreadCount; i++) {
                threads.emplace_back(threadFuncStress, ptr, Iterations);
        }
        for (auto &t : threads) {
                t.join();
        }

        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 42);
        ptr.clear();
        CHECK(objectsAlive == 0);
}

static void threadFuncNonNativeStress(SharedPtr<NonNative> ptr, int iterations) {
        for (int i = 0; i < iterations; i++) {
                SharedPtr<NonNative> local = ptr;
                (void)local->value;
        }
}

TEST_CASE("SharedPtr_NonNativeThreadStress") {
        objectsAlive = 0;
        const int ThreadCount = 8;
        const int Iterations = 10000;
        auto      ptr = SharedPtr<NonNative>::create(42);

        std::vector<std::thread> threads;
        for (int i = 0; i < ThreadCount; i++) {
                threads.emplace_back(threadFuncNonNativeStress, ptr, Iterations);
        }
        for (auto &t : threads) {
                t.join();
        }

        CHECK(ptr.referenceCount() == 1);
        CHECK(ptr->value == 42);
        ptr.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Scope-based lifetime
// ============================================================================

TEST_CASE("SharedPtr_ScopeLifetime") {
        objectsAlive = 0;
        auto outer = SharedPtr<Base>::create(1);
        CHECK(objectsAlive == 1);
        {
                SharedPtr<Base> inner = outer;
                CHECK(outer.referenceCount() == 2);
                CHECK(objectsAlive == 1);
        }
        CHECK(outer.referenceCount() == 1);
        CHECK(objectsAlive == 1);
        outer.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Multiple COW detaches
// ============================================================================

TEST_CASE("SharedPtr_MultipleCOWDetach") {
        objectsAlive = 0;
        auto            a = SharedPtr<Base>::create(1);
        SharedPtr<Base> b = a;
        SharedPtr<Base> c = a;
        CHECK(a.referenceCount() == 3);
        CHECK(objectsAlive == 1);

        // First COW detach
        b.modify()->value = 2;
        CHECK(a.referenceCount() == 2);
        CHECK(b.referenceCount() == 1);
        CHECK(objectsAlive == 2);

        // Second COW detach
        c.modify()->value = 3;
        CHECK(a.referenceCount() == 1);
        CHECK(c.referenceCount() == 1);
        CHECK(objectsAlive == 3);

        CHECK(a->value == 1);
        CHECK(b->value == 2);
        CHECK(c->value == 3);

        a.clear();
        b.clear();
        c.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Assign null SharedPtr
// ============================================================================

TEST_CASE("SharedPtr_AssignNullSharedPtr") {
        objectsAlive = 0;
        auto            ptr = SharedPtr<Base>::create(1);
        SharedPtr<Base> null;

        ptr = null;
        CHECK(ptr.isNull() == true);
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Move from shared (multi-ref) pointer
// ============================================================================

TEST_CASE("SharedPtr_MoveFromShared") {
        objectsAlive = 0;
        auto            a = SharedPtr<Base>::create(1);
        SharedPtr<Base> b = a;
        CHECK(a.referenceCount() == 2);

        // Move b — a should still hold the object with refcount unchanged
        SharedPtr<Base> c = std::move(b);
        CHECK(b.isNull() == true);
        CHECK(a.referenceCount() == 2);
        CHECK(c.referenceCount() == 2);
        CHECK(objectsAlive == 1);

        a.clear();
        c.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Implicit upcast: SharedPtr<Derived> -> SharedPtr<Base>
// ============================================================================

TEST_CASE("SharedPtr_ImplicitUpcastCopy") {
        objectsAlive = 0;
        auto d = SharedPtr<Derived>::create(42);
        CHECK(d.referenceCount() == 1);

        // Copy-construct a base pointer from the derived pointer — both
        // should share ownership of the same underlying object.
        SharedPtr<Base> b = d;
        CHECK(b.isValid());
        CHECK(d.isValid());
        CHECK(d.referenceCount() == 2);
        CHECK(b.referenceCount() == 2);
        CHECK(b->value == 42);
        CHECK(b->getType() == "Derived"); // virtual dispatch survives the upcast
        CHECK(objectsAlive == 1);

        d.clear();
        CHECK(b.referenceCount() == 1);
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_ImplicitUpcastMove") {
        objectsAlive = 0;
        auto d = SharedPtr<Derived>::create(7);

        // Move-construct a base pointer from the derived pointer — the
        // original must be left null, refcount must stay at 1.
        SharedPtr<Base> b = std::move(d);
        CHECK(d.isNull());
        CHECK(b.isValid());
        CHECK(b.referenceCount() == 1);
        CHECK(b->value == 7);
        CHECK(b->getType() == "Derived");
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// sharedPointerCast: runtime downcast
// ============================================================================

TEST_CASE("SharedPtr_sharedPointerCast_Success") {
        objectsAlive = 0;
        SharedPtr<Base> b = SharedPtr<Derived>::create(99);
        CHECK(b.referenceCount() == 1);

        auto d = sharedPointerCast<Derived>(b);
        REQUIRE(d.isValid());
        CHECK(d->value == 99);
        CHECK(d->getType() == "Derived");

        // Both pointers share ownership of the same underlying object.
        CHECK(b.referenceCount() == 2);
        CHECK(d.referenceCount() == 2);
        CHECK(objectsAlive == 1);

        b.clear();
        d.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_sharedPointerCast_WrongType") {
        objectsAlive = 0;
        // The pointee is a plain Base, not a Derived — the cast must
        // fail and leave the source pointer untouched.
        SharedPtr<Base> b = SharedPtr<Base>::create(5);
        CHECK(b.referenceCount() == 1);

        auto d = sharedPointerCast<Derived>(b);
        CHECK(d.isNull());
        CHECK(b.isValid());
        CHECK(b.referenceCount() == 1);
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("SharedPtr_sharedPointerCast_NullInput") {
        objectsAlive = 0;
        SharedPtr<Base> b; // null
        CHECK(b.isNull());

        auto d = sharedPointerCast<Derived>(b);
        CHECK(d.isNull());
        CHECK(objectsAlive == 0);
}
