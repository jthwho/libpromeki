/**
 * @file      sharedptr.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/sharedptr.h>

using namespace promeki;

PROMEKI_DEBUG(SharedPtrTest);

static std::atomic<int> objectsAlive = 0;

class Base {
    PROMEKI_SHARED(Base)
public:
    Base() {
        objectsAlive++;
    }

    Base(const Base &o) {
        objectsAlive++;
    }

    virtual ~Base() {
        objectsAlive--;
    }
    virtual std::string getType() const { return "Base"; }
};

class Derived : public Base {
    PROMEKI_SHARED_DERIVED(Base, Derived)
public:
    Derived() = default;
    Derived(const Derived &o) = default;
    virtual ~Derived() = default;
    virtual std::string getType() const override { return "Derived"; }
};

class NonNative {
public:
    NonNative() {
        objectsAlive++;
    }
    ~NonNative() {
        objectsAlive--;
    }
    std::string getType() const { return "NonNative"; }
};


PROMEKI_TEST_BEGIN(SharedPtr_NativeBasic)
    // Test basic functionality with native objects
    SharedPtr<Base> ptr(new Base());
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr.isNull() == false);
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

    ptr = ptr;
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

PROMEKI_TEST_BEGIN(SharedPtr_NonNativeBasic)
    // Test basic functionality with native objects
    SharedPtr<NonNative> ptr(new NonNative());
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

PROMEKI_TEST_BEGIN(SharedPtr_NativeObjectPolymorphicCopy) 
    SharedPtr<Base> ptr(new Derived());
    PROMEKI_TEST(ptr.isValid() == true);
    PROMEKI_TEST(ptr->getType() == "Derived");

    SharedPtr<Base> ptr2 = ptr;
    PROMEKI_TEST(ptr.referenceCount() == 2);
    PROMEKI_TEST(ptr2.referenceCount() == 2);
    PROMEKI_TEST(ptr2->getType() == "Derived");
    PROMEKI_TEST(objectsAlive == 1);

    PROMEKI_TEST(ptr2.modify() != ptr.ptr()); // ptr2 should now be detached.
    PROMEKI_TEST(ptr.referenceCount() == 1);
    PROMEKI_TEST(ptr2.referenceCount() == 1);
    PROMEKI_TEST(objectsAlive == 2);
    PROMEKI_TEST(ptr2->getType() == "Derived");

    ptr.clear();
    PROMEKI_TEST(objectsAlive == 1);
    ptr2.clear();
    PROMEKI_TEST(objectsAlive == 0);

PROMEKI_TEST_END();


int threadFunc(int tid, SharedPtr<Base> ptr) {
    PROMEKI_TEST(ptr.referenceCount() > 1);
    PROMEKI_TEST(ptr->getType() == "Base");
    return 0;
}

PROMEKI_TEST_BEGIN(SharedPtr_ThreadSafe) 
    const int ThreadCount = 10;
    SharedPtr<Base> ptr(new Base());
    PROMEKI_TEST(ptr.referenceCount() == 1);

    std::thread *t[ThreadCount];
    for(int i = 0; i < ThreadCount; i++) {
        t[i] = new std::thread(threadFunc, i, ptr);
    }
    for(int i = 0; i < ThreadCount; i++) {
        t[i]->join();
        delete t[i];
    }

    PROMEKI_TEST(ptr.referenceCount() == 1);

PROMEKI_TEST_END();

#if 0
// Test basic functionality with non-native objects
TEST(SharedPtrTest, NonNativeObjectBasic) {
    SharedPtr<NonNative> ptr(new NonNative());
    EXPECT_TRUE(ptr.isValid());
    EXPECT_EQ(ptr->getType(), "NonNative");
    EXPECT_EQ(ptr.referenceCount(), 1);

    SharedPtr<NonNative> ptr2 = ptr;
    EXPECT_EQ(ptr.referenceCount(), 2);
    EXPECT_EQ(ptr2.referenceCount(), 2);
    EXPECT_EQ(ptr2->getType(), "NonNative");

    ptr2.clear();
    EXPECT_EQ(ptr.referenceCount(), 1);
    EXPECT_TRUE(ptr2.isNull());
}

// Test polymorphic copying with native objects
TEST(SharedPtrTest, NativeObjectPolymorphicCopy) {
    SharedPtr<Base> ptr(new Derived());
    EXPECT_TRUE(ptr.isValid());
    EXPECT_EQ(ptr->getType(), "Derived");

    SharedPtr<Base> ptr2 = ptr;
    EXPECT_EQ(ptr.referenceCount(), 2);
    EXPECT_EQ(ptr2.referenceCount(), 2);
    EXPECT_EQ(ptr2->getType(), "Derived");

    ptr2 = new Base();
    EXPECT_EQ(ptr.referenceCount(), 1);
    EXPECT_EQ(ptr2.referenceCount(), 1);
    EXPECT_EQ(ptr2->getType(), "Base");
}

// Test thread safety with native objects
void threadFunc(SharedPtr<Base> ptr) {
    EXPECT_EQ(ptr.referenceCount(), 2);
    EXPECT_EQ(ptr->getType(), "Base");
}

TEST(SharedPtrTest, NativeObjectThreadSafety) {
    SharedPtr<Base> ptr(new Base());
    EXPECT_EQ(ptr.referenceCount(), 1);

    std::thread t(threadFunc, ptr);
    t.join();

    EXPECT_EQ(ptr.referenceCount(), 1);
}

// Test thread safety with non-native objects
void threadFuncNonNative(SharedPtr<NonNative> ptr) {
    EXPECT_EQ(ptr.referenceCount(), 2);
    EXPECT_EQ(ptr->getType(), "NonNative");
}

TEST(SharedPtrTest, NonNativeObjectThreadSafety) {
    SharedPtr<NonNative> ptr(new NonNative());
    EXPECT_EQ(ptr.referenceCount(), 1);

    std::thread t(threadFuncNonNative, ptr);
    t.join();

    EXPECT_EQ(ptr.referenceCount(), 1);
}

PROMEKI_TEST_END()
#endif

