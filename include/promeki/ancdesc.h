/**
 * @file      ancdesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/ancformat.h>
#include <promeki/size2d.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/list.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class SdpMediaDescription;

/**
 * @brief Describes a single ancillary-data stream.
 * @ingroup proav
 *
 * Per-stream descriptor that lives on @ref MediaDesc::ancList,
 * parallel to @ref ImageDesc on the video side and @ref AudioDesc on
 * the audio side.  Captures the shape of the ANC stream (the raster
 * the packets were keyed to, the scan mode that interprets the line
 * numbers, the frame rate that drives RFC 8331 timing) plus the
 * optional filter sets a sink can use to declare "I only carry
 * Captions" or "I only carry CEA-708 + ATC".
 *
 * @par Filtering
 *
 * Two independent filters are layered, both default to "no
 * restriction":
 *
 *  - @ref allowedFormats — explicit per-@ref AncFormat whitelist.
 *  - @ref allowedCategories — coarser @ref AncCategory whitelist
 *    (Captions, Timecode, Splice, …).
 *
 * @ref acceptsFormat returns @c true iff both filters admit the
 * candidate.  An empty filter list means "no restriction"; sinks
 * that carry every format leave both filters empty.
 *
 * @par Pairing with video / audio streams
 *
 * @ref pairedVideoStreamIndex and @ref pairedAudioStreamIndex name the
 * @ref MediaPayload::streamIndex of the video / audio payload this ANC
 * stream is associated with on the enclosing @ref Frame.  The default
 * value @c -1 means "unbound" — the ANC stream is not attributed to a
 * specific essence and is treated as global to the Frame.  A producer
 * that knows the pairing (the SDI capture that interleaved VANC with
 * a specific link, an encoder hook that injects SEI captions on a
 * specific encoded video stream, …) stamps the matching streamIndex
 * here so downstream selectors can filter on it without inspecting
 * the wire bytes.
 *
 * @par Storage and copy semantics
 *
 * @c AncDesc is an internally-CoW value-type handle (the
 * post-2026-05-07 convention: no @c ::Ptr alias, no
 * @c PROMEKI_SHARED_FINAL on the outer class).  Copying an
 * @c AncDesc bumps an internal refcount; mutators @c (setSourceRaster,
 * @c setScanMode, @c setFrameRate, @c setAllowedFormats,
 * @c setAllowedCategories, @c setMetadata, @c metadata() &,
 * @c setPairedVideoStreamIndex, @c setPairedAudioStreamIndex) detach
 * via copy-on-write when the refcount is greater than one.  The
 * handle is one pointer wide and is cheap to pass through pipelines.
 *
 * @c AncDesc::List is @c List<AncDesc> — a vector of value-type
 * handles that share storage when copied.
 *
 * @par SDP round-trip
 *
 * SDP serialization is deferred to Phase 1 of the ANC stack devplan
 * (`devplan/proav/ancdata.md`), which is where the matching
 * @c RtpPayloadAnc and the SDP @c m=application section land.
 * The DataStream serialization round-trip lands here at Phase 0 so
 * file-based persistence (PMDF, etc.) works immediately.
 *
 * @see AncFormat, AncCategory, AncPayload, AncPacket,
 *      MediaDesc, ImageDesc, AudioDesc
 */
class AncDesc {
        public:
                /** @brief Plain-value list of @c AncDescs (handles share storage when copied). */
                using List = ::promeki::List<AncDesc>;

                /**
                 * @brief Default-constructs an invalid @c AncDesc.
                 *
                 * @ref isValid returns @c false until one of the
                 * meaningful fields is populated.
                 */
                AncDesc();

                /**
                 * @brief Constructs an @c AncDesc bound to a paired
                 *        video raster + scan mode + frame rate.
                 *
                 * Common-case constructor for ANC streams associated
                 * with a video stream — the raster and scan mode let
                 * a consumer interpret VANC line numbers without
                 * consulting the paired @ref ImageDesc.  Paired stream
                 * indices are left at @c -1 (unbound); producers that
                 * know the streamIndex of the paired video / audio
                 * call @ref setPairedVideoStreamIndex /
                 * @ref setPairedAudioStreamIndex afterwards.
                 *
                 * @param raster    Source video raster (0×0 = unbound).
                 * @param scanMode  Source scan mode.
                 * @param frameRate Frame rate of the paired video
                 *                  (drives ST 2110-40 packet timing).
                 */
                AncDesc(const Size2Du32 &raster, const VideoScanMode &scanMode, const FrameRate &frameRate);

