/**
 * @file      uncompressedvideopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/uniqueptr.h>
#include <promeki/videopayload.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;
class PaintEngine;

/**
 * @brief Uncompressed video payload — raster pixels in the format
 *        described by the base @ref VideoPayload's @ref ImageDesc.
 * @ingroup proav
 *
 * The plane list on the base carries one @ref BufferView per plane
 * defined by the descriptor's @ref PixelFormat — one entry for
 * packed / interleaved formats (RGBA, YUYV), multiple entries for
 * planar formats (Y/U/V), and multiple-views-on-one-buffer when a
 * planar payload was read in a single allocation.  The number of
 * entries @em should match @c desc().planeCount, but that invariant
 * is left to callers rather than being enforced by the payload
 * itself so partial construction (create descriptor, attach planes
 * later) stays ergonomic.
 *
 * Every uncompressed payload is trivially keyframe-able — each
 * frame decodes on its own — so @ref isKeyframe is overridden to
 * always return @c true regardless of whether the @c Keyframe flag
 * has been set manually.
 *
 * UncompressedVideoPayload is intentionally not @c final — a future
 * CUDA- or GL-backed variant may extend it to tag its planes with
 * device-memory provenance, for example.
 *
 * @par Example — allocating from a descriptor
 * @code
 * ImageDesc desc(1920, 1080, PixelFormat::RGBA8_sRGB);
 * auto buf = Buffer::Ptr::create(desc.pixelFormat().planeSize(0, desc));
 * BufferView plane0(buf, 0, buf->size());
 * auto payload = UncompressedVideoPayload::Ptr::create(
 *         desc, plane0);
 * @endcode
 */
class UncompressedVideoPayload : public VideoPayload {
        public:
                virtual UncompressedVideoPayload *_promeki_clone() const override {
                        return new UncompressedVideoPayload(*this);
                }

                /** @brief Shared-pointer alias for UncompressedVideoPayload ownership. */
                using Ptr = SharedPtr<UncompressedVideoPayload, /*CopyOnWrite=*/true, UncompressedVideoPayload>;

                /** @brief List of shared pointers to UncompressedVideoPayload instances. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Unique-ownership pointer to an UncompressedVideoPayload. */
                using UPtr = UniquePtr<UncompressedVideoPayload>;

                /** @brief Constructs an empty uncompressed video payload. */
                UncompressedVideoPayload() = default;

                /**
                 * @brief Constructs an uncompressed payload with the given descriptor.
                 *
                 * Plane list left empty — attach via @ref setData.
                 */
                explicit UncompressedVideoPayload(const ImageDesc &desc) :
                        VideoPayload(desc) { }

                /**
                 * @brief Constructs an uncompressed payload with a descriptor and planes.
                 */
                UncompressedVideoPayload(const ImageDesc &desc,
                                         const BufferView &data) :
                        VideoPayload(desc, data) { }

                /**
                 * @brief Always returns @c false — this class only models
                 *        uncompressed payloads.
                 */
                bool isCompressed() const override { return false; }

                /**
                 * @brief Trivially true — every uncompressed frame is a
                 *        self-contained decode entry point.
                 */
                bool isKeyframe() const override { return true; }

                /**
                 * @brief Trivially true — cutting before any uncompressed
                 *        frame leaves downstream consumers in a coherent
                 *        state.
                 */
                bool isSafeCutPoint() const override { return true; }

                /**
                 * @brief Converts this payload to a different raster
                 *        pixel format.
                 *
                 * Thin payload-native wrapper for raster-format
                 * conversion — runs the CSC pipeline directly on this
                 * payload's planes and returns the result as a fresh
                 * @ref UncompressedVideoPayload.  Zero-copy when the
                 * payload's planes already cover their backing buffers
                 * in full.
                 *
                 * @param dstPd     The target raster pixel format.
                 *                  Passing a compressed format is a
                 *                  programming error and returns a
                 *                  null Ptr.
                 * @param metadata  Metadata to attach to the converted
                 *                  payload.
                 * @param config    Optional @ref MediaConfig hints
                 *                  (CSC path, etc.).
                 * @return A fresh uncompressed payload in @p dstPd, or
                 *         a null Ptr on failure.
                 */
                Ptr convert(const PixelFormat &dstPd,
                            const Metadata &metadata,
                            const MediaConfig &config) const;

                /** @copybrief convert(const PixelFormat &, const Metadata &, const MediaConfig &) const */
                Ptr convert(const PixelFormat &dstPd,
                            const Metadata &metadata) const;

                /**
                 * @brief Allocates a fresh payload with backing buffers
                 *        sized for @p desc.
                 *
                 * Convenience helper for producers that want to fill
                 * bytes directly into plane buffers they own.  One
                 * @ref Buffer is allocated per plane, sized to the
                 * descriptor's per-plane byte count (line pitch x
                 * height for interleaved and each plane of planar
                 * formats).  Each plane's @ref BufferView covers its
                 * buffer in full so downstream consumers see the
                 * whole-plane invariant.
                 *
                 * Returns a null Ptr if the descriptor's pixel format
                 * or size is invalid.
                 */
                static Ptr allocate(const ImageDesc &desc);

                /** @brief Stable FourCC for DataStream serialisation. */
                static constexpr FourCC kSubclassFourCC{'U','V','d','p'};

                uint32_t subclassFourCC() const override {
                        return kSubclassFourCC.value();
                }

                /** @copydoc MediaPayload::serialisePayload */
                void serialisePayload(DataStream &s) const override;

                /** @copydoc MediaPayload::deserialisePayload */
                void deserialisePayload(DataStream &s) override;

                /**
                 * @brief Creates a @ref PaintEngine targeting this payload's
                 *        pixel buffer.
                 *
                 * Payload-native paint engine factory.  The returned
                 * engine writes directly to the payload's plane-0
                 * @ref Buffer when its view covers the buffer in full
                 * (the common case), so paint operations are visible
                 * through any @ref UncompressedVideoPayload sharing
                 * the same backing buffer.
                 *
                 * Callers that need exclusive ownership of the pixel
                 * buffer (CoW-detach before paint) should duplicate
                 * the payload first — there is no
                 * @c ensureExclusive on @ref UncompressedVideoPayload
                 * yet; that lands when the compositor stages flip
                 * onto the payload-native paint path.
                 */
                PaintEngine createPaintEngine() const;

                UncompressedVideoPayload(const UncompressedVideoPayload &) = default;
                UncompressedVideoPayload(UncompressedVideoPayload &&) = default;
                UncompressedVideoPayload &operator=(const UncompressedVideoPayload &) = default;
                UncompressedVideoPayload &operator=(UncompressedVideoPayload &&) = default;
};

PROMEKI_NAMESPACE_END
