/**
 * @file      uniqueptr.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
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

    ptr = std::move(ptr);  // should be a no-op
    CHECK(ptr.isValid());
    CHECK(ptr->value == 5);
    CHECK(objectsAlive == 1);

    ptr.clear();
    CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_MoveAssignToNull") {
    objectsAlive = 0;
    UniquePtr<Base> ptr;
    auto src = UniquePtr<Base>::create(33);

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
    auto ptr = UniquePtr<Base>::create(33);
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
    Base *raw = ptr.release();
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

TEST_CASE("UniquePtr_ResetToSamePointer") {
    objectsAlive = 0;
    Base *raw = new Base(9);
    auto ptr = UniquePtr<Base>::takeOwnership(raw);
    CHECK(objectsAlive == 1);

    // Passing the same pointer should be a no-op, not double-delete
    ptr.reset(raw);
    CHECK(ptr.isValid());
    CHECK(ptr->value == 9);
    CHECK(objectsAlive == 1);

    ptr.clear();
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
    auto a = UniquePtr<Base>::create(1);
    auto b = UniquePtr<Base>::create(2);
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
    ptr.clear();  // should not crash
    CHECK(ptr.isNull() == true);
}

// ============================================================================
// Dereference
// ============================================================================

TEST_CASE("UniquePtr_Dereference") {
    objectsAlive = 0;
    auto ptr = UniquePtr<Base>::create(99);
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
    CHECK(b->getType() == "Derived");  // virtual dispatch survives
    CHECK(objectsAlive == 1);

    b.clear();
    CHECK(objectsAlive == 0);
}

TEST_CASE("UniquePtr_ImplicitUpcastMoveAssign") {
    objectsAlive = 0;
    auto d = UniquePtr<Derived>::create(7);
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
    auto b = UniquePtr<Base>::create(1);      // existing Base object
    auto d = UniquePtr<Derived>::create(99);  // new Derived object
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
    CHECK(b.isNull());  // ownership transferred out
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
    CHECK(b.isValid());  // original retained ownership
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
