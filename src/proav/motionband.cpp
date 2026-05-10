/**
 * @file      motionband.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/motionband.h>
#include <promeki/paintengine.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/mediaconfig.h>
#include <promeki/rect.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

MotionBand::MotionBand()
        : _backgroundColor(Color::srgb(0.10f, 0.10f, 0.10f)),
          _markerColor(Color::srgb(0.10f, 0.85f, 0.20f)),
          _borderColor(Color::Black) {}

MotionBand::~MotionBand() = default;

void MotionBand::setEnabled(bool val) {
        if (_enabled == val) return;
        _enabled = val;
        invalidateCache();
}

void MotionBand::setHeight(int lines) {
        const int normalized = (lines > 0) ? lines : DefaultHeight;
        if (_height == normalized) return;
        _height = normalized;
        invalidateCache();
}

void MotionBand::setSequenceLength(int frames) {
        const int normalized = (frames > 0) ? frames : 0;
        if (_sequenceLength == normalized) return;
        _sequenceLength = normalized;
        invalidateCache();
}

void MotionBand::setOffset(int frames) { _offset = frames; }

void MotionBand::setPosition(Position p) { _position = p; }

void MotionBand::setBorderWidth(int pixels) {
        const int normalized = (pixels >= 1) ? pixels : 1;
        if (_borderWidth == normalized) return;
        _borderWidth = normalized;
        invalidateCache();
}

void MotionBand::setBackgroundColor(const Color &c) {
        if (_backgroundColor == c) return;
        _backgroundColor = c;
        invalidateCache();
}

void MotionBand::setMarkerColor(const Color &c) {
        if (_markerColor == c) return;
        _markerColor = c;
        invalidateCache();
}

void MotionBand::setBorderColor(const Color &c) {
        if (_borderColor == c) return;
        _borderColor = c;
        invalidateCache();
}

void MotionBand::setAllocator(MediaIOAllocator::Ptr allocator) {
        if (_allocator == allocator) return;
        _allocator = allocator;
        invalidateCache();
}

void MotionBand::invalidateCache() const {
        _cache.clear();
        _cacheImgWidth = 0;
        _cacheImgHeight = 0;
        _cachePixelFormatId = 0;
        _cacheEffectiveHeight = 0;
}

int MotionBand::effectiveBandHeight(const ImageDesc &target) const {
        const int requested = (_height > 0) ? _height : DefaultHeight;
        // Round up to the deepest vertical chroma subsampling factor of
        // the target so per-plane row counts come out even.
        size_t                vMax = 1;
        const PixelMemLayout &ml = target.pixelFormat().memLayout();
        for (size_t p = 0; p < ml.planeCount(); p++) {
                const size_t v = ml.planeDesc(p).vSubsampling;
                if (v > vMax) vMax = v;
        }
        const int rounded = static_cast<int>(((static_cast<size_t>(requested) + vMax - 1) / vMax) * vMax);
        return rounded;
}

ImageDesc MotionBand::bandDesc(const ImageDesc &target, int bandHeight) const {
        ImageDesc d(target.size().width(), bandHeight, target.pixelFormat());
        // Carry the colour-management metadata so CSC paths
        // (rgbScratchDesc -> target) pick the right transfer curve and
        // matrix; without this they fall back to defaults that may not
        // match the target frame.
        d.metadata() = target.metadata();
        return d;
}

ImageDesc MotionBand::rgbScratchDesc(const ImageDesc &target, int bandHeight) const {
        ImageDesc d(target.size().width(), bandHeight, PixelFormat(PixelFormat::RGBA8_sRGB));
        d.metadata() = target.metadata();
        return d;
}

int MotionBand::bandTopY(const ImageDesc &target, int bandHeight) const {
        if (_position == Position::Top) return 0;
        const int h = static_cast<int>(target.size().height());
        const int top = h - bandHeight;
        return (top > 0) ? top : 0;
}

UncompressedVideoPayload::Ptr MotionBand::cachedFrame(int frameInCycle, const ImageDesc &target) const {
        const int    bandH = effectiveBandHeight(target);
        const size_t imgW = target.size().width();
        const size_t imgH = target.size().height();
        const int    pfId = static_cast<int>(target.pixelFormat().id());

        if (_cacheImgWidth != imgW || _cacheImgHeight != imgH || _cachePixelFormatId != pfId ||
            _cacheEffectiveHeight != bandH) {
                invalidateCache();
                _cacheImgWidth = imgW;
                _cacheImgHeight = imgH;
                _cachePixelFormatId = pfId;
                _cacheEffectiveHeight = bandH;
        }
        if (static_cast<int>(_cache.size()) != _sequenceLength) {
                _cache.clear();
                _cache.resize(static_cast<size_t>(_sequenceLength));
        }
        if (frameInCycle < 0 || frameInCycle >= _sequenceLength) {
                return UncompressedVideoPayload::Ptr();
        }
        UncompressedVideoPayload::Ptr &slot = _cache[static_cast<size_t>(frameInCycle)];
        if (!slot.isValid()) {
                ImageDesc bd = bandDesc(target, bandH);
                slot = _allocator.isValid() ? _allocator->allocateVideoPayload(bd)
                                            : UncompressedVideoPayload::allocate(bd);
                if (!slot.isValid()) return slot;
                renderFrame(*slot.modify(), frameInCycle);
                // Seal the cached payload so SystemCow-backed allocators
                // can cheap-detach if the caller (rare) writes through
                // it; default backends no-op.
                (void)slot->data().seal();
        }
        return slot;
}

void MotionBand::renderFrame(UncompressedVideoPayload &out, int frameInCycle) const {
        const int w = static_cast<int>(out.desc().size().width());
        const int h = static_cast<int>(out.desc().size().height());

        // For non-paintable target formats render into an RGBA8 scratch
        // and CSC-convert into the target.  This mirrors the
        // VideoTestPattern fallback path so the band looks correct on
        // any pixel format the CSC framework can hit.
        if (!out.desc().pixelFormat().hasPaintEngine()) {
                ImageDesc rgb = rgbScratchDesc(out.desc(), h);
                auto      scratch = UncompressedVideoPayload::allocate(rgb);
                if (!scratch.isValid()) return;
                renderFrame(*scratch.modify(), frameInCycle);
                auto conv = scratch->convert(out.desc().pixelFormat(), out.desc().metadata(), MediaConfig());
                if (!conv.isValid()) {
                        promekiWarn("MotionBand::renderFrame: CSC to '%s' failed",
                                    out.desc().pixelFormat().name().cstr());
                        return;
                }
                // Copy converted bytes into out's buffers in place so
                // the caller's allocator (e.g. SystemCow) keeps owning
                // the slot.  Plane strides match because both share the
                // same width and pixel format.
                const PixelMemLayout &ml = out.desc().pixelFormat().memLayout();
                for (size_t p = 0; p < ml.planeCount(); p++) {
                        const size_t stride = ml.lineStride(p, w);
                        const size_t vSub = ml.planeDesc(p).vSubsampling;
                        const size_t rows = (vSub > 0) ? static_cast<size_t>(h) / vSub : static_cast<size_t>(h);
                        std::memcpy(out.data()[p].data(), conv->data()[p].data(), stride * rows);
                }
                return;
        }

        PaintEngine pe = out.createPaintEngine();
        auto        bgPixel = pe.createPixel(_backgroundColor);
        pe.fill(bgPixel);

        if (_sequenceLength <= 0) return;

        const int N = _sequenceLength;
        const int bw = (_borderWidth >= 1) ? _borderWidth : 1;

        // Slot i covers pixel range [i*w/N, (i+1)*w/N).  Using integer
        // math keeps slot boundaries deterministic and the active
        // slot fill always tessellates exactly with the borders that
        // get drawn over it.
        auto slotX0 = [&](int i) { return static_cast<int>((static_cast<long long>(i) * w) / N); };

        // Active slot fill — full slot rect, borders get drawn on top
        // so the framed look comes out clean even if borderWidth grows.
        const int activeX0 = slotX0(frameInCycle);
        const int activeX1 = slotX0(frameInCycle + 1);
        if (activeX1 > activeX0) {
                auto markerPixel = pe.createPixel(_markerColor);
                pe.fillRect(markerPixel, Rect<int32_t>(activeX0, 0, activeX1 - activeX0, h));
        }

        // Vertical separators only — N+1 lines total (left edge,
        // N-1 internal, right edge).  No top/bottom horizontals so
        // the active slot fill runs flush with the band's top and
        // bottom rows.  Internal separators are centred on the slot
        // boundary; the outer left/right are anchored flush with the
        // band edge so borderWidth > 1 doesn't push them off-frame.
        auto borderPixel = pe.createPixel(_borderColor);
        for (int i = 0; i <= N; i++) {
                int x = slotX0(i);
                int x0;
                if (i == 0)
                        x0 = 0;
                else if (i == N)
                        x0 = w - bw;
                else
                        x0 = x - bw / 2;
                if (x0 < 0) x0 = 0;
                int x1 = x0 + bw;
                if (x1 > w) x1 = w;
                if (x1 <= x0) continue;
                pe.fillRect(borderPixel, Rect<int32_t>(x0, 0, x1 - x0, h));
        }
}

Error MotionBand::apply(UncompressedVideoPayload &inout, uint64_t frameCount) const {
        if (!_enabled || _sequenceLength <= 0) return Error::Ok;
        if (!inout.isValid()) return Error::InvalidArgument;

        const int bandH = effectiveBandHeight(inout.desc());
        if (bandH <= 0 || bandH > static_cast<int>(inout.desc().size().height())) {
                return Error::NotSupported;
        }

        // Reduce frameCount to a non-negative cycle index, then add
        // the user's offset and re-modulo with a guard so a negative
        // offset that pushes the result below zero still resolves
        // inside [0, _sequenceLength).
        const int N = _sequenceLength;
        const int base = static_cast<int>(frameCount % static_cast<uint64_t>(N));
        const int frameInCycle = ((base + _offset) % N + N) % N;

        UncompressedVideoPayload::Ptr band = cachedFrame(frameInCycle, inout.desc());
        if (!band.isValid()) return Error::NotSupported;

        const PixelMemLayout &ml = inout.desc().pixelFormat().memLayout();
        const size_t          imgW = inout.desc().size().width();
        const int             topY = bandTopY(inout.desc(), bandH);

        for (size_t p = 0; p < ml.planeCount(); p++) {
                const size_t stride = ml.lineStride(p, imgW);
                if (stride == 0) continue;
                const size_t vSub = ml.planeDesc(p).vSubsampling;
                if (vSub == 0) continue;
                const size_t bandRows = static_cast<size_t>(bandH) / vSub;
                const size_t dstTopRow = static_cast<size_t>(topY) / vSub;
                uint8_t     *src = band->data()[p].data();
                uint8_t     *dst = inout.data()[p].data();
                if (src == nullptr || dst == nullptr) continue;
                dst += dstTopRow * stride;
                for (size_t r = 0; r < bandRows; r++) {
                        std::memcpy(dst + r * stride, src + r * stride, stride);
                }
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
