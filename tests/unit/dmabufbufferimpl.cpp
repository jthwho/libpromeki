/**
 * @file      tests/unit/dmabufbufferimpl.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <utility>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/buffercommand.h>
#include <promeki/bufferimpl.h>
#include <promeki/buffermapflags.h>
#include <promeki/bufferrequest.h>
#include <promeki/config.h>
#include <promeki/error.h>
#include <promeki/memdomain.h>
#include <promeki/memspace.h>

#if PROMEKI_ENABLE_DMABUF
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#if __has_include(<linux/dma-heap.h>)
#include <linux/dma-heap.h>
#define PROMEKI_TEST_HAVE_DMA_HEAP 1
#endif
#endif

using namespace promeki;

// ============================================================================
// Registry checks — run on every build, even when the dma-buf backend is
// compiled out: the MemDomain / MemSpace IDs are registered as metadata
// unconditionally so call sites resolve them.
// ============================================================================

TEST_CASE("MemDomain::Dmabuf is registered") {
        MemDomain d(MemDomain::Dmabuf);
        CHECK(d.id() == MemDomain::Dmabuf);
        CHECK(d.name() == "Dmabuf");

        bool found = false;
        for (MemDomain::ID id : MemDomain::registeredIDs()) {
                if (id == MemDomain::Dmabuf) found = true;
        }
        CHECK(found);
}

TEST_CASE("MemSpace::Dmabuf is registered and lives in the Dmabuf domain") {
        MemSpace ms(MemSpace::Dmabuf);
        CHECK(ms.id() == MemSpace::Dmabuf);
        CHECK(ms.name() == "Dmabuf");
        CHECK(ms.domain().id() == MemDomain::Dmabuf);
        // The native fd is not directly CPU-addressable.
        CHECK_FALSE(ms.isHostAccessible(MemAllocation{}));
}

TEST_CASE("Buffer::dmabufFd() is -1 for a non-dmabuf Buffer") {
        Buffer host(4096);
        CHECK(host.dmabufFd() == -1);
}

// ============================================================================
// BufferImpl release callback — the generic final-reference-teardown hook
// the V4L2 dma-buf capture pool relies on to re-queue a kernel buffer once
// the exported dma-buf's last reference is dropped.  Tested here with a
// minimal stub impl so it runs on every build, dma-buf hardware or not.
// ============================================================================

namespace {
// Minimal non-mappable BufferImpl used only to exercise the base-class
// release callback.  Lives in MemSpace::Default; nothing maps.
class CallbackStubImpl : public BufferImpl {
        public:
                PROMEKI_SHARED_DERIVED(CallbackStubImpl)

                explicit CallbackStubImpl(size_t bytes) : _bytes(bytes) {}

                MemSpace      memSpace() const override { return MemSpace(MemSpace::Default); }
                size_t        allocSize() const override { return _bytes; }
                size_t        align() const override { return 0; }
                void         *mappedHostData() const override { return nullptr; }
                BufferRequest mapAcquire(MemDomain, MapFlags) override {
                        return BufferRequest::resolved(Error::NotSupported);
                }
                BufferRequest mapRelease(MemDomain) override { return BufferRequest::resolved(Error::NotSupported); }
                Error         fill(char, size_t, size_t) override { return Error::NotSupported; }
                Error         copyFromHost(const void *, size_t, size_t) override { return Error::NotSupported; }

        private:
                size_t _bytes = 0;
};
} // namespace

TEST_CASE("BufferImpl release callback fires once at final-reference teardown") {
        int fired = 0;
        {
                auto *impl = new CallbackStubImpl(4096);
                impl->setReleaseCallback([&fired]() { fired++; });
                Buffer a = Buffer::fromImpl(impl);
                REQUIRE(a.isValid());
                CHECK(fired == 0);
                {
                        Buffer b = a; // refcount 2
                        Buffer c = b; // refcount 3
                        CHECK(fired == 0);
                }
                // c and b destroyed → refcount back to 1, impl still alive.
                CHECK(fired == 0);
        }
        // Last handle (a) destroyed → impl deleted → callback fires once.
        CHECK(fired == 1);
}

TEST_CASE("BufferImpl release callback survives a moved handle and fires once") {
        int fired = 0;
        {
                auto *impl = new CallbackStubImpl(8);
                impl->setReleaseCallback([&fired]() { fired++; });
                Buffer a = Buffer::fromImpl(impl);
                Buffer b = std::move(a); // moved-from a no longer references the impl
                CHECK(fired == 0);
        }
        CHECK(fired == 1);
}

TEST_CASE("BufferImpl with no release callback tears down cleanly") {
        int    sentinel = 7;
        Buffer a         = Buffer::fromImpl(new CallbackStubImpl(16));
        CHECK(a.isValid());
        // No callback armed; destruction at end of scope must not fire
        // anything or crash.
        CHECK(sentinel == 7);
}

#if !PROMEKI_ENABLE_DMABUF

TEST_CASE("Buffer::wrapDmabuf is inert without PROMEKI_ENABLE_DMABUF") {
        Buffer b = Buffer::wrapDmabuf(3 /*any fd*/, 4096);
        CHECK_FALSE(b.isValid());
        CHECK(b.dmabufFd() == -1);
}

