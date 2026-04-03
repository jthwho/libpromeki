/**
 * @file      pixeldesc_proav.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/pixeldesc.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/core/metadata.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Paint engine factory declarations from implementation files
// ---------------------------------------------------------------------------

PaintEngine createPaintEngine_RGBA8(const PixelDesc::Data *d, const Image &img);
PaintEngine createPaintEngine_RGB8(const PixelDesc::Data *d, const Image &img);

// ---------------------------------------------------------------------------
// Register paint engine factories with PixelDesc entries.
// This runs at static init time in the proav library.
// ---------------------------------------------------------------------------

static struct PixelDescProavInit {
        PixelDescProavInit() {
                auto patch = [](PixelDesc::ID id, PaintEngine (*func)(const PixelDesc::Data *, const Image &)) {
                        PixelDesc pd(id);
                        if(!pd.isValid()) return;
                        PixelDesc::Data d = *pd.data();
                        d.createPaintEngineFunc = func;
                        PixelDesc::registerData(std::move(d));
                };
                patch(PixelDesc::RGBA8_sRGB_Full, createPaintEngine_RGBA8);
                patch(PixelDesc::RGB8_sRGB_Full, createPaintEngine_RGB8);
        }
} __pixelDescProavInit;

// ---------------------------------------------------------------------------
// Non-inline methods that need proav type definitions
// ---------------------------------------------------------------------------

size_t PixelDesc::lineStride(size_t planeIndex, const ImageDesc &desc) const {
        if(d->compressed) return 0;
        return d->pixelFormat.lineStride(planeIndex, desc.width(), desc.linePad(), desc.lineAlign());
}

size_t PixelDesc::planeSize(size_t planeIndex, const ImageDesc &desc) const {
        if(d->compressed) {
                if(!desc.metadata().contains(Metadata::CompressedSize)) return 0;
                return desc.metadata().get(Metadata::CompressedSize).get<size_t>();
        }
        return d->pixelFormat.planeSize(planeIndex, desc.width(), desc.height(),
                                        desc.linePad(), desc.lineAlign());
}

PaintEngine PixelDesc::createPaintEngine(const Image &img) const {
        if(d->createPaintEngineFunc == nullptr) return PaintEngine();
        return d->createPaintEngineFunc(d, img);
}

PROMEKI_NAMESPACE_END
