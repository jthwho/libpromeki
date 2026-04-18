/**
 * @file      sharedmemory.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/sharedmemory.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/uuid.h>

using namespace promeki;

// ============================================================================
// Helper: produce a unique region name per test so parallel or repeated runs
// don't collide.  The UUID hex string (no slashes after our leading '/') is
// well within all known SHM_NAME_MAX limits.
// ============================================================================

static String uniqueShmName(const char *tag) {
        return String("/promeki-test-") + String(tag) + String("-") +
               UUID::generateV4().toString();
}

TEST_CASE("SharedMemory: isSupported is true on POSIX") {
#if defined(PROMEKI_PLATFORM_POSIX)
        CHECK(SharedMemory::isSupported());
#else
        CHECK_FALSE(SharedMemory::isSupported());
#endif
}

TEST_CASE("SharedMemory: create maps a writable region") {
        if(!SharedMemory::isSupported()) return;
        SharedMemory shm;
        Error err = shm.create(uniqueShmName("create"), 4096);
        REQUIRE(err.isOk());
        CHECK(shm.isValid());
        CHECK(shm.isOwner());
        CHECK(shm.size() == 4096);
        CHECK(shm.access() == SharedMemory::ReadWrite);
        REQUIRE(shm.data() != nullptr);

        // Basic write-through — destructor unlinks.
        auto *p = static_cast<uint8_t *>(shm.data());
        p[0] = 0xAB;
        p[4095] = 0xCD;
        CHECK(p[0] == 0xAB);
        CHECK(p[4095] == 0xCD);
}

TEST_CASE("SharedMemory: open sees owner's writes") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("rw");

        SharedMemory owner;
        REQUIRE(owner.create(name, 256).isOk());
        auto *op = static_cast<uint32_t *>(owner.data());
        for(size_t i = 0; i < 64; ++i) op[i] = static_cast<uint32_t>(i * 17 + 3);

        SharedMemory reader;
        Error err = reader.open(name, SharedMemory::ReadOnly);
        REQUIRE(err.isOk());
        CHECK(reader.isValid());
        CHECK_FALSE(reader.isOwner());
        CHECK(reader.size() == 256);
        CHECK(reader.access() == SharedMemory::ReadOnly);
        REQUIRE(reader.data() != nullptr);

        const auto *rp = static_cast<const uint32_t *>(reader.data());
        for(size_t i = 0; i < 64; ++i) {
                CHECK(rp[i] == static_cast<uint32_t>(i * 17 + 3));
        }
}

TEST_CASE("SharedMemory: second create with the same name fails with Exists") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("collide");
        SharedMemory a;
        REQUIRE(a.create(name, 4096).isOk());

        SharedMemory b;
        Error err = b.create(name, 4096);
        CHECK(err == Error::Exists);
        CHECK_FALSE(b.isValid());
}

TEST_CASE("SharedMemory: open on a missing name fails with NotExist") {
        if(!SharedMemory::isSupported()) return;
        SharedMemory shm;
        Error err = shm.open(uniqueShmName("missing"));
        CHECK(err == Error::NotExist);
        CHECK_FALSE(shm.isValid());
}

TEST_CASE("SharedMemory: create rejects embedded slash") {
        if(!SharedMemory::isSupported()) return;
        SharedMemory shm;
        Error err = shm.create(String("/bad/name"), 4096);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(shm.isValid());
}

TEST_CASE("SharedMemory: create rejects empty name") {
        if(!SharedMemory::isSupported()) return;
        SharedMemory shm;
        Error err = shm.create(String(), 4096);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(shm.isValid());
}

TEST_CASE("SharedMemory: create rejects zero size") {
        if(!SharedMemory::isSupported()) return;
        SharedMemory shm;
        Error err = shm.create(uniqueShmName("zero"), 0);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(shm.isValid());
}

TEST_CASE("SharedMemory: leading slash is added if missing") {
        if(!SharedMemory::isSupported()) return;
        // Pass a name without leading slash; open should find it with or
        // without the prefix.
        String bare = String("promeki-test-noslash-") +
                      UUID::generateV4().toString();
        SharedMemory owner;
        REQUIRE(owner.create(bare, 64).isOk());
        CHECK(owner.name() == String("/") + bare);

        SharedMemory reader;
        // Open with same form as create (no slash) → must succeed.
        REQUIRE(reader.open(bare).isOk());
        CHECK(reader.name() == String("/") + bare);
}

TEST_CASE("SharedMemory: close on owner unlinks the name") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("unlink");

        {
                SharedMemory owner;
                REQUIRE(owner.create(name, 64).isOk());
                owner.close();
                CHECK_FALSE(owner.isValid());
        }
        // After owner close + dtor, open must fail.
        SharedMemory reader;
        Error err = reader.open(name);
        CHECK(err == Error::NotExist);
}

TEST_CASE("SharedMemory: non-owner close leaves the name intact") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("nonowner-close");

        SharedMemory owner;
        REQUIRE(owner.create(name, 64).isOk());
        auto *op = static_cast<uint8_t *>(owner.data());
        op[0] = 0x42;

        {
                SharedMemory reader;
                REQUIRE(reader.open(name).isOk());
                reader.close();
                CHECK_FALSE(reader.isValid());
        }

        // Owner is still alive and a second opener should still succeed.
        SharedMemory reader2;
        Error err = reader2.open(name);
        REQUIRE(err.isOk());
        CHECK(static_cast<const uint8_t *>(reader2.data())[0] == 0x42);
}

TEST_CASE("SharedMemory: destructor cleans up without explicit close") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("dtor");
        {
                SharedMemory owner;
                REQUIRE(owner.create(name, 64).isOk());
                static_cast<uint8_t *>(owner.data())[0] = 0xFE;
        }
        SharedMemory probe;
        CHECK(probe.open(name) == Error::NotExist);
}

TEST_CASE("SharedMemory: move construction transfers ownership") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("move-ctor");

        SharedMemory src;
        REQUIRE(src.create(name, 128).isOk());
        void *addr = src.data();
        CHECK(src.isValid());
        CHECK(src.isOwner());

        SharedMemory dst(std::move(src));
        CHECK(dst.isValid());
        CHECK(dst.isOwner());
        CHECK(dst.data() == addr);
        CHECK(dst.size() == 128);

        // Moved-from object must be empty.
        CHECK_FALSE(src.isValid());
        CHECK_FALSE(src.isOwner());
        CHECK(src.data() == nullptr);
        CHECK(src.size() == 0);

        // dst dtor should unlink cleanly.
}

TEST_CASE("SharedMemory: move assignment closes target and transfers") {
        if(!SharedMemory::isSupported()) return;
        String aName = uniqueShmName("move-a");
        String bName = uniqueShmName("move-b");

        SharedMemory a;
        REQUIRE(a.create(aName, 64).isOk());
        SharedMemory b;
        REQUIRE(b.create(bName, 128).isOk());

        a = std::move(b);
        CHECK(a.isValid());
        CHECK(a.size() == 128);
        CHECK_FALSE(b.isValid());

        // aName should already be unlinked (a's previous region was closed).
        SharedMemory probeA;
        CHECK(probeA.open(aName) == Error::NotExist);

        // bName is still alive via a.
        SharedMemory probeB;
        CHECK(probeB.open(bName).isOk());
}

TEST_CASE("SharedMemory: open fails after creator close even with a stale consumer") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("stale");

        SharedMemory owner;
        REQUIRE(owner.create(name, 64).isOk());
        SharedMemory reader;
        REQUIRE(reader.open(name).isOk());

        owner.close();

        // The previously mapped reader keeps its mapping — POSIX semantics.
        CHECK(reader.isValid());
        // But a fresh open must now fail.
        SharedMemory newReader;
        CHECK(newReader.open(name) == Error::NotExist);
}

TEST_CASE("SharedMemory: opening same region twice creates two independent maps") {
        if(!SharedMemory::isSupported()) return;
        String name = uniqueShmName("two-readers");

        SharedMemory owner;
        REQUIRE(owner.create(name, 64).isOk());
        static_cast<uint8_t *>(owner.data())[0] = 0x11;

        SharedMemory r1, r2;
        REQUIRE(r1.open(name).isOk());
        REQUIRE(r2.open(name).isOk());

        CHECK(r1.data() != r2.data());                  // distinct mappings
        CHECK(static_cast<const uint8_t *>(r1.data())[0] == 0x11);
        CHECK(static_cast<const uint8_t *>(r2.data())[0] == 0x11);

        // Owner write is visible through both mappings.
        static_cast<uint8_t *>(owner.data())[0] = 0x22;
        CHECK(static_cast<const uint8_t *>(r1.data())[0] == 0x22);
        CHECK(static_cast<const uint8_t *>(r2.data())[0] == 0x22);
}

TEST_CASE("SharedMemory: AlreadyOpen when reusing an open instance") {
        if(!SharedMemory::isSupported()) return;
        SharedMemory shm;
        REQUIRE(shm.create(uniqueShmName("reuse"), 64).isOk());
        Error err = shm.create(uniqueShmName("reuse2"), 64);
        CHECK(err == Error::AlreadyOpen);
}