#else // PROMEKI_ENABLE_DMABUF

namespace {

// Allocates a real, CPU-mappable dma-buf from the kernel system heap.
// Returns a valid fd, or -1 when the dma-heap device is unavailable
// (older kernel, container without /dev/dma_heap, heaps not enabled) —
// in which case the fd-backed subcases skip rather than fail.
int allocSystemDmabuf(size_t bytes) {
#ifdef PROMEKI_TEST_HAVE_DMA_HEAP
        int heap = ::open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
        if (heap < 0) return -1;
        struct dma_heap_allocation_data data;
        std::memset(&data, 0, sizeof(data));
        data.len = bytes;
        data.fd_flags = O_RDWR | O_CLOEXEC;
        int r = ::ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &data);
        ::close(heap);
        if (r < 0) return -1;
        return static_cast<int>(data.fd);
#else
        (void)bytes;
        return -1;
#endif
}

// Counts entries under /proc/self/fd.  Each call opens one transient fd
// for the directory itself, so two calls bracketing a scope are directly
// comparable (both include their own dir fd).
int openFdCount() {
        DIR *d = ::opendir("/proc/self/fd");
        if (d == nullptr) return -1;
        int            n = 0;
        struct dirent *e;
        while ((e = ::readdir(d)) != nullptr) {
                if (e->d_name[0] != '.') n++;
        }
        ::closedir(d);
        return n;
}

} // namespace

TEST_CASE("Buffer::wrapDmabuf rejects bad arguments") {
        CHECK_FALSE(Buffer::wrapDmabuf(-1, 4096).isValid());
        CHECK_FALSE(Buffer::wrapDmabuf(3, 0).isValid());
}

TEST_CASE("Buffer::wrapDmabuf imports an fd and exposes Dmabuf identity") {
        constexpr size_t kSize = 64 * 1024;
        int              fd = allocSystemDmabuf(kSize);
        if (fd < 0) {
                MESSAGE("skipping: /dev/dma_heap/system unavailable");
                return;
        }

        {
                Buffer b = Buffer::wrapDmabuf(fd, kSize);
                REQUIRE(b.isValid());
                CHECK(b.allocSize() == kSize);
                CHECK(b.memSpace().id() == MemSpace::Dmabuf);
                CHECK(b.memSpace().domain().id() == MemDomain::Dmabuf);
                // wrapDmabuf dups: the backend fd is an independent
                // reference, distinct from the caller's fd.
                CHECK(b.dmabufFd() >= 0);
                CHECK(b.dmabufFd() != fd);

                // Not host-addressable until explicitly mapped.
                CHECK(b.data() == nullptr);
                CHECK_FALSE(b.isHostAccessible());
        }

        // The caller's fd is untouched by the Buffer's lifetime.
        CHECK(::fcntl(fd, F_GETFD) != -1);
        ::close(fd);
}

TEST_CASE("Buffer(dmabuf): Dmabuf-domain map surfaces the backend fd") {
        constexpr size_t kSize = 32 * 1024;
        int              fd = allocSystemDmabuf(kSize);
        if (fd < 0) {
                MESSAGE("skipping: /dev/dma_heap/system unavailable");
                return;
        }

        Buffer b = Buffer::wrapDmabuf(fd, kSize);
        REQUIRE(b.isValid());

        BufferRequest req = b.mapAcquire(MemDomain::Dmabuf, MapFlags::Read);
        CHECK(req.isReady());
        CHECK(req.wait() == Error::Ok);
        const auto *cmd = req.commandAs<BufferDmabufMapCommand>();
        REQUIRE(cmd != nullptr);
        CHECK(cmd->dmabufFd == b.dmabufFd());
        CHECK(cmd->dmabufFd >= 0);

        CHECK(b.mapRelease(MemDomain::Dmabuf).wait() == Error::Ok);
        ::close(fd);
}

