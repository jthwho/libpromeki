/**
 * @file      securebuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/buffer.h>
#include <promeki/core/memspace.h>

using namespace promeki;

static const MemSpace Secure(MemSpace::SystemSecure);

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("SecureBuffer_Default") {
        Buffer b;
        CHECK(!b.isValid());
        CHECK(b.data() == nullptr);
        CHECK(b.size() == 0);
}

TEST_CASE("SecureBuffer_Allocate") {
        Buffer b(1024, Buffer::DefaultAlign, Secure);
        CHECK(b.isValid());
        CHECK(b.data() != nullptr);
        CHECK(b.size() == 0);
        CHECK(b.availSize() == 1024);
        CHECK(b.memSpace().id() == MemSpace::SystemSecure);
}

TEST_CASE("SecureBuffer_AllocateWithAlign") {
        Buffer b(4096, 64, Secure);
        CHECK(b.isValid());
        CHECK(b.availSize() == 4096);
        CHECK(b.align() == 64);
        uintptr_t addr = reinterpret_cast<uintptr_t>(b.data());
        CHECK(addr % 64 == 0);
}

// ============================================================================
// Secure zeroing on destruction (owned)
// ============================================================================

TEST_CASE("SecureBuffer_DestructionDoesNotCrash") {
        void *raw = nullptr;
        {
                Buffer b(128, Buffer::DefaultAlign, Secure);
                REQUIRE(b.isValid());
                CHECK(b.fill(0xCD).isOk());
                raw = b.odata();
        }
        CHECK(raw != nullptr);
}

// ============================================================================
// Fill
// ============================================================================

TEST_CASE("SecureBuffer_Fill") {
        Buffer b(128, Buffer::DefaultAlign, Secure);
        REQUIRE(b.isValid());
        CHECK(b.fill(0xCD).isOk());
        const uint8_t *ptr = static_cast<const uint8_t *>(b.data());
        CHECK(ptr[0] == 0xCD);
        CHECK(ptr[63] == 0xCD);
        CHECK(ptr[127] == 0xCD);
}

// ============================================================================
// Copy semantics — copies inherit the MemSpace
// ============================================================================

TEST_CASE("SecureBuffer_CopyIsIndependent") {
        Buffer b1(256, Buffer::DefaultAlign, Secure);
        CHECK(b1.fill(0x42).isOk());

        Buffer b2 = b1;
        CHECK(b2.isValid());
        CHECK(b2.availSize() == b1.availSize());
        CHECK(b2.data() != b1.data());
        CHECK(b2.memSpace().id() == MemSpace::SystemSecure);

        const uint8_t *p1 = static_cast<const uint8_t *>(b1.data());
        const uint8_t *p2 = static_cast<const uint8_t *>(b2.data());
        CHECK(p2[0] == 0x42);
        CHECK(p2[255] == 0x42);

        b2.fill(0x00);
        CHECK(p1[0] == 0x42);
        CHECK(p2[0] == 0x00);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST_CASE("SecureBuffer_Move") {
        Buffer b1(512, Buffer::DefaultAlign, Secure);
        CHECK(b1.fill(0x77).isOk());
        void *origData = b1.data();

        Buffer b2(std::move(b1));
        CHECK(b2.isValid());
        CHECK(b2.data() == origData);
        CHECK(b2.memSpace().id() == MemSpace::SystemSecure);
        CHECK(!b1.isValid());
}

// ============================================================================
// Assignment
// ============================================================================

TEST_CASE("SecureBuffer_Assignment") {
        Buffer b1(128, Buffer::DefaultAlign, Secure);
        CHECK(b1.fill(0xAA).isOk());

        Buffer b2(64, Buffer::DefaultAlign, Secure);
        CHECK(b2.fill(0xBB).isOk());

        b1 = b2;
        CHECK(b1.availSize() == 64);
        const uint8_t *p = static_cast<const uint8_t *>(b1.data());
        CHECK(p[0] == 0xBB);
}

// ============================================================================
// SharedPtr support
// ============================================================================

TEST_CASE("SecureBuffer_SharedPtr") {
        auto p1 = Buffer::Ptr::create(256, Buffer::DefaultAlign, Secure);
        CHECK(p1->isValid());
        CHECK(p1->availSize() == 256);
        CHECK(p1->memSpace().id() == MemSpace::SystemSecure);
        CHECK(p1.referenceCount() == 1);

        Buffer::Ptr p2 = p1;
        CHECK(p1.referenceCount() == 2);
        CHECK(p1->data() == p2->data());
}

// ============================================================================
// PtrList
// ============================================================================

TEST_CASE("SecureBuffer_PtrList") {
        Buffer::PtrList list;
        list.pushToBack(Buffer::Ptr::create(64, Buffer::DefaultAlign, Secure));
        list.pushToBack(Buffer::Ptr::create(128, Buffer::DefaultAlign, Secure));
        CHECK(list.size() == 2);
        CHECK(list[0]->availSize() == 64);
        CHECK(list[1]->availSize() == 128);
}
