/**
 * @file      uniqueptr.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/platform.h>
#include <promeki/uniqueptr.h>
#include <promeki/logger.h>

using namespace promeki;

PROMEKI_DEBUG(UniquePtrTest);

namespace {

        std::atomic<int> objectsAlive = 0;

        class Base {
                public:
                        int value = 0;

                        Base() { objectsAlive++; }
                        Base(int v) : value(v) { objectsAlive++; }
                        Base(const Base &o) : value(o.value) { objectsAlive++; }
                        virtual ~Base() { objectsAlive--; }

                        virtual std::string getType() const { return "Base"; }
        };

        class Derived : public Base {
                public:
                        Derived() = default;
                        Derived(int v) : Base(v) {}
                        Derived(const Derived &o) = default;
                        virtual ~Derived() = default;
                        virtual std::string getType() const override { return "Derived"; }
        };

        class Plain {
                public:
                        int value = 0;

                        Plain() { objectsAlive++; }
                        Plain(int v) : value(v) { objectsAlive++; }
                        ~Plain() { objectsAlive--; }
        };

} // namespace

// ============================================================================
// Factory method tests
// ============================================================================

TEST_CASE("UniquePtr_Create") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Base>::create(42);
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr->value == 42);
        CHECK(ptr->getType() == "Base");
        CHECK(objectsAlive == 1);

        auto ptr2 = UniquePtr<Base>::create();
        CHECK(ptr2->value == 0);
        CHECK(objectsAlive == 2);

        ptr.clear();
        ptr2.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_CreatePlain") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Plain>::create(77);
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr->value == 77);
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_TakeOwnership") {
        objectsAlive = 0;
        Base *raw = new Base(99);
        CHECK(objectsAlive == 1);

        auto ptr = UniquePtr<Base>::takeOwnership(raw);
        REQUIRE(ptr.isValid() == true);
        CHECK(ptr->value == 99);
        CHECK(ptr.ptr() == raw);
        CHECK(ptr.get() == raw);
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_GetOnNull") {
        UniquePtr<Base> ptr;
        CHECK(ptr.get() == nullptr);
}

TEST_CASE("UniquePtr_TakeOwnershipNull") {
        auto ptr = UniquePtr<Base>::takeOwnership(nullptr);
        CHECK(ptr.isNull() == true);
        CHECK(ptr.isValid() == false);
}

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("UniquePtr_DefaultConstruction") {
        UniquePtr<Base> ptr;
        CHECK(ptr.isNull() == true);
        CHECK(ptr.isValid() == false);
        CHECK((bool)ptr == false);

        UniquePtr<Plain> ptr2;
        CHECK(ptr2.isNull() == true);
        CHECK(ptr2.isValid() == false);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST_CASE("UniquePtr_MoveConstruct") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Base>::create(42);
        CHECK(ptr.isValid());
        CHECK(ptr->value == 42);

        UniquePtr<Base> ptr2(std::move(ptr));
        CHECK(ptr.isNull() == true);
        CHECK(ptr2.isValid());
        CHECK(ptr2->value == 42);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_MoveAssign") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Base>::create(10);
        auto ptr2 = UniquePtr<Base>::create(20);
        CHECK(objectsAlive == 2);

        // Move assign should release ptr2's old object
        ptr2 = std::move(ptr);
        CHECK(ptr.isNull() == true);
        CHECK(ptr2.isValid());
        CHECK(ptr2->value == 10);
        CHECK(objectsAlive == 1);

        ptr2.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_SelfMoveAssign") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Base>::create(5);
        CHECK(objectsAlive == 1);

        // Intentional self-move to verify the operator handles it gracefully.
#if defined(PROMEKI_COMPILER_GCC) || defined(PROMEKI_COMPILER_CLANG)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        ptr = std::move(ptr); // should be a no-op
#if defined(PROMEKI_COMPILER_GCC) || defined(PROMEKI_COMPILER_CLANG)
#pragma GCC diagnostic pop
#endif
        CHECK(ptr.isValid());
        CHECK(ptr->value == 5);
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_MoveAssignToNull") {
        objectsAlive = 0;
        UniquePtr<Base> ptr;
        auto            src = UniquePtr<Base>::create(33);

        ptr = std::move(src);
        CHECK(ptr.isValid());
        CHECK(src.isNull());
        CHECK(ptr->value == 33);
        CHECK(objectsAlive == 1);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_MoveAssignFromNull") {
        objectsAlive = 0;
        auto            ptr = UniquePtr<Base>::create(33);
        UniquePtr<Base> src;

        ptr = std::move(src);
        CHECK(ptr.isNull());
        CHECK(src.isNull());
        CHECK(objectsAlive == 0);
}

// ============================================================================
// release / reset
// ============================================================================

TEST_CASE("UniquePtr_Release") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Base>::create(7);
        CHECK(objectsAlive == 1);

        Base *raw = ptr.release();
        REQUIRE(raw != nullptr);
        CHECK(raw->value == 7);
        CHECK(ptr.isNull() == true);
        CHECK(objectsAlive == 1);

        delete raw;
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_ReleaseOnNull") {
        UniquePtr<Base> ptr;
        Base           *raw = ptr.release();
        CHECK(raw == nullptr);
        CHECK(ptr.isNull() == true);
}

TEST_CASE("UniquePtr_Reset") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Base>::create(1);
        CHECK(objectsAlive == 1);

        // Reset to a new object — old one must be deleted
        ptr.reset(new Base(2));
        CHECK(ptr.isValid());
        CHECK(ptr->value == 2);
        CHECK(objectsAlive == 1);

        // Reset to nullptr
        ptr.reset();
        CHECK(ptr.isNull());
        CHECK(objectsAlive == 0);
}

// UniquePtr<T>::reset matches std::unique_ptr::reset — calling
// reset(get()) is a deliberate use-after-free, not a no-op.  The
// documented contract is "delete current, then assign", so this test
// only exercises the legitimate reset paths (replacement object and
// nullptr); the same-pointer case is documented as caller error and
// is therefore not tested here.
TEST_CASE("UniquePtr_ResetReleasesPriorObject") {
        objectsAlive = 0;
        auto ptr = UniquePtr<Base>::create(9);
        CHECK(objectsAlive == 1);

        // Replace with a new object — old one must be deleted before
        // the new one becomes the owned pointer.
        ptr.reset(new Base(10));
        CHECK(ptr.isValid());
        CHECK(ptr->value == 10);
        CHECK(objectsAlive == 1);

        ptr.reset(nullptr);
        CHECK(ptr.isNull());
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Swap
// ============================================================================

TEST_CASE("UniquePtr_Swap") {
        objectsAlive = 0;
        auto a = UniquePtr<Base>::create(1);
        auto b = UniquePtr<Base>::create(2);

        a.swap(b);
        CHECK(a->value == 2);
        CHECK(b->value == 1);
        CHECK(objectsAlive == 2);

        // Swap with null
        UniquePtr<Base> c;
        a.swap(c);
        CHECK(a.isNull() == true);
        CHECK(c->value == 2);

        c.clear();
        b.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_NonMemberSwap") {
        objectsAlive = 0;
        auto a = UniquePtr<Base>::create(10);
        auto b = UniquePtr<Base>::create(20);

        swap(a, b);
        CHECK(a->value == 20);
        CHECK(b->value == 10);
        CHECK(objectsAlive == 2);

        a.clear();
        b.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Comparison operators
// ============================================================================

TEST_CASE("UniquePtr_CompareEqual") {
        objectsAlive = 0;
        auto            a = UniquePtr<Base>::create(1);
        auto            b = UniquePtr<Base>::create(2);
        UniquePtr<Base> null;

        CHECK(a != b);
        CHECK(!(a == b));

        CHECK(null == nullptr);
        CHECK(!(null != nullptr));
        CHECK(a != nullptr);
        CHECK(!(a == nullptr));

        UniquePtr<Base> null2;
        CHECK(null == null2);

        a.clear();
        b.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Clear on null is safe
// ============================================================================

TEST_CASE("UniquePtr_ClearNull") {
        UniquePtr<Base> ptr;
        ptr.clear(); // should not crash
        CHECK(ptr.isNull() == true);
}

// ============================================================================
// Dereference
// ============================================================================

TEST_CASE("UniquePtr_Dereference") {
        objectsAlive = 0;
        auto  ptr = UniquePtr<Base>::create(99);
        Base &ref = *ptr;
        CHECK(ref.value == 99);
        CHECK(ref.getType() == "Base");

        // Arrow operator through non-const access allows mutation
        ptr->value = 100;
        CHECK(ptr->value == 100);

        ptr.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Scope-based lifetime
// ============================================================================

TEST_CASE("UniquePtr_ScopeLifetime") {
        objectsAlive = 0;
        {
                auto ptr = UniquePtr<Base>::create(1);
                CHECK(objectsAlive == 1);
        }
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Implicit upcast: UniquePtr<Derived> -> UniquePtr<Base>
// ============================================================================

TEST_CASE("UniquePtr_ImplicitUpcastMove") {
        objectsAlive = 0;
        auto d = UniquePtr<Derived>::create(42);
        CHECK(d.isValid());

        // Move-construct a base pointer from the derived pointer
        UniquePtr<Base> b = std::move(d);
        CHECK(d.isNull());
        CHECK(b.isValid());
        CHECK(b->value == 42);
        CHECK(b->getType() == "Derived"); // virtual dispatch survives
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_ImplicitUpcastMoveAssign") {
        objectsAlive = 0;
        auto            d = UniquePtr<Derived>::create(7);
        UniquePtr<Base> b;

        b = std::move(d);
        CHECK(d.isNull());
        CHECK(b.isValid());
        CHECK(b->value == 7);
        CHECK(b->getType() == "Derived");
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_ImplicitUpcastReplacesExisting") {
        objectsAlive = 0;
        auto b = UniquePtr<Base>::create(1);     // existing Base object
        auto d = UniquePtr<Derived>::create(99); // new Derived object
        CHECK(objectsAlive == 2);

        // Move-assigning derived into b must delete the existing Base
        b = std::move(d);
        CHECK(d.isNull());
        CHECK(b.isValid());
        CHECK(b->value == 99);
        CHECK(b->getType() == "Derived");
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(objectsAlive == 0);
}

// ============================================================================
// uniquePointerCast — runtime downcast
// ============================================================================

TEST_CASE("UniquePtr_PointerCast_Success") {
        objectsAlive = 0;
        UniquePtr<Base> b = UniquePtr<Derived>::create(99);
        CHECK(b.isValid());

        auto d = uniquePointerCast<Derived>(std::move(b));
        REQUIRE(d.isValid());
        CHECK(d->value == 99);
        CHECK(d->getType() == "Derived");
        CHECK(b.isNull()); // ownership transferred out
        CHECK(objectsAlive == 1);

        d.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_PointerCast_WrongType") {
        objectsAlive = 0;
        // The pointee is a plain Base, not Derived — cast must fail and
        // leave the source pointer with its original ownership.
        UniquePtr<Base> b = UniquePtr<Base>::create(5);

        auto d = uniquePointerCast<Derived>(std::move(b));
        CHECK(d.isNull());
        CHECK(b.isValid()); // original retained ownership
        CHECK(b->value == 5);
        CHECK(objectsAlive == 1);

        b.clear();
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_PointerCast_NullInput") {
        objectsAlive = 0;
        UniquePtr<Base> b;
        CHECK(b.isNull());

        auto d = uniquePointerCast<Derived>(std::move(b));
        CHECK(d.isNull());
        CHECK(b.isNull());
        CHECK(objectsAlive == 0);
}

// ============================================================================
// Type checks — UniquePtr must be move-only
// ============================================================================

TEST_CASE("UniquePtr_NotCopyable") {
        CHECK(std::is_copy_constructible_v<UniquePtr<Base>> == false);
        CHECK(std::is_copy_assignable_v<UniquePtr<Base>> == false);
        CHECK(std::is_move_constructible_v<UniquePtr<Base>> == true);
        CHECK(std::is_move_assignable_v<UniquePtr<Base>> == true);
}

// ============================================================================
// Array specialization: UniquePtr<T[]>
// ============================================================================

TEST_CASE("UniquePtrArray_CreateArray_DefaultInitTrivial") {
        // For trivially-default-constructible T, createArray leaves
        // the storage uninitialised — we can only check that
        // operator[] returns the underlying storage and writes are
        // visible.  No content assertion on the uninitialised cells.
        auto arr = UniquePtr<int[]>::createArray(4);
        REQUIRE(arr.isValid());
        for (size_t i = 0; i < 4; ++i) arr[i] = static_cast<int>(i) * 7;
        CHECK(arr[0] == 0);
        CHECK(arr[3] == 21);
}

TEST_CASE("UniquePtrArray_CreateArrayValueInit_ZerosTrivial") {
        // Value-init form must zero trivially-default-constructible T.
        auto arr = UniquePtr<int[]>::createArrayValueInit(8);
        REQUIRE(arr.isValid());
        for (size_t i = 0; i < 8; ++i) CHECK(arr[i] == 0);
}

TEST_CASE("UniquePtrArray_CreateArrayValueInit_RunsDefaultCtor") {
        // For class types with a user-provided default constructor
        // (e.g. Plain — bumps objectsAlive on each construction),
        // value-init invokes the constructor for every element.
        objectsAlive = 0;
        {
                auto arr = UniquePtr<Plain[]>::createArrayValueInit(5);
                REQUIRE(arr.isValid());
                CHECK(objectsAlive == 5);
                // Verify the elements are reachable via operator[].
                arr[2].value = 99;
                CHECK(arr[2].value == 99);
        }
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtrArray_TakeOwnership") {
        int *raw = new int[3]{10, 20, 30};
        auto arr = UniquePtr<int[]>::takeOwnership(raw);
        REQUIRE(arr.isValid());
        CHECK(arr[0] == 10);
        CHECK(arr[1] == 20);
        CHECK(arr[2] == 30);
        // raw is now owned by `arr`; no manual delete[].
}

TEST_CASE("UniquePtrArray_MoveConstruct") {
        objectsAlive = 0;
        auto a = UniquePtr<Plain[]>::createArrayValueInit(3);
        CHECK(objectsAlive == 3);
        auto b = std::move(a);
        CHECK(a.isNull());
        CHECK(b.isValid());
        CHECK(objectsAlive == 3); // Move doesn't reconstruct.
}

TEST_CASE("UniquePtrArray_MoveAssign") {
        objectsAlive = 0;
        auto a = UniquePtr<Plain[]>::createArrayValueInit(2);
        auto b = UniquePtr<Plain[]>::createArrayValueInit(5);
        CHECK(objectsAlive == 7);
        a = std::move(b);
        // b's prior allocation now owned by a; a's prior 2 elements
        // were destroyed by move-assign.
        CHECK(objectsAlive == 5);
        CHECK(b.isNull());
}

TEST_CASE("UniquePtrArray_Reset_ReleasesPriorArray") {
        objectsAlive = 0;
        auto arr = UniquePtr<Plain[]>::createArrayValueInit(4);
        CHECK(objectsAlive == 4);
        arr.reset(new Plain[2]());
        CHECK(objectsAlive == 2);
        arr.reset(nullptr);
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtrArray_Release") {
        objectsAlive = 0;
        auto  arr = UniquePtr<Plain[]>::createArrayValueInit(3);
        Plain *raw = arr.release();
        CHECK(arr.isNull());
        CHECK(objectsAlive == 3);
        delete[] raw;
        CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtrArray_Swap") {
        auto a = UniquePtr<int[]>::createArrayValueInit(2);
        auto b = UniquePtr<int[]>::createArrayValueInit(3);
        a[0] = 11;
        b[0] = 22;
        a.swap(b);
        CHECK(a[0] == 22);
        CHECK(b[0] == 11);

        swap(a, b); // non-member swap
        CHECK(a[0] == 11);
        CHECK(b[0] == 22);
}

TEST_CASE("UniquePtrArray_BooleanContext") {
        UniquePtr<int[]> empty;
        CHECK(!empty);
        CHECK(empty.isNull());
        CHECK(empty == nullptr);

        auto arr = UniquePtr<int[]>::createArrayValueInit(1);
        CHECK(static_cast<bool>(arr));
        CHECK(arr.isValid());
        CHECK(arr != nullptr);
}

TEST_CASE("UniquePtrArray_NotCopyable") {
        CHECK(std::is_copy_constructible_v<UniquePtr<int[]>> == false);
        CHECK(std::is_copy_assignable_v<UniquePtr<int[]>> == false);
        CHECK(std::is_move_constructible_v<UniquePtr<int[]>> == true);
        CHECK(std::is_move_assignable_v<UniquePtr<int[]>> == true);
}

TEST_CASE("UniquePtrArray_NonMovableElement") {
        // The motivating use case: hold an array of non-movable
        // atomics that can't sit inside a List<T> or Atomic<T> array.
        auto arr = UniquePtr<Atomic<int>[]>::createArrayValueInit(4);
        REQUIRE(arr.isValid());
        for (size_t i = 0; i < 4; ++i) {
                CHECK(arr[i].load(MemoryOrder::Relaxed) == 0);
                arr[i].store(static_cast<int>(i + 1), MemoryOrder::Relaxed);
        }
        for (size_t i = 0; i < 4; ++i) {
                CHECK(arr[i].load(MemoryOrder::Relaxed) == static_cast<int>(i + 1));
        }
}
