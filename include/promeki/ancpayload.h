/**
 * @file      ancpayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediapayload.h>
#include <promeki/ancdesc.h>
#include <promeki/ancpacket.h>
#include <promeki/duration.h>
#include <promeki/fourcc.h>
#include <promeki/list.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A @ref MediaPayload carrying ancillary-data packets
 *        alongside the video and audio payloads of a frame.
 * @ingroup proav
 *
 * @c AncPayload binds an @ref AncDesc (the per-stream shape) to an
 * @ref AncPacket::List (the per-frame packet vector) and rides
 * through the @ref MediaPipeline as a peer of @ref VideoPayload and
 * @ref AudioPayload.  No backing @ref BufferView is needed because
 * each @ref AncPacket already owns its own wire bytes via its
 * internal @ref Buffer; @ref MediaPayload::data is left empty.
 *
 * The CoW value-type nature of the underlying @ref AncPacket means
 * copying an @c AncPayload is cheap — the @ref AncPacket::List
 * itself bumps refcounts on each contained packet's @c Impl rather
 * than deep-copying the wire bytes.
 *
 * @par FourCC
 *
 * Stable serialisation FourCC is @c "ANCp", registered via
 * @ref PROMEKI_REGISTER_MEDIAPAYLOAD in @c ancpayload.cpp.
 *
 * @par Duration
 *
 * ANC payloads carry a wall-clock duration on the same model as
 * @ref VideoPayload — a zero @ref Duration means "not yet stamped"
 * and @ref MediaIO treats that as a fill site, assigning one frame
 * of the session rate.  This keeps RFC 8331 packet pacing aligned
 * with the video stream that produced the ANC.
 *
 * @see AncDesc, AncPacket, MediaPayload, AncFormat
 */
class AncPayload : public MediaPayload {
        public:
                PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH(AncPayload)

                /** @brief Shared-pointer alias for @c AncPayload ownership. */
                using Ptr = SharedPtr<AncPayload, /*CopyOnWrite=*/true, AncPayload>;

                /** @brief List of shared pointers to @c AncPayload instances. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Stable FourCC for DataStream serialisation. */
                static constexpr FourCC kSubclassFourCC{'A', 'N', 'C', 'p'};

                /** @brief Default-constructs an empty @c AncPayload. */
                AncPayload() = default;

                /**
                 * @brief Constructs an @c AncPayload bound to a stream
                 *        descriptor.
                 *
                 * The packet list is left empty; producers call
                 * @ref addPacket to attach packets.
                 */
                explicit AncPayload(const AncDesc &desc) : _desc(desc) {}

                /**
                 * @brief Constructs an @c AncPayload with both
                 *        descriptor and initial packet list.
                 */
                AncPayload(const AncDesc &desc, AncPacket::List packets)
                    : _desc(desc), _packets(std::move(packets)) {}

                /** @brief Returns @c MediaPayloadKind::AncillaryData. */
                const MediaPayloadKind &kind() const override { return MediaPayloadKind::AncillaryData; }

                /** @brief Returns @c false — ANC packets carry their wire bytes verbatim. */
                bool isCompressed() const override { return false; }

                /** @brief Forwards to @c desc().metadata(). */
                const Metadata &metadata() const override { return _desc.metadata(); }

                /** @copydoc metadata() const */
                Metadata &metadata() override { return _desc.metadata(); }

                /** @brief ANC payloads always support a duration. */
                bool hasDuration() const override { return true; }

                /** @brief Returns the per-payload duration. */
                Duration duration() const override { return _duration; }

                /** @brief Stores @p val as the payload's duration. */
                Error setDuration(const Duration &val) override {
                        _duration = val;
                        return Error::Ok;
                }

                /** @brief Returns the stable subclass FourCC value. */
                uint32_t subclassFourCC() const override { return kSubclassFourCC.value(); }

                /**
                 * @brief Returns @c true by default — @c AncPacket
                 *        wire buffers are typically freshly built per
                 *        capture and do not alias other payloads' base
                 *        data.
                 *
                 * If an application constructs an @c AncPacket whose
                 * underlying @ref Buffer is shared with another
                 * payload's plane, override / customise via
                 * @ref MediaPayload::isExclusiveExtras at a higher
                 * layer.
                 */
                bool isExclusiveExtras() const override { return true; }

                /** @brief Returns the stream descriptor. */
                const AncDesc &desc() const { return _desc; }

                /** @brief Returns a mutable reference to the stream descriptor. */
                AncDesc &desc() { return _desc; }

                /** @brief Replaces the stream descriptor. */
                void setDesc(const AncDesc &d) { _desc = d; }

                /** @brief Returns the per-frame packet list. */
                const AncPacket::List &packets() const { return _packets; }

                /** @brief Returns a mutable reference to the per-frame packet list. */
                AncPacket::List &packets() { return _packets; }

                /** @brief Appends @p pkt to the packet list. */
                void addPacket(const AncPacket &pkt) { _packets.pushToBack(pkt); }

                /** @brief Clears the packet list. */
                void clearPackets() { _packets.clear(); }

                /** @brief Returns the packets whose @c format matches @p fmt. */
                AncPacket::List packetsOfFormat(const AncFormat &fmt) const;

                /**
                 * @brief Returns the packets whose @c format's
                 *        @c category matches @p category.
                 */
                AncPacket::List packetsOfCategory(const AncCategory &category) const;

                /** @brief Returns the packets whose @c transport matches @p transport. */
                AncPacket::List packetsOfTransport(const AncTransport &transport) const;

                /** @brief Returns @c true when any packet has @p fmt. */
                bool hasFormat(const AncFormat &fmt) const;

                /**
                 * @brief Returns @c true when any packet's format
                 *        category matches @p category.
                 */
                bool hasCategory(const AncCategory &category) const;

                /** @brief Deep clone for @ref SharedPtr CoW detach. */
                AncPayload *_promeki_clone() const override { return new AncPayload(*this); }

                void serialisePayload(DataStream &s) const override;
                void deserialisePayload(DataStream &s) override;

                AncPayload(const AncPayload &) = default;
                AncPayload(AncPayload &&) = default;
                AncPayload &operator=(const AncPayload &) = default;
                AncPayload &operator=(AncPayload &&) = default;

        private:
                AncDesc          _desc;
                AncPacket::List  _packets;
                Duration         _duration;
};

PROMEKI_NAMESPACE_END