                /** @brief Returns the source video raster (0×0 = unbound). */
                const Size2Du32 &sourceRaster() const;

                /** @brief Replaces the source video raster. */
                void setSourceRaster(const Size2Du32 &raster);

                /** @brief Returns the source scan mode. */
                const VideoScanMode &scanMode() const;

                /** @brief Replaces the source scan mode. */
                void setScanMode(const VideoScanMode &scanMode);

                /** @brief Returns the frame rate of the paired video. */
                const FrameRate &frameRate() const;

                /** @brief Replaces the frame rate. */
                void setFrameRate(const FrameRate &frameRate);

                /**
                 * @brief Returns the allowed-format whitelist
                 *        (empty = no restriction).
                 */
                const AncFormat::IDList &allowedFormats() const;

                /** @brief Replaces the allowed-format whitelist. */
                void setAllowedFormats(AncFormat::IDList ids);

                /**
                 * @brief Returns the allowed-category whitelist
                 *        (empty = no restriction).
                 */
                const ::promeki::List<AncCategory> &allowedCategories() const;

                /** @brief Replaces the allowed-category whitelist. */
                void setAllowedCategories(::promeki::List<AncCategory> categories);

                /** @brief Returns the descriptor's metadata container. */
                const Metadata &metadata() const;

                /** @brief Returns a mutable reference to the metadata container. */
                Metadata &metadata();

                /** @brief Replaces the metadata container. */
                void setMetadata(Metadata m);

                /**
                 * @brief Returns the @ref MediaPayload::streamIndex of
                 *        the video payload this ANC stream is paired
                 *        with on the enclosing @ref Frame.
                 *
                 * Returns @c -1 when the ANC stream is not attributed
                 * to a specific video stream (the default for SDP-
                 * derived descriptors, RTP-only sources, or any
                 * producer that does not know the pairing).
                 */
                int pairedVideoStreamIndex() const;

                /** @brief Sets the paired video stream index; @c -1 clears the pairing. */
                void setPairedVideoStreamIndex(int index);

                /**
                 * @brief Returns the @ref MediaPayload::streamIndex of
                 *        the audio payload this ANC stream is paired
                 *        with on the enclosing @ref Frame.
                 *
                 * Returns @c -1 when the ANC stream is not attributed
                 * to a specific audio stream (the common case — most
                 * ANC streams pair with video).  Non-default values
                 * are used for ANC carriages that ride alongside a
                 * specific audio track (e.g. cue points associated
                 * with one audio program).
                 */
                int pairedAudioStreamIndex() const;

                /** @brief Sets the paired audio stream index; @c -1 clears the pairing. */
                void setPairedAudioStreamIndex(int index);

                /**
                 * @brief Returns @c true when the descriptor carries
                 *        enough information to be meaningfully consumed.
                 *
                 * A descriptor is valid when either:
                 *  - it is bound to a positive-area raster paired with
                 *    a valid scan mode (the common case, where ANC
                 *    accompanies a video stream); or
                 *  - it is unbound (0×0 raster) but declares an
                 *    explicit @ref allowedFormats or
                 *    @ref allowedCategories filter, signalling the
                 *    application intent for the stream.
                 */
                bool isValid() const;

                /**
                 * @brief Returns @c true when @p fmt is admitted by
                 *        this descriptor's filters.
                 *
                 * An empty @ref allowedFormats list admits every
                 * format; otherwise @p fmt's ID must appear in the
                 * list.  An empty @ref allowedCategories list admits
                 * every category; otherwise @p fmt's category must
                 * appear in the list.  Both filters must admit @p fmt
                 * for @c acceptsFormat to return @c true.
                 */
                bool acceptsFormat(const AncFormat &fmt) const;

