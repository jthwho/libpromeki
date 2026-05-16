/**
 * @file      bufferallocator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/bufferallocator.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/pixelformat.h>
#endif

PROMEKI_NAMESPACE_BEGIN

String DefaultBufferAllocator::name() const { return String("DefaultBufferAllocator"); }

#if PROMEKI_ENABLE_PROAV
Buffer DefaultBufferAllocator::allocateVideoPlane(const ImageDesc &desc, int planeIndex) const {
        const PixelFormat &pf = desc.pixelFormat();
        if (!pf.isValid() || !desc.size().isValid()) return Buffer();
        if (planeIndex < 0 || planeIndex >= static_cast<int>(pf.planeCount())) return Buffer();
        const size_t bytes = pf.planeSize(static_cast<size_t>(planeIndex), desc);
        if (bytes == 0) return Buffer();
        Buffer buf(bytes);
        if (buf.isValid()) buf.setSize(bytes);
        return buf;
}

Buffer DefaultBufferAllocator::allocateAudioChunk(const AudioDesc &desc, size_t samples) const {
        const size_t bytes = desc.bufferSize(samples);
        if (bytes == 0) return Buffer();
        Buffer buf(bytes);
        if (buf.isValid()) buf.setSize(bytes);
        return buf;
}
#endif // PROMEKI_ENABLE_PROAV

Buffer DefaultBufferAllocator::allocateBytes(size_t bytes, size_t align) const {
        if (bytes == 0) return Buffer();
        return align == 0 ? Buffer(bytes) : Buffer(bytes, align);
}

BufferAllocator::Ptr BufferAllocator::defaultAllocator() {
        // Meyers' singleton — C++11+ guarantees thread-safe init of
        // the static local; no manual locking, no leak, no order-of-
        // init pitfall against other singletons.
        static const Ptr instance = Ptr::takeOwnership(new DefaultBufferAllocator());
        return instance;
}

PROMEKI_NAMESPACE_END
