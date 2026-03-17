/**
 * @file      tests/bufferview.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/bufferview.h>
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

                BufferView::List views;
                for(size_t i = 0; i < 3; i++) {
                        views.pushToBack(BufferView(buf, i * 1000, 1000));
                }
                CHECK(views.size() == 3);
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