                /**
                 * @brief Returns @c true when @p other has the same
                 *        format-shape fields as this descriptor.
                 *
                 * Compares raster, scan mode, frame rate, the two
                 * filter lists, and the paired stream indices.
                 * Metadata is ignored — mirror of
                 * @ref AudioDesc::formatEquals and
                 * @ref ImageDesc::formatEquals.
                 */
                bool formatEquals(const AncDesc &other) const;

                /**
                 * @brief Builds an @c AncDesc from an SDP
                 *        @c m=application section (RFC 8331).
                 *
                 * Recognises the @c smpte291 rtpmap encoding and
                 * parses the @c DID_SDID fmtp parameter list — RFC
                 * 8331 §6.2 permits multiple @c DID_SDID entries
                 * inside one @c a=fmtp line, separated by @c ; .
                 * Each pair is resolved through
                 * @ref AncFormat::fromSt291DidSdid and pushed onto
                 * @ref allowedFormats; pairs with no registered
                 * format are kept on @ref allowedFormats anyway (as
                 * @c AncFormat::Invalid IDs) so the application
                 * still knows the wire-side intent.
                 *
                 * The other AncDesc fields
                 * (@ref sourceRaster, @ref scanMode,
                 * @ref frameRate, paired stream indices) are
                 * intentionally left at their defaults — RFC 8331
                 * SDP does not carry them; they are populated by the
                 * caller from the paired @ref ImageDesc when one
                 * exists.  Returns a default @c AncDesc when the SDP
                 * section has the wrong media type or a malformed
                 * rtpmap.
                 */
                static AncDesc fromSdp(const SdpMediaDescription &md);

                /**
                 * @brief Builds an SDP @c m=application section for
                 *        this descriptor (RFC 8331).
                 *
                 * Emits @c m=application @c 0 @c RTP/AVP @c <pt> ,
                 * @c a=rtpmap:<pt> @c smpte291/90000 , and an
                 * @c a=fmtp:<pt> line carrying one
                 * @c DID_SDID={DID,SDID} entry per St291-carriage
                 * @ref AncFormat covered by this descriptor.
                 *
                 * The fmtp set is
                 * <tt>registeredIDsForTransport(St291)</tt>
                 * intersected with @ref allowedFormats when
                 * @ref allowedFormats is non-empty; when it is
                 * empty the full St291 registry ships.  DID and
                 * SDID are written as @c 0xXX hex literals, the
                 * common interop form.
                 *
                 * @param payloadType Dynamic RTP payload type
                 *                    (typically 100 for ANC).
                 */
                SdpMediaDescription toSdp(uint8_t payloadType) const;

                /**
                 * @brief Equality compares every field, including
                 *        @ref metadata.
                 */
                bool operator==(const AncDesc &other) const;

                /** @brief Inequality. */
                bool operator!=(const AncDesc &other) const { return !(*this == other); }

                /**
                 * @brief Private @c Impl struct holding the
                 *        descriptor's state.
                 *
                 * Marked @c PROMEKI_SHARED_FINAL so @c SharedPtr<Impl>
                 * can refcount it natively with CoW semantics.
                 * Exposed publicly only so the in-namespace stream
                 * operators can poke at fields without a friend
                 * declaration; application code should not depend on
                 * the @c Impl layout.
                 */
                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                Size2Du32                    sourceRaster;
                                VideoScanMode                scanMode;
                                FrameRate                    frameRate;
                                AncFormat::IDList            allowedFormats;
                                ::promeki::List<AncCategory> allowedCategories;
                                Metadata                     metadata;
                                int                          pairedVideoStreamIndex = -1;
                                int                          pairedAudioStreamIndex = -1;
                };

        private:
                SharedPtr<Impl> _d;
};

/**
 * @brief Writes an @c AncDesc as raster + scan mode + frame rate +
 *        filter lists + paired stream indices + metadata.
 */
DataStream &operator<<(DataStream &stream, const AncDesc &desc);

/** @brief Reads an @c AncDesc from its tagged wire format. */
DataStream &operator>>(DataStream &stream, AncDesc &desc);

PROMEKI_NAMESPACE_END
