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
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

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
 * @par Storage and copy semantics
 *
 * @c AncDesc is a shareable data object per
 * @ref dataobjects.dox.  Plain values copy by deep copy (small —
 * a handful of POD-ish members plus a CoW @ref Metadata).
 * @c AncDesc::Ptr shares a single instance via atomic refcount; the
 * @c PtrList alias is preferred when sinks accept many descriptors
 * that should ride together.
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
                PROMEKI_SHARED_FINAL(AncDesc)
        public:
                /** @brief Shared pointer to an AncDesc. */
                using Ptr = SharedPtr<AncDesc>;

                /** @brief Plain-value list of @c AncDescs. */
                using List = ::promeki::List<AncDesc>;

                /** @brief List of @ref Ptr — shared descriptors sharing storage. */
                using PtrList = ::promeki::List<Ptr>;

                /**
                 * @brief Default-constructs an invalid @c AncDesc.
                 *
                 * @ref isValid returns @c false until one of the
                 * meaningful fields is populated.
                 */
                AncDesc() = default;

                /**
                 * @brief Constructs an @c AncDesc bound to a paired
                 *        video raster + scan mode + frame rate.
                 *
                 * Common-case constructor for ANC streams associated
                 * with a video stream — the raster and scan mode let
                 * a consumer interpret VANC line numbers without
                 * consulting the paired @ref ImageDesc.
                 *
                 * @param raster    Source video raster (0×0 = unbound).
                 * @param scanMode  Source scan mode.
                 * @param frameRate Frame rate of the paired video
                 *                  (drives ST 2110-40 packet timing).
                 */
                AncDesc(const Size2Du32 &raster, const VideoScanMode &scanMode, const FrameRate &frameRate)
                    : _sourceRaster(raster), _scanMode(scanMode), _frameRate(frameRate) {}

                /** @brief Returns the source video raster (0×0 = unbound). */
                const Size2Du32 &sourceRaster() const { return _sourceRaster; }

                /** @brief Replaces the source video raster. */
                void setSourceRaster(const Size2Du32 &raster) { _sourceRaster = raster; }

                /** @brief Returns the source scan mode. */
                const VideoScanMode &scanMode() const { return _scanMode; }

                /** @brief Replaces the source scan mode. */
                void setScanMode(const VideoScanMode &scanMode) { _scanMode = scanMode; }

                /** @brief Returns the frame rate of the paired video. */
                const FrameRate &frameRate() const { return _frameRate; }

                /** @brief Replaces the frame rate. */
                void setFrameRate(const FrameRate &frameRate) { _frameRate = frameRate; }

                /**
                 * @brief Returns the allowed-format whitelist
                 *        (empty = no restriction).
                 */
                const AncFormat::IDList &allowedFormats() const { return _allowedFormats; }

                /** @brief Replaces the allowed-format whitelist. */
                void setAllowedFormats(AncFormat::IDList ids) { _allowedFormats = std::move(ids); }

                /**
                 * @brief Returns the allowed-category whitelist
                 *        (empty = no restriction).
                 */
                const ::promeki::List<AncCategory> &allowedCategories() const { return _allowedCategories; }

                /** @brief Replaces the allowed-category whitelist. */
                void setAllowedCategories(::promeki::List<AncCategory> categories) {
                        _allowedCategories = std::move(categories);
                }

                /** @brief Returns the descriptor's metadata container. */
                const Metadata &metadata() const { return _metadata; }

                /** @brief Returns a mutable reference to the metadata container. */
                Metadata &metadata() { return _metadata; }

                /** @brief Replaces the metadata container. */
                void setMetadata(Metadata m) { _metadata = std::move(m); }

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
                bool isValid() const {
                        const bool hasRaster = _sourceRaster.width() > 0 && _sourceRaster.height() > 0;
                        const bool hasScanMode = _scanMode != VideoScanMode::Unknown;
                        if (hasRaster && hasScanMode) return true;
                        return !_allowedFormats.isEmpty() || !_allowedCategories.isEmpty();
                }

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
                bool acceptsFormat(const AncFormat &fmt) const {
                        if (!_allowedFormats.isEmpty()) {
                                bool found = false;
                                for (auto id : _allowedFormats) {
                                        if (id == fmt.id()) {
                                                found = true;
                                                break;
                                        }
                                }
                                if (!found) return false;
                        }
                        if (!_allowedCategories.isEmpty()) {
                                bool found = false;
                                for (const auto &cat : _allowedCategories) {
                                        if (cat == fmt.category()) {
                                                found = true;
                                                break;
                                        }
                                }
                                if (!found) return false;
                        }
                        return true;
                }

                /**
                 * @brief Returns @c true when @p other has the same
                 *        format-shape fields as this descriptor.
                 *
                 * Compares raster, scan mode, frame rate, and the two
                 * filter lists.  Metadata is ignored — mirror of
                 * @ref AudioDesc::formatEquals and
                 * @ref ImageDesc::formatEquals.
                 */
                bool formatEquals(const AncDesc &other) const {
                        if (!(_sourceRaster == other._sourceRaster)) return false;
                        if (!(_scanMode == other._scanMode)) return false;
                        if (!(_frameRate == other._frameRate)) return false;
                        if (_allowedFormats.size() != other._allowedFormats.size()) return false;
                        for (size_t i = 0; i < _allowedFormats.size(); ++i) {
                                if (_allowedFormats.at(i) != other._allowedFormats.at(i)) return false;
                        }
                        if (_allowedCategories.size() != other._allowedCategories.size()) return false;
                        for (size_t i = 0; i < _allowedCategories.size(); ++i) {
                                if (!(_allowedCategories.at(i) == other._allowedCategories.at(i))) return false;
                        }
                        return true;
                }

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
                 * @ref frameRate) are intentionally left at their
                 * defaults — RFC 8331 SDP does not carry them; they
                 * are populated by the caller from the paired
                 * @ref ImageDesc when one exists.  Returns a default
                 * @c AncDesc when the SDP section has the wrong
                 * media type or a malformed rtpmap.
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
                bool operator==(const AncDesc &other) const {
                        return formatEquals(other) && _metadata == other._metadata;
                }

                /** @brief Inequality. */
                bool operator!=(const AncDesc &other) const { return !(*this == other); }

        private:
                Size2Du32                 _sourceRaster;
                VideoScanMode             _scanMode;
                FrameRate                 _frameRate;
                AncFormat::IDList         _allowedFormats;
                ::promeki::List<AncCategory> _allowedCategories;
                Metadata                  _metadata;
};

