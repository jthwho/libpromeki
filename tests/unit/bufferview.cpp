/**
 * @file      tests/bufferview.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/bufferview.h>
#include <cstring>

using namespace promeki;

TEST_CASE("BufferView") {

        SUBCASE("default construction") {
                BufferView view;
                CHECK(view.isNull());
                CHECK_FALSE(view.isValid());
                CHECK(view.data() == nullptr);
                CHECK(view.offset() == 0);
                CHECK(view.size() == 0);
        }

        SUBCASE("construction with buffer") {
                auto buf = Buffer::Ptr::create(1024);
                buf->setSize(1024);
                BufferView view(buf, 0, 512);
                CHECK(view.isValid());
                CHECK_FALSE(view.isNull());
                CHECK(view.offset() == 0);
                CHECK(view.size() == 512);
                CHECK(view.data() == static_cast<const uint8_t *>(buf->data()));
        }

        SUBCASE("offset into buffer") {
                auto buf = Buffer::Ptr::create(4096);
                buf->setSize(4096);
                std::memset(buf->data(), 0, 4096);
                static_cast<uint8_t *>(buf->data())[1000] = 0xAA;
                static_cast<uint8_t *>(buf->data())[1001] = 0xBB;

                BufferView view(buf, 1000, 200);
                CHECK(view.offset() == 1000);
                CHECK(view.size() == 200);
                CHECK(view.data()[0] == 0xAA);
                CHECK(view.data()[1] == 0xBB);
        }

        SUBCASE("multiple views sharing one buffer") {
                auto buf = Buffer::Ptr::create(4200);
                buf->setSize(4200);

                BufferView v1(buf, 0, 1400);
                BufferView v2(buf, 1400, 1400);
                BufferView v3(buf, 2800, 1400);

                CHECK(v1.buffer().ptr() == v2.buffer().ptr());
                CHECK(v2.buffer().ptr() == v3.buffer().ptr());

                CHECK(v1.offset() == 0);
                CHECK(v2.offset() == 1400);
                CHECK(v3.offset() == 2800);

                const uint8_t *base = static_cast<const uint8_t *>(buf->data());
                CHECK(v1.data() == base);
                CHECK(v2.data() == base + 1400);
                CHECK(v3.data() == base + 2800);
        }

        SUBCASE("mutable data access") {
                auto buf = Buffer::Ptr::create(256);
                buf->setSize(256);
                BufferView view(buf, 0, 256);
                uint8_t *d = view.data();
                REQUIRE(d != nullptr);
                d[0] = 0x42;
                CHECK(static_cast<const uint8_t *>(buf->data())[0] == 0x42);
        }

        SUBCASE("view list") {
                auto buf = Buffer::Ptr::create(3000);
                buf->setSize(3000);

                BufferView views;
                for(size_t i = 0; i < 3; i++) {
                        views.pushToBack(buf, i * 1000, 1000);
                }
                CHECK(views.count() == 3);
                CHECK(views[0].offset() == 0);
                CHECK(views[1].offset() == 1000);
                CHECK(views[2].offset() == 2000);
        }

        SUBCASE("copy semantics") {
                auto buf = Buffer::Ptr::create(512);
                buf->setSize(512);
                BufferView v1(buf, 100, 200);
                BufferView v2 = v1;
                CHECK(v2.buffer().ptr() == v1.buffer().ptr());
                CHECK(v2.offset() == 100);
                CHECK(v2.size() == 200);
        }
}

TEST_CASE("BufferViewList domain operations") {

        SUBCASE("totalSize sums every view") {
                auto a = Buffer::Ptr::create(1024);
                auto b = Buffer::Ptr::create(2048);
                a->setSize(1024);
                b->setSize(2048);
                BufferView list = {
                        BufferView(a, 0,   200),
                        BufferView(a, 200, 300),
                        BufferView(b, 0,   1000),
                };
                CHECK(list.totalSize() == 200 + 300 + 1000);
        }

        SUBCASE("isExclusive: list-exclusive when every ref is in-list") {
                // Two views over the same buffer — no external
                // holder, so list is exclusive relative to itself.
                auto buf = Buffer::Ptr::create(1024);
                buf->setSize(1024);
                BufferView list = {
                        BufferView(buf, 0,   512),
                        BufferView(buf, 512, 512),
                };
                // Drop the external handle so only the list's two
                // views reference the buffer.
                buf.clear();
                CHECK(list.isExclusive());
        }

        SUBCASE("isExclusive: false when any view's buffer has external ref") {
                auto buf = Buffer::Ptr::create(1024);
                buf->setSize(1024);
                BufferView list = {
                        BufferView(buf, 0, 1024),
                };
                // `buf` is still held by the caller — external ref exists.
                CHECK_FALSE(list.isExclusive());
        }

        SUBCASE("ensureExclusive: dedup clones one-per-unique-buffer") {
                // Three views share the same backing buffer plus an
                // external caller-held Ptr.  ensureExclusive must
                // clone the buffer exactly once and redirect all
                // three views to the same clone — not three
                // independent copies.
                auto buf = Buffer::Ptr::create(1024);
                buf->setSize(1024);
                const Buffer *originalKey = buf.ptr();
                BufferView list = {
                        BufferView(buf, 0,   256),
                        BufferView(buf, 256, 256),
                        BufferView(buf, 512, 512),
                };
                REQUIRE_FALSE(list.isExclusive());

                list.ensureExclusive();

                // Every view was redirected off the original.
                CHECK(list[0].buffer().ptr() != originalKey);
                CHECK(list[1].buffer().ptr() != originalKey);
                CHECK(list[2].buffer().ptr() != originalKey);
                // And all three views now reference the SAME clone —
                // only one CoW happened, not three.
                const Buffer *cloneKey = list[0].buffer().ptr();
                CHECK(list[1].buffer().ptr() == cloneKey);
                CHECK(list[2].buffer().ptr() == cloneKey);
                // Offsets / sizes preserved.
                CHECK(list[0].offset() == 0);   CHECK(list[0].size() == 256);
                CHECK(list[1].offset() == 256); CHECK(list[1].size() == 256);
                CHECK(list[2].offset() == 512); CHECK(list[2].size() == 512);
                // Caller's `buf` still points at the unchanged original.
                CHECK(buf.ptr() == originalKey);
                // And the list is now exclusive.
                CHECK(list.isExclusive());
        }

        SUBCASE("ensureExclusive: list-exclusive buffers are left alone") {
                // No external holder: drop the caller's Ptr before
                // the check.  The list's two views each hold the
                // buffer, so refcount == in-list count == 2 — no
                // clone needed.
                auto buf = Buffer::Ptr::create(1024);
                buf->setSize(1024);
                BufferView list = {
                        BufferView(buf, 0,   512),
                        BufferView(buf, 512, 512),
                };
                const Buffer *key = buf.ptr();
                buf.clear();
                REQUIRE(list.isExclusive());

                list.ensureExclusive();
                // Buffers unchanged — no clone happened.
                CHECK(list[0].buffer().ptr() == key);
                CHECK(list[1].buffer().ptr() == key);
        }

        SUBCASE("ensureExclusive: mixed — one shared, one exclusive") {
                auto shared = Buffer::Ptr::create(1024);
                auto unique = Buffer::Ptr::create(512);
                shared->setSize(1024);
                unique->setSize(512);
                BufferView list = {
                        BufferView(shared, 0, 1024),   // externally held → clone
                        BufferView(unique, 0, 512),    // externally held → clone
                };
                const Buffer *sharedKey = shared.ptr();
                const Buffer *uniqueKey = unique.ptr();
                // Both buffers have an external holder (the local
                // Ptrs).  ensureExclusive must clone both.
                list.ensureExclusive();
                CHECK(list[0].buffer().ptr() != sharedKey);
                CHECK(list[1].buffer().ptr() != uniqueKey);
                // And the two views still point at different
                // clones (not the same one).
                CHECK(list[0].buffer().ptr() != list[1].buffer().ptr());
        }
}
