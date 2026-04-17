/**
 * @file      bufferpool.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/bufferpool.h>
#include <promeki/buffer.h>

using namespace promeki;

TEST_CASE("BufferPool: default construct is invalid") {
        BufferPool pool;
        CHECK(pool.bufferSize() == 0);
        CHECK(pool.available() == 0);
}

TEST_CASE("BufferPool: construct with size/alignment") {
        BufferPool pool(4096, 4096);
        CHECK(pool.bufferSize() == 4096);
        CHECK(pool.alignment() == 4096);
        CHECK(pool.available() == 0);
}

TEST_CASE("BufferPool: reserve pre-allocates") {
        BufferPool pool(1024, 4096);
        pool.reserve(4);
        CHECK(pool.available() == 4);
}

TEST_CASE("BufferPool: acquire + release round trip") {
        BufferPool pool(2048, 4096);
        pool.reserve(2);
        CHECK(pool.available() == 2);

        Buffer b1 = pool.acquire();
        CHECK(b1.isValid());
        CHECK(b1.allocSize() == 2048);
        CHECK(pool.available() == 1);

        Buffer b2 = pool.acquire();
        CHECK(b2.isValid());
        CHECK(pool.available() == 0);

        pool.release(std::move(b1));
        CHECK(pool.available() == 1);
        pool.release(std::move(b2));
        CHECK(pool.available() == 2);
}

TEST_CASE("BufferPool: acquire from empty pool allocates fresh") {
        BufferPool pool(512, 4096);
        CHECK(pool.available() == 0);
        Buffer b = pool.acquire();
        CHECK(b.isValid());
        CHECK(b.allocSize() == 512);
        CHECK(pool.available() == 0);
}

TEST_CASE("BufferPool: released buffer is reused on next acquire") {
        BufferPool pool(1024, 4096);
        pool.reserve(1);

        Buffer b1 = pool.acquire();
        void *p1 = b1.data();
        pool.release(std::move(b1));
        Buffer b2 = pool.acquire();
        // Same memory region is reused.
        CHECK(b2.data() == p1);
}

TEST_CASE("BufferPool: release rejects shape mismatch") {
        BufferPool pool(1024, 4096);
        Buffer wrong(2048, 4096);
        pool.release(std::move(wrong));
        CHECK(pool.available() == 0);
}

TEST_CASE("BufferPool: acquired buffer is usable for writes") {
        BufferPool pool(4096, 4096);
        Buffer b = pool.acquire();
        REQUIRE(b.isValid());
        std::memset(b.data(), 0x42, 4096);
        CHECK(static_cast<uint8_t *>(b.data())[0] == 0x42);
        CHECK(static_cast<uint8_t *>(b.data())[4095] == 0x42);
        pool.release(std::move(b));
}

TEST_CASE("BufferPool: clear drops all free buffers") {
        BufferPool pool(512, 4096);
        pool.reserve(4);
        CHECK(pool.available() == 4);
        pool.clear();
        CHECK(pool.available() == 0);
}

TEST_CASE("BufferPool: released buffer view is reset to base") {
        BufferPool pool(4096, 4096);
        Buffer b = pool.acquire();
        // Shift the view + set a size, then release.
        b.shiftData(128);
        b.setSize(100);
        pool.release(std::move(b));
        // Next acquire should land back at offset 0 with size=0.
        Buffer b2 = pool.acquire();
        CHECK(b2.size() == 0);
        CHECK(b2.availSize() == 4096);
}