/**
 * @brief Writes an @c AncDesc as raster + scan mode + frame rate +
 *        filter lists + metadata.
 */
inline DataStream &operator<<(DataStream &stream, const AncDesc &desc) {
        stream.writeTag(DataStream::TypeAncDesc);
        stream << desc.sourceRaster();
        stream << desc.scanMode();
        stream << desc.frameRate();
        const AncFormat::IDList &fmts = desc.allowedFormats();
        stream << static_cast<uint32_t>(fmts.size());
        for (auto id : fmts) stream << static_cast<int32_t>(id);
        const ::promeki::List<AncCategory> &cats = desc.allowedCategories();
        stream << static_cast<uint32_t>(cats.size());
        for (const auto &cat : cats) stream << cat;
        stream << desc.metadata();
        return stream;
}

/** @brief Reads an @c AncDesc from its tagged wire format. */
inline DataStream &operator>>(DataStream &stream, AncDesc &desc) {
        if (!stream.readTag(DataStream::TypeAncDesc)) {
                desc = AncDesc();
                return stream;
        }
        Size2Du32     raster;
        VideoScanMode scanMode;
        FrameRate     frameRate;
        uint32_t      fmtCount = 0;
        uint32_t      catCount = 0;
        Metadata      meta;
        stream >> raster >> scanMode >> frameRate >> fmtCount;
        AncFormat::IDList fmts;
        fmts.reserve(fmtCount);
        for (uint32_t i = 0; i < fmtCount; ++i) {
                int32_t v = 0;
                stream >> v;
                fmts.pushToBack(static_cast<AncFormat::ID>(v));
        }
        stream >> catCount;
        ::promeki::List<AncCategory> cats;
        cats.reserve(catCount);
        for (uint32_t i = 0; i < catCount; ++i) {
                AncCategory cat;
                stream >> cat;
                cats.pushToBack(cat);
        }
        stream >> meta;
        if (stream.status() != DataStream::Ok) {
                desc = AncDesc();
                return stream;
        }
        desc = AncDesc();
        desc.setSourceRaster(raster);
        desc.setScanMode(scanMode);
        desc.setFrameRate(frameRate);
        desc.setAllowedFormats(std::move(fmts));
        desc.setAllowedCategories(std::move(cats));
        desc.setMetadata(std::move(meta));
        return stream;
}

PROMEKI_NAMESPACE_END
