/**
 * @file      sharedptr.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <thread>
#include <vector>
#include <promeki/unittest.h>
#include <promeki/sharedptr.h>

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

PROMEKI_TEST_BEGIN(SharedPtr_Create)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(42);
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 42);
    PROMEKI_TEST(ptr->getType() == "Base");
    PROMEKI_TEST(objectsAlive == 1);

    // Default construction via create
    auto ptr2 = SharedPtr<Base>::create();
    PROMEKI_TEST(ptr2->value == 0);
    PROMEKI_TEST(objectsAlive == 2);

    ptr.clear();
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_CreateNonNative)
    objectsAlive = 0;
    auto ptr = SharedPtr<NonNative>::create(42);
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 42);
    PROMEKI_TEST(ptr->getType() == "NonNative");
    PROMEKI_TEST(objectsAlive == 1);

    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_TakeOwnership)
    objectsAlive = 0;
    Base *raw = new Base(99);
    PROMEKI_TEST(objectsAlive == 1);

    auto ptr = SharedPtr<Base>::takeOwnership(raw);
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 99);
    PROMEKI_TEST(objectsAlive == 1);

    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_TakeOwnershipNull)
    auto ptr = SharedPtr<Base>::takeOwnership(nullptr);
    PROMEKI_TEST(ptr.isNull() == true);
    PROMEKI_TEST(ptr.referenceCount() == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_TakeOwnershipNonNative)
    objectsAlive = 0;
    NonNative *raw = new NonNative(55);
    PROMEKI_TEST(objectsAlive == 1);

    auto ptr = SharedPtr<NonNative>::takeOwnership(raw);
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 55);
    PROMEKI_TEST(objectsAlive == 1);

    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Native object basic tests
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NativeBasic)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create();
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr.isNull() == false);
    PROMEKI_TEST((bool)ptr == true);
    PROMEKI_TEST(ptr->getType() == "Base");
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 1);

    SharedPtr<Base> ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(ptr2.referenceCount() == 2);
    PROMEKI_TEST(ptr2->getType() == "Base");
    PROMEKI_TEST(objectsAlive == 1);

    SharedPtr<Base> ptr3(ptr);
    PROMEKI_TEST(ptr.referenceCount() == 3);
    PROMEKI_TEST(ptr3.referenceCount() == 3);

    // Self-assignment should be a no-op
    ptr = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 3);
    PROMEKI_TEST(objectsAlive == 1);

    ptr2.clear();
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(ptr2.referenceCount() == 0);
    PROMEKI_TEST(ptr2.isNull() == true);
    PROMEKI_TEST((bool)ptr2 == false);
    PROMEKI_TEST(objectsAlive == 1);

    ptr.clear();
    ptr3.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Non-native object basic tests
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NonNativeBasic)
    objectsAlive = 0;
    auto ptr = SharedPtr<NonNative>::create();
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr.isNull() == false);
    PROMEKI_TEST(ptr->getType() == "NonNative");
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 1);

    SharedPtr<NonNative> ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(ptr2.referenceCount() == 2);
    PROMEKI_TEST(ptr2->getType() == "NonNative");
    PROMEKI_TEST(objectsAlive == 1);

    SharedPtr<NonNative> ptr3(ptr);
    PROMEKI_TEST(ptr.referenceCount() == 3);
    PROMEKI_TEST(ptr3.referenceCount() == 3);

    ptr = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 3);
    PROMEKI_TEST(objectsAlive == 1);

    ptr2.clear();
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(ptr2.referenceCount() == 0);
    PROMEKI_TEST(ptr2.isNull() == true);
    PROMEKI_TEST(objectsAlive == 1);

    ptr.clear();
    ptr3.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Default construction (null state)
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_DefaultConstruction)
    SharedPtr<Base> ptr;
    PROMEKI_TEST(ptr.isNull() == true);
    PROMEKI_TEST(ptr.isValid() == false);
    PROMEKI_TEST((bool)ptr == false);
    PROMEKI_TEST(ptr.referenceCount() == 0);

    SharedPtr<NonNative> ptr2;
    PROMEKI_TEST(ptr2.isNull() == true);
    PROMEKI_TEST(ptr2.isValid() == false);
    PROMEKI_TEST(ptr2.referenceCount() == 0);
PROMEKI_TEST_END();

// ============================================================================
// Move semantics - native
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NativeMoveConstruct)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(42);
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 42);

    // Move construct — should transfer ownership, no refcount change
    SharedPtr<Base> ptr2(std::move(ptr));
    PROMEKI_TEST(ptr.isNull() == true);
    PROMEKI_TEST(ptr.referenceCount() == 0);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(ptr2->value == 42);
    PROMEKI_TEST(objectsAlive == 1);

    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_NativeMoveAssign)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(10);
    auto ptr2 = SharedPtr<Base>::create(20);
    PROMEKI_TEST(objectsAlive == 2);

    // Move assign — old object in ptr2 should be released
    ptr2 = std::move(ptr);
    PROMEKI_TEST(ptr.isNull() == true);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(ptr2->value == 10);
    PROMEKI_TEST(objectsAlive == 1);

    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Move semantics - non-native
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NonNativeMoveConstruct)
    objectsAlive = 0;
    auto ptr = SharedPtr<NonNative>::create(42);
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 42);

    SharedPtr<NonNative> ptr2(std::move(ptr));
    PROMEKI_TEST(ptr.isNull() == true);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(ptr2->value == 42);
    PROMEKI_TEST(objectsAlive == 1);

    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_NonNativeMoveAssign)
    objectsAlive = 0;
    auto ptr = SharedPtr<NonNative>::create(10);
    auto ptr2 = SharedPtr<NonNative>::create(20);
    PROMEKI_TEST(objectsAlive == 2);

    ptr2 = std::move(ptr);
    PROMEKI_TEST(ptr.isNull() == true);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(ptr2->value == 10);
    PROMEKI_TEST(objectsAlive == 1);

    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Swap
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_Swap)
    objectsAlive = 0;
    auto a = SharedPtr<Base>::create(1);
    auto b = SharedPtr<Base>::create(2);

    a.swap(b);
    PROMEKI_TEST(a->value == 2);
    PROMEKI_TEST(b->value == 1);
    PROMEKI_TEST(a.referenceCount() == 1);
    PROMEKI_TEST(b.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);

    // Swap with null
    SharedPtr<Base> c;
    a.swap(c);
    PROMEKI_TEST(a.isNull() == true);
    PROMEKI_TEST(c->value == 2);

    c.clear();
    b.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Comparison operators
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_CompareEqual)
    objectsAlive = 0;
    auto a = SharedPtr<Base>::create(1);
    SharedPtr<Base> b = a;
    auto c = SharedPtr<Base>::create(2);
    SharedPtr<Base> null;

    // Same underlying object
    PROMEKI_TEST(a == b);
    PROMEKI_TEST(!(a != b));

    // Different underlying objects
    PROMEKI_TEST(a != c);
    PROMEKI_TEST(!(a == c));

    // Null comparisons
    PROMEKI_TEST(null == nullptr);
    PROMEKI_TEST(!(null != nullptr));
    PROMEKI_TEST(a != nullptr);
    PROMEKI_TEST(!(a == nullptr));

    // Two nulls
    SharedPtr<Base> null2;
    PROMEKI_TEST(null == null2);

    a.clear();
    b.clear();
    c.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_CompareAfterDetach)
    objectsAlive = 0;
    auto a = SharedPtr<Base>::create(1);
    SharedPtr<Base> b = a;
    PROMEKI_TEST(a == b);

    // After COW detach, they should no longer be equal
    b.modify()->value = 2;
    PROMEKI_TEST(a != b);

    a.clear();
    b.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Copy-on-write (native)
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NativeCopyOnWrite)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(100);
    SharedPtr<Base> ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(objectsAlive == 1);

    // modify() should detach ptr2 (copy the object)
    ptr2.modify()->value = 200;
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);
    PROMEKI_TEST(ptr->value == 100);
    PROMEKI_TEST(ptr2->value == 200);

    ptr.clear();
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

PROMEKI_TEST_BEGIN(SharedPtr_NativeCOWNoDetachWhenSingleRef)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(100);
    PROMEKI_TEST(ptr.referenceCount() == 1);

    // modify() when refcount is 1 should NOT create a copy
    const Base *before = ptr.ptr();
    ptr.modify()->value = 200;
    PROMEKI_TEST(ptr.ptr() == before);
    PROMEKI_TEST(ptr->value == 200);
    PROMEKI_TEST(objectsAlive == 1);

    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Copy-on-write (non-native)
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NonNativeCopyOnWrite)
    objectsAlive = 0;
    auto ptr = SharedPtr<NonNative>::create(100);
    SharedPtr<NonNative> ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(objectsAlive == 1);

    ptr2.modify()->value = 200;
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);
    PROMEKI_TEST(ptr->value == 100);
    PROMEKI_TEST(ptr2->value == 200);

    ptr.clear();
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// CopyOnWrite disabled
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NoCopyOnWrite)
    objectsAlive = 0;
    using NoCOWPtr = SharedPtr<Base, false>;
    auto ptr = NoCOWPtr::create(100);
    NoCOWPtr ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);

    // modify() with COW disabled should NOT detach
    ptr2.modify()->value = 200;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(objectsAlive == 1);
    // Both see the same modified data
    PROMEKI_TEST(ptr->value == 200);
    PROMEKI_TEST(ptr2->value == 200);

    ptr.clear();
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Polymorphic copy-on-write (native)
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_NativePolymorphicCOW)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::takeOwnership(new Derived(50));
    PROMEKI_TEST(ptr->getType() == "Derived");
    PROMEKI_TEST(ptr->value == 50);

    SharedPtr<Base> ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(objectsAlive == 1);

    // modify() should clone via Derived's _promeki_clone(), preserving type
    PROMEKI_TEST(ptr2.modify() != ptr.ptr());
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);
    PROMEKI_TEST(ptr2->getType() == "Derived");
    PROMEKI_TEST(ptr2->value == 50);

    ptr.clear();
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Explicit detach
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_ExplicitDetach)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(77);
    SharedPtr<Base> ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);

    ptr2.detach();
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);
    PROMEKI_TEST(ptr2->value == 77);

    // Detach on single ref should be no-op
    const Base *before = ptr.ptr();
    ptr.detach();
    PROMEKI_TEST(ptr.ptr() == before);
    PROMEKI_TEST(ptr.referenceCount() == 1);

    // Detach on null should be no-op
    SharedPtr<Base> null;
    null.detach();
    PROMEKI_TEST(null.isNull() == true);

    ptr.clear();
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Dereference operator
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_DereferenceOperator)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(99);
    const Base &ref = *ptr;
    PROMEKI_TEST(ref.value == 99);
    PROMEKI_TEST(ref.getType() == "Base");

    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Clear on null is safe
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_ClearNull)
    SharedPtr<Base> ptr;
    ptr.clear();  // Should not crash
    PROMEKI_TEST(ptr.isNull() == true);

    SharedPtr<NonNative> ptr2;
    ptr2.clear();
    PROMEKI_TEST(ptr2.isNull() == true);
PROMEKI_TEST_END();

// ============================================================================
// Assignment to null SharedPtr
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_AssignToNull)
    objectsAlive = 0;
    SharedPtr<Base> ptr;
    auto ptr2 = SharedPtr<Base>::create(5);

    ptr = ptr2;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(ptr->value == 5);

    ptr.clear();
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Chain of copies
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_ChainOfCopies)
    objectsAlive = 0;
    auto a = SharedPtr<Base>::create(1);
    SharedPtr<Base> b = a;
    SharedPtr<Base> c = b;
    SharedPtr<Base> d = c;
    PROMEKI_TEST(a.referenceCount() == 4);
    PROMEKI_TEST(objectsAlive == 1);

    b.clear();
    PROMEKI_TEST(a.referenceCount() == 3);
    c.clear();
    PROMEKI_TEST(a.referenceCount() == 2);
    d.clear();
    PROMEKI_TEST(a.referenceCount() == 1);
    a.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Reassignment releases old, acquires new
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_Reassignment)
    objectsAlive = 0;
    auto a = SharedPtr<Base>::create(1);
    auto b = SharedPtr<Base>::create(2);
    SharedPtr<Base> c = a;
    PROMEKI_TEST(a.referenceCount() == 2);
    PROMEKI_TEST(b.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);

    // Reassign c from a's object to b's object
    c = b;
    PROMEKI_TEST(a.referenceCount() == 1);
    PROMEKI_TEST(b.referenceCount() == 2);
    PROMEKI_TEST(c.referenceCount() == 2);
    PROMEKI_TEST(c->value == 2);
    PROMEKI_TEST(objectsAlive == 2);

    a.clear();
    b.clear();
    c.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// IsSharedObject trait
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_IsSharedObjectTrait)
    PROMEKI_TEST(IsSharedObject<Base>::value == true);
    PROMEKI_TEST(IsSharedObject<Derived>::value == true);
    PROMEKI_TEST(IsSharedObject<NonNative>::value == false);
    PROMEKI_TEST(SharedPtr<Base>::isNative == true);
    PROMEKI_TEST(SharedPtr<NonNative>::isNative == false);
PROMEKI_TEST_END();

// ============================================================================
// constexpr checks
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_StaticProperties)
    PROMEKI_TEST((SharedPtr<Base, true>::isCopyOnWrite == true));
    PROMEKI_TEST((SharedPtr<Base, false>::isCopyOnWrite == false));
    PROMEKI_TEST((SharedPtr<NonNative, true>::isCopyOnWrite == true));
    PROMEKI_TEST((SharedPtr<NonNative, false>::isCopyOnWrite == false));
PROMEKI_TEST_END();

// ============================================================================
// Thread safety tests
// ============================================================================

static int threadFuncBasic(int tid, SharedPtr<Base> ptr) {
    PROMEKI_TEST(ptr.referenceCount() > 1);
    PROMEKI_TEST(ptr->getType() == "Base");
    return 0;
}

PROMEKI_TEST_BEGIN(SharedPtr_ThreadSafe)
    objectsAlive = 0;
    const int ThreadCount = 10;
    auto ptr = SharedPtr<Base>::create();
    PROMEKI_TEST(ptr.referenceCount() == 1);

    std::thread *t[ThreadCount];
    for(int i = 0; i < ThreadCount; i++) {
        t[i] = new std::thread(threadFuncBasic, i, ptr);
    }
    for(int i = 0; i < ThreadCount; i++) {
        t[i]->join();
        delete t[i];
    }

    PROMEKI_TEST(ptr.referenceCount() == 1);
    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

static void threadFuncStress(SharedPtr<Base> ptr, int iterations) {
    for(int i = 0; i < iterations; i++) {
        SharedPtr<Base> local = ptr;
        // Just touch the object to ensure it's alive
        (void)local->value;
    }
}

PROMEKI_TEST_BEGIN(SharedPtr_ThreadStress)
    objectsAlive = 0;
    const int ThreadCount = 8;
    const int Iterations = 10000;
    auto ptr = SharedPtr<Base>::create(42);

    std::vector<std::thread> threads;
    for(int i = 0; i < ThreadCount; i++) {
        threads.emplace_back(threadFuncStress, ptr, Iterations);
    }
    for(auto &t : threads) {
        t.join();
    }

    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 42);
    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

static void threadFuncNonNativeStress(SharedPtr<NonNative> ptr, int iterations) {
    for(int i = 0; i < iterations; i++) {
        SharedPtr<NonNative> local = ptr;
        (void)local->value;
    }
}

PROMEKI_TEST_BEGIN(SharedPtr_NonNativeThreadStress)
    objectsAlive = 0;
    const int ThreadCount = 8;
    const int Iterations = 10000;
    auto ptr = SharedPtr<NonNative>::create(42);

    std::vector<std::thread> threads;
    for(int i = 0; i < ThreadCount; i++) {
        threads.emplace_back(threadFuncNonNativeStress, ptr, Iterations);
    }
    for(auto &t : threads) {
        t.join();
    }

    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr->value == 42);
    ptr.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Scope-based lifetime
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_ScopeLifetime)
    objectsAlive = 0;
    auto outer = SharedPtr<Base>::create(1);
    PROMEKI_TEST(objectsAlive == 1);
    {
        SharedPtr<Base> inner = outer;
        PROMEKI_TEST(outer.referenceCount() == 2);
        PROMEKI_TEST(objectsAlive == 1);
    }
    PROMEKI_TEST(outer.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 1);
    outer.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Multiple COW detaches
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_MultipleCOWDetach)
    objectsAlive = 0;
    auto a = SharedPtr<Base>::create(1);
    SharedPtr<Base> b = a;
    SharedPtr<Base> c = a;
    PROMEKI_TEST(a.referenceCount() == 3);
    PROMEKI_TEST(objectsAlive == 1);

    // First COW detach
    b.modify()->value = 2;
    PROMEKI_TEST(a.referenceCount() == 2);
    PROMEKI_TEST(b.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);

    // Second COW detach
    c.modify()->value = 3;
    PROMEKI_TEST(a.referenceCount() == 1);
    PROMEKI_TEST(c.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 3);

    PROMEKI_TEST(a->value == 1);
    PROMEKI_TEST(b->value == 2);
    PROMEKI_TEST(c->value == 3);

    a.clear();
    b.clear();
    c.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Assign null SharedPtr
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_AssignNullSharedPtr)
    objectsAlive = 0;
    auto ptr = SharedPtr<Base>::create(1);
    SharedPtr<Base> null;

    ptr = null;
    PROMEKI_TEST(ptr.isNull() == true);
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();

// ============================================================================
// Move from shared (multi-ref) pointer
// ============================================================================

PROMEKI_TEST_BEGIN(SharedPtr_MoveFromShared)
    objectsAlive = 0;
    auto a = SharedPtr<Base>::create(1);
    SharedPtr<Base> b = a;
    PROMEKI_TEST(a.referenceCount() == 2);

    // Move b — a should still hold the object with refcount unchanged
    SharedPtr<Base> c = std::move(b);
    PROMEKI_TEST(b.isNull() == true);
    PROMEKI_TEST(a.referenceCount() == 2);
    PROMEKI_TEST(c.referenceCount() == 2);
    PROMEKI_TEST(objectsAlive == 1);

    a.clear();
    c.clear();
    PROMEKI_TEST(objectsAlive == 0);
PROMEKI_TEST_END();
