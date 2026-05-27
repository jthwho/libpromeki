/**
 * @file      tests/unit/memfdbufferimpl.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/config.h>
#include <promeki/memspace.h>

using namespace promeki;

// ============================================================================
// SystemCow MemSpace registration (always-on — even on the fallback build
// the ID resolves to the registered Ops)
// ============================================================================

TEST_CASE("MemSpace::SystemCow is registered") {
        MemSpace ms(MemSpace::SystemCow);
        CHECK(ms.id() == MemSpace::SystemCow);
        CHECK(ms.name() == "SystemCow");
        CHECK(ms.domain().id() == MemDomain::Host);
}

// ============================================================================
// Producer-phase behaviour (always succeeds — uses memfd on Linux,
// HostBufferImpl on the fallback build)
// ============================================================================

TEST_CASE("Buffer(SystemCow): producer phase write/read across handles") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(a.isValid());
        REQUIRE(a.data() != nullptr);
        std::memset(a.data(), 0x42, a.allocSize());

        Buffer b = a; // refcount bump, same backing
        CHECK(b.isValid());
        CHECK(b.data() == a.data()); // same impl, same producer view
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0x42);
        CHECK(static_cast<unsigned char *>(b.data())[a.allocSize() - 1] == 0x42);
}

TEST_CASE("Buffer(SystemCow): seal is idempotent and OK") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(a.isValid());
        std::memset(a.data(), 0x33, a.allocSize());
        CHECK(a.seal() == Error::Ok);
        CHECK(a.seal() == Error::Ok);
        CHECK(a.seal() == Error::Ok);
        // After seal the data is still readable through this handle.
        CHECK(a.isValid());
        CHECK(a.data() != nullptr);
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x33);
}

#if PROMEKI_ENABLE_MEMFD

// ============================================================================
// CoW-specific behaviour (Linux only — fallback build doesn't CoW)
// ============================================================================

TEST_CASE("Buffer(SystemCow): isCowBacked is true on memfd builds") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(a.isValid());
        CHECK(a.isCowBacked());
}

TEST_CASE("Buffer(SystemCow): explicit seal then ensureExclusive diverges") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(a.isValid());
        std::memset(a.data(), 0x11, a.allocSize());

        Buffer b = a;
        REQUIRE(a.seal() == Error::Ok);

        // Detach b from the shared impl.  ensureExclusive triggers
        // _promeki_clone -> a fresh MAP_PRIVATE clone.
        b.ensureExclusive();
        CHECK(a.impl().ptr() != b.impl().ptr());

        // Both still readable
        REQUIRE(a.data() != nullptr);
        REQUIRE(b.data() != nullptr);
        // Each starts as a copy of the sealed source content
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x11);
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0x11);

        // Mutate b only
        static_cast<unsigned char *>(b.data())[0] = 0xEE;
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0xEE);
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x11);
}

TEST_CASE("Buffer(SystemCow): implicit seal on first clone") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(a.isValid());
        std::memset(a.data(), 0x77, a.allocSize());

        Buffer b = a;
        // No explicit seal — ensureExclusive() should auto-seal as a
        // safety net before producing the clone.
        b.ensureExclusive();
        CHECK(a.impl().ptr() != b.impl().ptr());
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x77);
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0x77);

        // After implicit seal, explicit seal on either handle is a no-op success.
        CHECK(a.seal() == Error::Ok);
        CHECK(b.seal() == Error::Ok);
}

TEST_CASE("Buffer(SystemCow): multiple clones diverge from each other and source") {
        Buffer src(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(src.isValid());
        std::memset(src.data(), 0x55, src.allocSize());
        REQUIRE(src.seal() == Error::Ok);

        Buffer a = src;
        Buffer b = src;
        Buffer c = src;
        a.ensureExclusive();
        b.ensureExclusive();
        c.ensureExclusive();

        CHECK(a.impl().ptr() != src.impl().ptr());
        CHECK(b.impl().ptr() != src.impl().ptr());
        CHECK(c.impl().ptr() != src.impl().ptr());
        CHECK(a.impl().ptr() != b.impl().ptr());
        CHECK(a.impl().ptr() != c.impl().ptr());
        CHECK(b.impl().ptr() != c.impl().ptr());

        static_cast<unsigned char *>(a.data())[0] = 0xAA;
        static_cast<unsigned char *>(b.data())[0] = 0xBB;
        static_cast<unsigned char *>(c.data())[0] = 0xCC;
        CHECK(static_cast<unsigned char *>(src.data())[0] == 0x55);
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0xAA);
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0xBB);
        CHECK(static_cast<unsigned char *>(c.data())[0] == 0xCC);
}

TEST_CASE("Buffer(SystemCow): residentBytes drops dramatically after CoW clone") {
        // The whole property the SystemCow MemSpace exists for: a
        // small write into a fresh clone yields a small resident set
        // for that clone, even though the source is fully populated.
        const size_t bytes = 16 * 1024 * 1024;
        Buffer       src(bytes, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(src.isValid());
        std::memset(src.data(), 0x42, bytes);
        REQUIRE(src.seal() == Error::Ok);

        Buffer clone = src;
        clone.ensureExclusive();
        REQUIRE(clone.impl().ptr() != src.impl().ptr());

        // Touch one page in the clone so private_dirty has at least one page.
        static_cast<unsigned char *>(clone.data())[0] = 0xFF;

        size_t cloneRes = clone.residentBytes();
        // Clone's private-dirty should be a tiny fraction of the
        // allocation (one page, plus any kernel-internal slack).
        CHECK(cloneRes > 0);
        CHECK(cloneRes < bytes / 4);
}

TEST_CASE("Buffer(SystemCow): residentBytes pre-seal == allocSize") {
        // Pre-seal we report allocSize because the producer view is
        // MAP_SHARED — Private_Dirty doesn't apply, and allocSize is
        // the conservative + correct answer ("we wrote the whole thing").
        const size_t bytes = 64 * 1024;
        Buffer       a(bytes, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(a.isValid());
        std::memset(a.data(), 0x10, bytes);
        CHECK(a.residentBytes() == a.allocSize());
}

TEST_CASE("BufferView(SystemCow): seal walks unique buffers in a multi-plane payload") {
        // Multi-plane: two distinct SystemCow buffers, each appearing
        // as a slice in the view.  BufferView::seal() should seal
        // each unique impl exactly once.
        Buffer plane0(32 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        Buffer plane1(32 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(plane0.isValid());
        REQUIRE(plane1.isValid());

        plane0.setSize(plane0.allocSize());
        plane1.setSize(plane1.allocSize());
        std::memset(plane0.data(), 0x01, plane0.allocSize());
        std::memset(plane1.data(), 0x02, plane1.allocSize());

        BufferView view = {
                BufferView(plane0, 0, plane0.allocSize()),
                BufferView(plane1, 0, plane1.allocSize()),
        };
        CHECK(view.seal() == Error::Ok);

        // Ensure further seal is still Ok (idempotent across the view).
        CHECK(view.seal() == Error::Ok);

        // After seal, ensureExclusive on a sibling handle to one
        // plane diverges only that plane.
        Buffer plane0Sibling = plane0;
        plane0Sibling.ensureExclusive();
        CHECK(plane0Sibling.impl().ptr() != plane0.impl().ptr());

        static_cast<unsigned char *>(plane0Sibling.data())[0] = 0xFF;
        CHECK(static_cast<unsigned char *>(plane0.data())[0] == 0x01);
        CHECK(static_cast<unsigned char *>(plane0Sibling.data())[0] == 0xFF);
}

TEST_CASE("Buffer(SystemCow): detach-of-dirty-clone copies only modified pages") {
        // The headline SystemCow win: per-frame burn-in writes a small
        // band of pages, and a subsequent detach (because something
        // else is still holding the first-frame clone — e.g.
        // FastFont's PaintEngine reference in TPG) should pay for
        // *only those modified pages*, not the entire frame.  A
        // naive memcpy of allocSize would cost the same as the
        // pre-SystemCow deep copy and defeat the optimisation.
        const size_t bytes = 16 * 1024 * 1024; // 16 MiB
        Buffer       source(bytes, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(source.isValid());
        std::memset(source.data(), 0x42, bytes);
        REQUIRE(source.seal() == Error::Ok);

        // 1st detach: cached → per-frame.  Source is _dirty=false so
        // the kernel CoW-from-file path runs (no memcpy).
        Buffer perFrame = source;
        perFrame.ensureExclusive();
        CHECK(perFrame.impl().ptr() != source.impl().ptr());

        // Mutate a single page worth of bytes — TPG burn-in band.
        long pg = ::sysconf(_SC_PAGESIZE);
        REQUIRE(pg > 0);
        const size_t pageSize = static_cast<size_t>(pg);
        std::memset(perFrame.data(), 0xAA, pageSize);

        // 2nd detach: per-frame → data-encoder.  perFrame is
        // _dirty=true so the implementation must preserve the burn
        // pixels — but ideally only by copying the burn band's
        // pages, not the whole frame.
        Buffer dataenc = perFrame;
        dataenc.ensureExclusive();
        REQUIRE(dataenc.data() != nullptr);
        // Sanity: burn pixels survive through the detach chain.
        CHECK(static_cast<unsigned char *>(dataenc.data())[0] == 0xAA);
        CHECK(static_cast<unsigned char *>(dataenc.data())[pageSize - 1] == 0xAA);
        CHECK(static_cast<unsigned char *>(dataenc.data())[pageSize] == 0x42);

        // Resident bytes on dataenc tells us how many pages were
        // privately materialised.  Pagemap-based per-page copy
        // materialises only the page we wrote through perFrame; the
        // remaining pages stay kernel-CoW shared with the sealed
        // file via the new MAP_PRIVATE clone.  A full-frame memcpy
        // implementation would materialise every page (defeating the
        // SystemCow win and matching the pre-SystemCow cost).
        const size_t residentDataenc = dataenc.residentBytes();
        // We allow up to 32 pages of slack — even the most
        // conservative pagemap path should not exceed that, since
        // we only wrote one page.
        CHECK(residentDataenc <= 32 * pageSize);
        if (residentDataenc > 32 * pageSize) {
                MESSAGE("dataenc.residentBytes() = ", residentDataenc,
                        " bytes — expected ~", pageSize, " (one page).  "
                        "Pagemap-based per-page copy not in effect; "
                        "the implementation materialised more pages "
                        "than necessary.");
        }
}

TEST_CASE("Buffer(SystemCow): TPG-shape regression — full frame minus burn band") {
        // Models the TPG burn → data-encoder per-frame pattern at a
        // ~1080p-RGBA scale.  The cached background is sealed; the
        // per-frame detach is free; burn writes a small band; the
        // second detach (data-encoder, triggered by FastFont's
        // PaintEngine reference) copies only the burn band's pages.
        // The whole-frame memcpy that the original implementation
        // would have done at this scale would land 8.3 MB residency
        // on dataenc — what we want to verify is that we're paying
        // far less than that.
        long pgRaw = ::sysconf(_SC_PAGESIZE);
        REQUIRE(pgRaw > 0);
        const size_t pageSize = static_cast<size_t>(pgRaw);
        const size_t bytes = 8 * 1024 * 1024; // ~1080p RGBA
        Buffer       cached(bytes, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(cached.isValid());
        std::memset(cached.data(), 0x42, bytes);
        REQUIRE(cached.seal() == Error::Ok);

        // Per-frame detach → free.
        Buffer perFrame = cached;
        perFrame.ensureExclusive();
        REQUIRE(perFrame.impl().ptr() != cached.impl().ptr());

        // Simulate burn: write to a contiguous "band" that's a small
        // fraction of the frame — say 64 KiB of dirty pages (the
        // size of a typical burn-text rectangle).
        const size_t burnBytes = 64 * 1024;
        std::memset(perFrame.data(), 0xEE, burnBytes);

        // Second detach (data-encoder due to a sibling refcount).
        Buffer dataenc = perFrame;
        dataenc.ensureExclusive();
        REQUIRE(dataenc.impl().ptr() != perFrame.impl().ptr());

        // Burn pixels survived the second detach.
        CHECK(static_cast<unsigned char *>(dataenc.data())[0] == 0xEE);
        CHECK(static_cast<unsigned char *>(dataenc.data())[burnBytes - 1] == 0xEE);
        CHECK(static_cast<unsigned char *>(dataenc.data())[burnBytes] == 0x42);

        // Resident bytes on dataenc should be roughly the burn band's
        // page-count (rounded up to whole pages), nowhere near the
        // full frame.
        const size_t residentDataenc = dataenc.residentBytes();
        const size_t expectedPages   = (burnBytes + pageSize - 1) / pageSize;
        const size_t expectedBytes   = expectedPages * pageSize;
        // Allow 4x slack for kernel readahead / measurement noise.
        CHECK(residentDataenc < expectedBytes * 4);
        // And, critically, far below the full-frame cost we'd have
        // paid pre-SystemCow (or with a naive memcpy on detach).
        CHECK(residentDataenc < bytes / 16);
}

TEST_CASE("Buffer(SystemCow): second detach preserves first detach's writes") {
        // Regression: HostBufferImpl::_promeki_clone is a deep copy
        // that preserves modifications.  MemfdBufferImpl::_promeki_clone
        // must match that semantics — the kernel's
        // MAP_PRIVATE-from-sealed-file CoW would otherwise drop any
        // writes the caller made through this view.  TPG hits this
        // when the burn pass detaches once and the data-encoder pass
        // re-detaches a moment later (FastFont's PaintEngine still
        // holds a reference to the burn-modified clone): without
        // this fix the data encoder's detach reads back to the
        // sealed source's content and the burn pixels disappear.
        Buffer source(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(source.isValid());
        std::memset(source.data(), 0x11, source.allocSize());
        REQUIRE(source.seal() == Error::Ok);

        // First detach (e.g. cached → per-frame).  Sibling clone is
        // born _dirty=true, content matches the sealed source.
        Buffer firstClone = source;
        firstClone.ensureExclusive();
        CHECK(firstClone.impl().ptr() != source.impl().ptr());
        CHECK(static_cast<unsigned char *>(firstClone.data())[0] == 0x11);

        // Mutate firstClone through PaintEngine-style writes — write
        // a known byte in the first page.
        static_cast<unsigned char *>(firstClone.data())[0] = 0xEE;

        // Second detach (e.g. data-encoder pass re-detaches due to a
        // sibling reference).  The new clone MUST see the 0xEE
        // modification, not the sealed source's 0x11.
        Buffer secondClone = firstClone;
        secondClone.ensureExclusive();
        CHECK(secondClone.impl().ptr() != firstClone.impl().ptr());
        CHECK(static_cast<unsigned char *>(secondClone.data())[0] == 0xEE);
        // And the byte we didn't touch still matches.
        CHECK(static_cast<unsigned char *>(secondClone.data())[100] == 0x11);
}

#else // !PROMEKI_ENABLE_MEMFD

TEST_CASE("Buffer(SystemCow): fallback build isCowBacked is false") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::SystemCow);
        REQUIRE(a.isValid());
        // On non-memfd builds the SystemCow factory returns a plain
        // HostBufferImpl, so seal is a no-op success and isCowBacked
        // is false.  Behaviour is correctness-equivalent — the user
        // just doesn't get the CoW savings.
        CHECK_FALSE(a.isCowBacked());
        CHECK(a.seal() == Error::Ok);

        // ensureExclusive still works (deep-copy clone path).
        Buffer b = a;
        b.ensureExclusive();
        CHECK(a.impl().ptr() != b.impl().ptr());
}

#endif // PROMEKI_ENABLE_MEMFD
