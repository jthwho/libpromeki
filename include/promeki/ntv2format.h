/**
 * @file      ntv2format.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/enums_video.h>
#include <promeki/error.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/namespace.h>
#include <promeki/pixelformat.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/string.h>
#include <promeki/videoportref.h>
#include <promeki/videoreferenceconfig.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Free-function helpers translating between AJA NTV2 enums
 *        and libpromeki's generic types.
 * @ingroup proav
 *
 * Translation lives in its own header so other NTV2 files (device,
 * clock, MediaIO) can share one canonical mapping without depending
 * on each other.  All functions are pure — no state, no logging,
 * no SDK calls — so they are cheap to use on hot paths and testable
 * without hardware.
 *
 * @par SDK enum representation
 *
 * The AJA SDK's enums (@c NTV2VideoFormat, @c NTV2FrameBufferFormat,
 * @c NTV2InputSource, @c NTV2Channel, @c NTV2ReferenceSource) are
 * exposed here as plain @c int so callers don't need to drag the
 * libajantv2 headers into translation units that only consume the
 * mappings.  The @c .cpp pulls in the real headers and casts at the
 * boundary.  The integer-as-enum pattern follows the precedent set
 * by @ref NdiFormat (FourCC values as @c uint32_t).
 *
 * The helpers return the SDK's "unknown" sentinel value (typically
 * @c NTV2_FORMAT_UNKNOWN / @c NTV2_FBF_INVALID, both @c 0 in the
 * SDK at the time of writing) when no mapping applies, so callers
 * can fall back to the planner's CSC bridge without a separate
 * error channel.
 */
namespace Ntv2Format {

        // ---- Pixel format ----

        /**
         * @brief Translates a promeki @ref PixelFormat::ID to an AJA
         *        @c NTV2FrameBufferFormat (returned as @c int).
         *
         * Returns @c NTV2_FBF_INVALID (the SDK's sentinel) when the
         * format has no direct NTV2 equivalent.  Phase-1 coverage is
         * the eight uncompressed shapes the demo capture path uses;
         * the V210 packed-10-bit format ships in Phase 5 as a first-
         * class @ref PixelFormat addition.
         */
        int toNtv2PixelFormat(PixelFormat::ID id);

        /**
         * @brief Inverse of @ref toNtv2PixelFormat.
         *
         * Returns @c PixelFormat::Invalid when the NTV2 format does
         * not currently map to a libpromeki @c PixelFormat::ID.
         */
        PixelFormat::ID fromNtv2PixelFormat(int fbf);

        // ---- Video format ----

        /**
         * @brief Returns the NTV2 video format matching the
         *        @p image / @p rate combination.
         *
         * Combines raster size, frame rate, and scan mode into one
         * of the SDK's @c NTV2_FORMAT_* identifiers.  Returns
         * @c NTV2_FORMAT_UNKNOWN (== 0) when no single enum value
         * covers the combination (typically: an unsupported raster,
         * a non-broadcast rate, or an interlaced raster not in the
         * common 1080i / 525i / 625i set).
         *
         * @param image The image descriptor (raster + scan mode).
         * @param rate  The frame rate.
         */
        int toNtv2VideoFormat(const ImageDesc &image, const FrameRate &rate);

        /**
         * @brief Inverse of @ref toNtv2VideoFormat.
         *
         * Decomposes an NTV2 video format into a raster size, frame
         * rate, and scan mode.  Any of the outputs may be @c nullptr
         * if the caller doesn't need that piece.  Returns
         * @c Error::InvalidArgument when @p fmt is
         * @c NTV2_FORMAT_UNKNOWN or unrecognised.
         *
         * @param fmt     The NTV2 video format value.
         * @param outSize Optional output: the raster size.
         * @param outRate Optional output: the frame rate.
         * @param outScan Optional output: the scan mode.
         */
        Error fromNtv2VideoFormat(int fmt, Size2Du32 *outSize, FrameRate *outRate,
                                  VideoScanMode *outScan);

        // ---- Input source / channel / reference ----

        /**
         * @brief Maps a 1-based logical channel index to an
         *        @c NTV2Channel enum value.
         *
         * Returns the SDK's @c NTV2_CHANNEL_INVALID when @p channel
         * is out of range (must be 1..8).
         */
        int toNtv2Channel(int channel);

        /**
         * @brief Inverse of @ref toNtv2Channel.
         *
         * Returns 0 when @p ntv2Ch is invalid.
         */
        int fromNtv2Channel(int ntv2Ch);

        /**
         * @brief Maps a @ref VideoPortRef to the matching
         *        @c NTV2InputSource enum value.
         *
         * Supports @ref VideoConnectorKind::Sdi and
         * @ref VideoConnectorKind::Hdmi for indices 1..8 and 1..4
         * respectively.  Returns @c NTV2_INPUTSOURCE_INVALID for
         * any other kind / index combination.
         */
        int portToInputSource(const VideoPortRef &port);

        /**
         * @brief Maps a @ref VideoReferenceConfig to an AJA
         *        @c NTV2ReferenceSource enum value.
         *
         * @c FreeRun → @c NTV2_REFERENCE_FREERUN.  @c Genlock /
         * @c External → @c NTV2_REFERENCE_EXTERNAL.  @c FromSignal
         * picks the SDI / HDMI input matching the config's
         * @c signalPort.  @c Ptp picks one of the SFP-PTP inputs
         * (implementation defaults to @c SFP1).  Unknown sources
         * fall through to @c NTV2_REFERENCE_FREERUN as a safe
         * default.
         */
        int referenceFor(const VideoReferenceConfig &ref);

        // ---- SDI link standard ----

        /**
         * @brief Returns @c true when the given link standard can
         *        be carried by an AJA card that exposes
         *        @p availableCables physical SDI cables.
         *
         * Pure compatibility check against the per-standard cable
         * count; the caller is responsible for validating
         * topology-specific requirements (12G connector presence,
         * routing presets, etc.) separately.  @ref SdiLinkStandard::Auto
         * is always accepted.
         */
        bool standardFitsCableCount(const SdiLinkStandard &standard, int availableCables);

} // namespace Ntv2Format

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