TEST_CASE("Buffer(dmabuf): host mapping round-trips through the shared buffer") {
        constexpr size_t kSize = 64 * 1024;
        int              fd = allocSystemDmabuf(kSize);
        if (fd < 0) {
                MESSAGE("skipping: /dev/dma_heap/system unavailable");
                return;
        }

        Buffer b = Buffer::wrapDmabuf(fd, kSize);
        REQUIRE(b.isValid());

        // Acquire a writable host view, stamp a pattern, release it.
        {
                BufferRequest req = b.mapAcquire(MemDomain::Host, MapFlags::ReadWrite);
                REQUIRE(req.isReady());
                REQUIRE(req.wait() == Error::Ok);
                void *p = b.data();
                REQUIRE(p != nullptr);
                CHECK(b.isHostAccessible());
                std::memset(p, 0xAB, kSize);
                CHECK(b.mapRelease(MemDomain::Host).wait() == Error::Ok);
        }

        // The view is torn down once the last Host ref drops.
        CHECK(b.data() == nullptr);
        CHECK_FALSE(b.isHostAccessible());

        // Re-map read-only and confirm the bytes persisted in the dma-buf.
        {
                BufferRequest req = b.mapAcquire(MemDomain::Host, MapFlags::Read);
                REQUIRE(req.wait() == Error::Ok);
                const uint8_t *p = static_cast<const uint8_t *>(b.data());
                REQUIRE(p != nullptr);
                bool allSet = true;
                for (size_t i = 0; i < kSize; i++) {
                        if (p[i] != 0xAB) { allSet = false; break; }
                }
                CHECK(allSet);
                CHECK(b.mapRelease(MemDomain::Host).wait() == Error::Ok);
        }
        ::close(fd);
}

TEST_CASE("Buffer(dmabuf): nested host maps refcount; releasing unknown domain fails") {
        constexpr size_t kSize = 16 * 1024;
        int              fd = allocSystemDmabuf(kSize);
        if (fd < 0) {
                MESSAGE("skipping: /dev/dma_heap/system unavailable");
                return;
        }

        Buffer b = Buffer::wrapDmabuf(fd, kSize);
        REQUIRE(b.isValid());

        REQUIRE(b.mapAcquire(MemDomain::Host, MapFlags::Read).wait() == Error::Ok);
        void *first = b.data();
        REQUIRE(first != nullptr);
        // Second acquire is a refcount bump — same mapping.
        REQUIRE(b.mapAcquire(MemDomain::Host, MapFlags::Read).wait() == Error::Ok);
        CHECK(b.data() == first);

        // One release: still mapped.
        CHECK(b.mapRelease(MemDomain::Host).wait() == Error::Ok);
        CHECK(b.data() == first);
        // Final release: torn down.
        CHECK(b.mapRelease(MemDomain::Host).wait() == Error::Ok);
        CHECK(b.data() == nullptr);

        // Releasing a domain that was never acquired is an error.
        CHECK(b.mapRelease(MemDomain::Host).wait() == Error::Invalid);
        ::close(fd);
}

TEST_CASE("Buffer(dmabuf): wrapDmabuf dups and owns — caller's fd survives, no leak") {
        int fd = allocSystemDmabuf(8 * 1024);
        if (fd < 0) {
                MESSAGE("skipping: /dev/dma_heap/system unavailable");
                return;
        }

        int before = openFdCount();
        {
                Buffer b = Buffer::wrapDmabuf(fd, 8 * 1024);
                REQUIRE(b.isValid());
                // The dup is an extra open fd while the Buffer is alive.
                CHECK(openFdCount() == before + 1);
                // The caller's fd is independent and still open.
                CHECK(::fcntl(fd, F_GETFD) != -1);
        }
        // The Buffer closed its dup (no leak); the caller's fd is untouched.
        CHECK(openFdCount() == before);
        CHECK(::fcntl(fd, F_GETFD) != -1);
        ::close(fd);
}

TEST_CASE("Buffer(dmabuf): a dma-buf cannot be cloned") {
        int fd = allocSystemDmabuf(8 * 1024);
        if (fd < 0) {
                MESSAGE("skipping: /dev/dma_heap/system unavailable");
                return;
        }

        Buffer a = Buffer::wrapDmabuf(fd, 8 * 1024);
        REQUIRE(a.isValid());
        Buffer b = a; // handle copy — shares the backend
        CHECK(b.dmabufFd() == a.dmabufFd());
        CHECK(a.isShared());
        // ensureExclusiveError surfaces the non-clonable backend.
        CHECK(b.ensureExclusiveError() == Error::NotSupported);
        ::close(fd);
}

#endif // PROMEKI_ENABLE_DMABUF
