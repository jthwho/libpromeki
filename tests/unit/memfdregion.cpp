/**
 * @file      tests/unit/memfdregion.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/config.h>
#include <promeki/memfdregion.h>

using namespace promeki;

#if PROMEKI_ENABLE_MEMFD

#include <sys/mman.h>
#include <unistd.h>

namespace {
        size_t pageSize() {
                long p = ::sysconf(_SC_PAGESIZE);
                return p > 0 ? static_cast<size_t>(p) : 4096;
        }
}

TEST_CASE("MemfdRegion: default construction is invalid") {
        MemfdRegion r;
        CHECK_FALSE(r.isValid());
        CHECK_FALSE(r.isSealed());
        CHECK(r.size() == 0);
        CHECK(r.fd() == -1);
        CHECK(r.producerView() == nullptr);
        Error err;
        CHECK(r.cloneView(&err) == nullptr);
        CHECK(err == Error::Invalid);
        CHECK(r.readOnlyView(&err) == nullptr);
        CHECK(err == Error::Invalid);
        CHECK(r.seal() == Error::Invalid);
}

TEST_CASE("MemfdRegion: size rounds up to page boundary") {
        const size_t pg = pageSize();
        MemfdRegion  small(1, "small");
        REQUIRE(small.isValid());
        CHECK(small.size() == pg);

        MemfdRegion exact(pg, "exact");
        REQUIRE(exact.isValid());
        CHECK(exact.size() == pg);

        MemfdRegion oversized(pg + 1, "oversized");
        REQUIRE(oversized.isValid());
        CHECK(oversized.size() == 2 * pg);
}

TEST_CASE("MemfdRegion: producerView returns same pointer until sealed") {
        MemfdRegion r(64 * 1024, "producerView-stable");
        REQUIRE(r.isValid());
        void *p1 = r.producerView();
        REQUIRE(p1 != nullptr);
        void *p2 = r.producerView();
        CHECK(p1 == p2);
        // Producer can write
        std::memset(p1, 0xAB, r.size());
}

TEST_CASE("MemfdRegion: pre-seal cloneView/readOnlyView return NotReady") {
        MemfdRegion r(64 * 1024, "pre-seal-not-ready");
        REQUIRE(r.isValid());
        Error err;
        CHECK(r.cloneView(&err) == nullptr);
        CHECK(err == Error::NotReady);
        CHECK(r.readOnlyView(&err) == nullptr);
        CHECK(err == Error::NotReady);
}

TEST_CASE("MemfdRegion: seal then cloneView/readOnlyView succeed") {
        MemfdRegion r(64 * 1024, "post-seal-ok");
        REQUIRE(r.isValid());
        void *prod = r.producerView();
        REQUIRE(prod != nullptr);
        std::memset(prod, 0x42, r.size());

        REQUIRE(r.seal() == Error::Ok);
        CHECK(r.isSealed());
        CHECK(r.producerView() == nullptr); // post-seal: producer view is gone

        Error err;
        void *clone = r.cloneView(&err);
        REQUIRE(clone != nullptr);
        CHECK(err == Error::Ok);
        CHECK(static_cast<unsigned char *>(clone)[0] == 0x42);
        CHECK(static_cast<unsigned char *>(clone)[r.size() - 1] == 0x42);
        CHECK(r.releaseView(clone) == Error::Ok);

        void *ro = r.readOnlyView(&err);
        REQUIRE(ro != nullptr);
        CHECK(err == Error::Ok);
        CHECK(static_cast<unsigned char *>(ro)[0] == 0x42);
        CHECK(r.releaseView(ro) == Error::Ok);

        // Seal is idempotent
        CHECK(r.seal() == Error::Ok);
        CHECK(r.seal() == Error::Ok);
}

TEST_CASE("MemfdRegion: seal(&out) returns the atomic first clone") {
        MemfdRegion r(64 * 1024, "atomic-seal-clone");
        REQUIRE(r.isValid());
        void *prod = r.producerView();
        REQUIRE(prod != nullptr);
        std::memset(prod, 0x7E, r.size());

        void *firstClone = nullptr;
        REQUIRE(r.seal(&firstClone) == Error::Ok);
        REQUIRE(firstClone != nullptr);
        CHECK(static_cast<unsigned char *>(firstClone)[0] == 0x7E);
        CHECK(static_cast<unsigned char *>(firstClone)[r.size() - 1] == 0x7E);

        // Second seal is a no-op and must not overwrite outFirstClone.
        void *sentinel = reinterpret_cast<void *>(uintptr_t(0xDEADBEEF));
        void *outAgain = sentinel;
        CHECK(r.seal(&outAgain) == Error::Ok);
        CHECK(outAgain == sentinel);

        CHECK(r.releaseView(firstClone) == Error::Ok);
}

TEST_CASE("MemfdRegion: clones diverge under CoW") {
        MemfdRegion r(64 * 1024, "clones-diverge");
        REQUIRE(r.isValid());
        void *prod = r.producerView();
        REQUIRE(prod != nullptr);
        std::memset(prod, 0x10, r.size());
        REQUIRE(r.seal() == Error::Ok);

        void *cloneA = r.cloneView();
        void *cloneB = r.cloneView();
        void *cloneC = r.cloneView();
        REQUIRE(cloneA != nullptr);
        REQUIRE(cloneB != nullptr);
        REQUIRE(cloneC != nullptr);
        CHECK(cloneA != cloneB);
        CHECK(cloneA != cloneC);

        // Mutate clone A only.
        static_cast<unsigned char *>(cloneA)[0]    = 0xFF;
        static_cast<unsigned char *>(cloneA)[1024] = 0xFF;

        // B and C still see the original byte.
        CHECK(static_cast<unsigned char *>(cloneA)[0] == 0xFF);
        CHECK(static_cast<unsigned char *>(cloneB)[0] == 0x10);
        CHECK(static_cast<unsigned char *>(cloneC)[0] == 0x10);

        CHECK(r.releaseView(cloneA) == Error::Ok);
        CHECK(r.releaseView(cloneB) == Error::Ok);
        CHECK(r.releaseView(cloneC) == Error::Ok);
}

TEST_CASE("MemfdRegion: readOnlyView reflects producer's pre-seal content") {
        MemfdRegion r(64 * 1024, "ro-view");
        REQUIRE(r.isValid());
        void *prod = r.producerView();
        REQUIRE(prod != nullptr);
        std::memset(prod, 0x33, r.size());
        REQUIRE(r.seal() == Error::Ok);

        void *ro1 = r.readOnlyView();
        void *ro2 = r.readOnlyView();
        REQUIRE(ro1 != nullptr);
        REQUIRE(ro2 != nullptr);
        CHECK(static_cast<unsigned char *>(ro1)[0] == 0x33);
        CHECK(static_cast<unsigned char *>(ro2)[r.size() - 1] == 0x33);
        CHECK(r.releaseView(ro1) == Error::Ok);
        CHECK(r.releaseView(ro2) == Error::Ok);
}

TEST_CASE("MemfdRegion: releaseView(nullptr) returns Invalid") {
        MemfdRegion r(64 * 1024, "release-null");
        REQUIRE(r.isValid());
        CHECK(r.releaseView(nullptr) == Error::Invalid);
}

TEST_CASE("MemfdRegion: move construction transfers ownership") {
        MemfdRegion src(64 * 1024, "move-src");
        REQUIRE(src.isValid());
        int    srcFd = src.fd();
        size_t srcSz = src.size();

        MemfdRegion dst(std::move(src));
        CHECK(dst.isValid());
        CHECK(dst.fd() == srcFd);
        CHECK(dst.size() == srcSz);
        CHECK_FALSE(src.isValid());
        CHECK(src.fd() == -1);
        CHECK(src.size() == 0);
}

TEST_CASE("MemfdRegion: move assignment closes destination first") {
        MemfdRegion a(64 * 1024, "move-assign-a");
        MemfdRegion b(128 * 1024, "move-assign-b");
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        int    bFd = b.fd();
        size_t bSz = b.size();

        a = std::move(b);
        CHECK(a.fd() == bFd);
        CHECK(a.size() == bSz);
        CHECK_FALSE(b.isValid());
}

TEST_CASE("MemfdRegion: page-CoW divergence with /proc/self/smaps measurement") {
        // The whole reason this class exists: each clone's *private*
        // resident set grows only by the pages it dirties.  mincore(2)
        // can't distinguish "shared file-cache page" from "CoW'd
        // anonymous page" for MAP_PRIVATE mappings — it just reports
        // residency of whatever a read access would return.  The
        // accurate kernel facility is /proc/self/smaps Private_Dirty,
        // which is what Phase B's residentBytes() will use under the
        // hood.  Here we verify the contract is the property smaps
        // measures, by sampling Private_Dirty on each clone.
        const size_t pg    = pageSize();
        const size_t bytes = 16 * 1024 * 1024; // 16 MiB
        MemfdRegion  r(bytes, "page-cow");
        REQUIRE(r.isValid());

        void *prod = r.producerView();
        REQUIRE(prod != nullptr);
        std::memset(prod, 0xCC, r.size());
        REQUIRE(r.seal() == Error::Ok);

        unsigned char *cloneA = static_cast<unsigned char *>(r.cloneView());
        unsigned char *cloneB = static_cast<unsigned char *>(r.cloneView());
        REQUIRE(cloneA != nullptr);
        REQUIRE(cloneB != nullptr);

        // Dirty exactly one page in cloneA.  CoW must produce exactly
        // one private anonymous page in this VMA.
        cloneA[0] = 0x55;

        // Read Private_Dirty for the VMA containing each clone.  Each
        // VMA's smaps block starts with "<start>-<end> ..." on its
        // first line; its subsequent lines list properties.  Walk the
        // file, find the block whose start address matches the clone,
        // and grab the Private_Dirty value (kB).
        auto privateDirtyKb = [](void *addr) -> long {
                std::FILE *fp = std::fopen("/proc/self/smaps", "r");
                if (fp == nullptr) return -1;
                char    line[512];
                bool    inBlock = false;
                long    kb      = -1;
                const auto wantStart = reinterpret_cast<uintptr_t>(addr);
                while (std::fgets(line, sizeof(line), fp) != nullptr) {
                        unsigned long start = 0, end = 0;
                        if (std::sscanf(line, "%lx-%lx", &start, &end) == 2) {
                                inBlock = (start == wantStart);
                                continue;
                        }
                        if (inBlock && std::strncmp(line, "Private_Dirty:", 14) == 0) {
                                long v = 0;
                                if (std::sscanf(line + 14, "%ld", &v) == 1) kb = v;
                                break;
                        }
                }
                std::fclose(fp);
                return kb;
        };

        long dirtyA = privateDirtyKb(cloneA);
        long dirtyB = privateDirtyKb(cloneB);

        // CloneA should hold exactly one dirty page; cloneB should hold zero.
        CHECK(dirtyA >= 0);
        CHECK(dirtyB >= 0);
        CHECK(static_cast<size_t>(dirtyA) * 1024 == pg);
        CHECK(dirtyB == 0);

        // Content-divergence sanity check (independent of smaps).
        CHECK(cloneA[0] == 0x55);
        CHECK(cloneB[0] == 0xCC);

        CHECK(r.releaseView(cloneA) == Error::Ok);
        CHECK(r.releaseView(cloneB) == Error::Ok);
}

TEST_CASE("MemfdRegion: dead state after seal failure") {
        // Force F_ADD_SEALS to fail by holding a sibling MAP_SHARED|PROT_WRITE
        // mapping on the fd through a second file descriptor.  The duplicate
        // fd has its own writable mapping that the region doesn't track and
        // can't unmap — F_SEAL_WRITE will refuse.
        MemfdRegion r(64 * 1024, "dead-state");
        REQUIRE(r.isValid());
        REQUIRE(r.producerView() != nullptr);

        int dupFd = ::dup(r.fd());
        REQUIRE(dupFd >= 0);
        void *sibling = ::mmap(nullptr, r.size(), PROT_READ | PROT_WRITE, MAP_SHARED, dupFd, 0);
        REQUIRE(sibling != MAP_FAILED);

        Error sealErr = r.seal();
        CHECK(sealErr != Error::Ok); // expected: Busy (EBUSY) or similar
        CHECK_FALSE(r.isValid());
        CHECK_FALSE(r.isSealed());
        CHECK(r.producerView() == nullptr);

        Error err;
        CHECK(r.cloneView(&err) == nullptr);
        CHECK(err == Error::Invalid);
        CHECK(r.readOnlyView(&err) == nullptr);
        CHECK(err == Error::Invalid);

        // A second seal call must not transition out of the dead state.
        CHECK(r.seal() == Error::Invalid);

        ::munmap(sibling, r.size());
        ::close(dupFd);
}

#else // !PROMEKI_ENABLE_MEMFD

TEST_CASE("MemfdRegion: stub on non-memfd builds reports unsupported") {
        CHECK_FALSE(MemfdRegion::isSupported());
        MemfdRegion r(4096, "stub");
        CHECK_FALSE(r.isValid());
        CHECK(r.producerView() == nullptr);
        Error err;
        CHECK(r.cloneView(&err) == nullptr);
        CHECK(err == Error::NotSupported);
        CHECK(r.seal() == Error::NotSupported);
}

#endif // PROMEKI_ENABLE_MEMFD
