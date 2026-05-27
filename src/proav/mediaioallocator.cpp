/**
 * @file      mediaioallocator.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaioallocator.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/pixelformat.h>
#include <promeki/bufferview.h>

PROMEKI_NAMESPACE_BEGIN

String MediaIOAllocator::name() const { return String("DefaultMediaIOAllocator"); }

Buffer MediaIOAllocator::allocateVideoPlane(const ImageDesc &desc, int planeIndex) const {
        return BufferAllocator::defaultAllocator()->allocateVideoPlane(desc, planeIndex);
}

Buffer MediaIOAllocator::allocateAudioChunk(const AudioDesc &desc, size_t samples) const {
        return BufferAllocator::defaultAllocator()->allocateAudioChunk(desc, samples);
}

Buffer MediaIOAllocator::allocateBytes(size_t bytes, size_t align) const {
        return BufferAllocator::defaultAllocator()->allocateBytes(bytes, align);
}

UncompressedVideoPayload::Ptr MediaIOAllocator::allocateVideoPayload(const ImageDesc &desc) const {
        const PixelFormat &pf = desc.pixelFormat();
        if (!pf.isValid() || !desc.size().isValid()) return UncompressedVideoPayload::Ptr();
        const int planeCount = pf.planeCount();
        if (planeCount <= 0) return UncompressedVideoPayload::Ptr();
        BufferView planes;
        for (int i = 0; i < planeCount; ++i) {
                Buffer plane = allocateVideoPlane(desc, i);
                if (!plane.isValid()) return UncompressedVideoPayload::Ptr();
                const size_t bytes = plane.allocSize();
                planes.pushToBack(plane, 0, bytes);
        }
        return UncompressedVideoPayload::Ptr::create(desc, planes);
}

PcmAudioPayload::Ptr MediaIOAllocator::allocateAudioPayload(const AudioDesc &desc, size_t samples) const {
        Buffer chunk = allocateAudioChunk(desc, samples);
        if (!chunk.isValid()) return PcmAudioPayload::Ptr();
        BufferView view(chunk, 0, chunk.allocSize());
        return PcmAudioPayload::Ptr::create(desc, samples, view);
}

MediaIOAllocator::Ptr MediaIOAllocator::defaultAllocator() {
        // Meyers' singleton — same pattern as BufferAllocator's.
        static const Ptr instance = Ptr::takeOwnership(new MediaIOAllocator());
        return instance;
}

PROMEKI_NAMESPACE_END
