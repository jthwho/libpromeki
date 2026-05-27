/**
 * @file      framesyncdisposition.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/framesyncdisposition.h>

using namespace promeki;

TEST_CASE("FrameSyncDisposition factories") {
        SUBCASE("play()") {
                FrameSyncDisposition d = FrameSyncDisposition::play();
                CHECK(d.kind() == FrameSyncDisposition::Play);
                CHECK(d.repeatCount() == 0);
        }
        SUBCASE("drop()") {
                FrameSyncDisposition d = FrameSyncDisposition::drop();
                CHECK(d.kind() == FrameSyncDisposition::Drop);
                CHECK(d.repeatCount() == 0);
        }
        SUBCASE("repeat() default count is 1") {
                FrameSyncDisposition d = FrameSyncDisposition::repeat();
                CHECK(d.kind() == FrameSyncDisposition::Repeat);
                CHECK(d.repeatCount() == FrameSyncDisposition::DefaultRepeatCount);
                CHECK(d.repeatCount() == 1);
        }
        SUBCASE("repeat(N) honours explicit count") {
                FrameSyncDisposition d = FrameSyncDisposition::repeat(3);
                CHECK(d.kind() == FrameSyncDisposition::Repeat);
                CHECK(d.repeatCount() == 3);
        }
        SUBCASE("repeat(0) is allowed and stores 0") {
                // Boundary: caller asks for zero repeats.  The class
                // doesn't reinterpret the value; the consumer (AncFrameSync)
                // is the one that decides what zero means.
                FrameSyncDisposition d = FrameSyncDisposition::repeat(0);
                CHECK(d.kind() == FrameSyncDisposition::Repeat);
                CHECK(d.repeatCount() == 0);
        }
        SUBCASE("repeat(255) honours the max uint8_t") {
                FrameSyncDisposition d = FrameSyncDisposition::repeat(255);
                CHECK(d.kind() == FrameSyncDisposition::Repeat);
                CHECK(d.repeatCount() == 255);
        }
}

TEST_CASE("FrameSyncDisposition default-construction") {
        FrameSyncDisposition d;
        CHECK(d.kind() == FrameSyncDisposition::Play);
        CHECK(d.repeatCount() == 0);
}

TEST_CASE("FrameSyncDisposition value semantics") {
        SUBCASE("Copy preserves kind and count") {
                FrameSyncDisposition src = FrameSyncDisposition::repeat(7);
                FrameSyncDisposition copy(src);
                CHECK(copy.kind() == FrameSyncDisposition::Repeat);
                CHECK(copy.repeatCount() == 7);
        }
        SUBCASE("Assignment overwrites kind and count") {
                FrameSyncDisposition d = FrameSyncDisposition::play();
                d = FrameSyncDisposition::repeat(2);
                CHECK(d.kind() == FrameSyncDisposition::Repeat);
                CHECK(d.repeatCount() == 2);
                d = FrameSyncDisposition::drop();
                CHECK(d.kind() == FrameSyncDisposition::Drop);
                CHECK(d.repeatCount() == 0);
        }
}

TEST_CASE("FrameSyncDisposition equality") {
        SUBCASE("Same kind and count compare equal") {
                CHECK(FrameSyncDisposition::play() == FrameSyncDisposition::play());
                CHECK(FrameSyncDisposition::drop() == FrameSyncDisposition::drop());
                CHECK(FrameSyncDisposition::repeat(3) == FrameSyncDisposition::repeat(3));
        }
        SUBCASE("Different kind compares unequal") {
                CHECK(FrameSyncDisposition::play() != FrameSyncDisposition::drop());
                CHECK(FrameSyncDisposition::play() != FrameSyncDisposition::repeat(0));
        }
        SUBCASE("Same kind, different count compares unequal") {
                CHECK(FrameSyncDisposition::repeat(1) != FrameSyncDisposition::repeat(2));
        }
}

TEST_CASE("FrameSyncDisposition is constexpr-usable") {
        // Compile-time evaluation proves the type stays a Simple data
        // object suitable for static initialisation.
        constexpr FrameSyncDisposition p = FrameSyncDisposition::play();
        constexpr FrameSyncDisposition d = FrameSyncDisposition::drop();
        constexpr FrameSyncDisposition r = FrameSyncDisposition::repeat(4);
        static_assert(p.kind() == FrameSyncDisposition::Play);
        static_assert(d.kind() == FrameSyncDisposition::Drop);
        static_assert(r.kind() == FrameSyncDisposition::Repeat);
        static_assert(r.repeatCount() == 4);
        // Anchor a runtime CHECK so the test isn't elided.
        CHECK(p.kind() == FrameSyncDisposition::Play);
}
