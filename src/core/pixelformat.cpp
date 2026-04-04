/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pixelformat.h>
#include <promeki/atomic.h>
#include <promeki/map.h>
#include <promeki/util.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(PixelFormat)

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered types
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{PixelFormat::UserDefined};

PixelFormat::ID PixelFormat::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Stride / plane-size helpers shared by several formats
// ---------------------------------------------------------------------------

static size_t interleavedLineStride(const PixelFormat::Data *d, size_t /*planeIdx*/,
                                    size_t width, size_t linePad, size_t lineAlign) {
        size_t lineBytes = (width * d->bytesPerBlock) / d->pixelsPerBlock + linePad;
        return PROMEKI_ALIGN_UP(lineBytes, lineAlign);
}

static size_t interleavedPlaneSize(const PixelFormat::Data *d, size_t planeIdx,
                                   size_t width, size_t height,
                                   size_t linePad, size_t lineAlign) {
        return interleavedLineStride(d, planeIdx, width, linePad, lineAlign) * height;
}

// ---------------------------------------------------------------------------
// Factory functions for well-known pixel formats
// ---------------------------------------------------------------------------

static PixelFormat::Data makeInvalid() {
        PixelFormat::Data d;
        d.id   = PixelFormat::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid pixel format";
        return d;
}

static PixelFormat::Data makeInterleaved4x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_4x8;
        d.name           = "Interleaved_4x8";
        d.desc           = "4 components, 8 bits each, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 4;
        d.compCount      = 4;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.comps[2]       = { 0, 8, 2 };
        d.comps[3]       = { 0, 8, 3 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved3x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_3x8;
        d.name           = "Interleaved_3x8";
        d.desc           = "3 components, 8 bits each, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 3;
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.comps[2]       = { 0, 8, 2 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved3x10() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_3x10;
        d.name           = "Interleaved_3x10";
        d.desc           = "3 components, 10 bits each, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 4;  // 30 bits packed into 4 bytes
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 0, 10, 1 };
        d.comps[2]       = { 0, 10, 2 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved422_3x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_3x8;
        d.name           = "Interleaved_422_3x8";
        d.desc           = "3 components, 8 bits, 4:2:2 subsampled, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling422;
        d.pixelsPerBlock = 2;  // 2 pixels per macropixel (YUYV)
        d.bytesPerBlock  = 4;  // Y0 Cb Y1 Cr = 4 bytes for 2 pixels
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.comps[2]       = { 0, 8, 3 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved422_3x10() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_3x10;
        d.name           = "Interleaved_422_3x10";
        d.desc           = "3 components, 10 bits, 4:2:2 subsampled, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling422;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 8;  // 2 pixels * 10-bit * (Y+Cb/Cr) packed
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 0, 10, 2 };
        d.comps[2]       = { 0, 10, 6 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry
// ---------------------------------------------------------------------------

struct PixelFormatRegistry {
        Map<PixelFormat::ID, PixelFormat::Data> entries;
        Map<String, PixelFormat::ID> nameMap;

        PixelFormatRegistry() {
                add(makeInvalid());
                add(makeInterleaved4x8());
                add(makeInterleaved3x8());
                add(makeInterleaved3x10());
                add(makeInterleaved422_3x8());
                add(makeInterleaved422_3x10());
        }

        void add(PixelFormat::Data d) {
                PixelFormat::ID id = d.id;
                if(d.id != PixelFormat::Invalid) {
                        nameMap[d.name] = id;
                }
                entries[id] = std::move(d);
        }
};

static PixelFormatRegistry &registry() {
        static PixelFormatRegistry reg;
        return reg;
}

const PixelFormat::Data *PixelFormat::lookupData(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void PixelFormat::registerData(Data &&data) {
        auto &reg = registry();
        if(data.id != Invalid) {
                reg.nameMap[data.name] = data.id;
        }
        reg.entries[data.id] = std::move(data);
}

PixelFormat PixelFormat::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        return (it != reg.nameMap.end()) ? PixelFormat(it->second) : PixelFormat(Invalid);
}

PixelFormat::IDList PixelFormat::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
