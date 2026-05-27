/**
 * @file      hdmiinfoframe.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/ancpacket.h>
#include <promeki/buffer.h>
#include <promeki/result.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Typed accessor over an @ref AncPacket whose transport is
 *        @c AncTransport::HdmiInfoFrame.
 * @ingroup proav
 *
 * Composition helper for the HDMI / CEA-861 InfoFrame transport.
 * Holds an @ref AncPacket by value and exposes the InfoFrame's
 * 4-byte header (Type, Version, Length, Checksum) plus the variable-
 * length payload that follows.
 *
 * @par Wire format (CEA-861-G §6.1)
 *
 * @code
 * byte 0 : Type   (0x82 = AVI, 0x83 = SPD, 0x84 = Audio,
 *                  0x87 = DRM, 0x81 = Vendor-Specific, ...)
 * byte 1 : Version
 * byte 2 : Length (count of payload bytes that follow this header)
 * byte 3 : Checksum (mod-256 sum of bytes 0..3+Length is zero)
 * bytes 4..(3+Length) : Payload
 * @endcode
 *
 * @ref AncPacket::data on the wrapped packet holds the entire
 * InfoFrame including the 4-byte header.  The header bytes are also
 * mirrored into @ref AncPacket::meta via @c AncMeta::HdmiInfoFrame
 * for fast access without re-parsing the data buffer.
 *
 * The HDMI capture / sink wiring for this transport is out of scope
 * for the Phase 0 contract; the class ships now so the rest of the
 * ANC stack (translators, codecs, typed parsers) has a stable
 * vehicle to work against, ready for a future DeckLink HDMI or
 * similar MediaIO backend.
 *
 * @note Not related to @ref HdmiSignalConfig despite the shared @c Hdmi
 *       prefix — this class is a @em typed @em packet @em helper for
 *       the CEA-861 InfoFrame wire format that rides through ANC,
 *       while @ref HdmiSignalConfig is a @em carrier-level @em descriptor
 *       (port + spec-version hint) that backends consume at open time.
 *
 * @see AncPacket, AncFormat, AncMeta::HdmiInfoFrame, HdmiSignalConfig
 */
class HdmiInfoFrame {
        public:
                /** @brief InfoFrame header length in bytes (Type, Version, Length, Checksum). */
                static constexpr size_t HeaderSize = 4;

                /**
                 * @brief Promotes an @ref AncPacket on the
                 *        @c HdmiInfoFrame transport to a typed view.
                 *
                 * Validates the transport and that the data buffer is
                 * at least @ref HeaderSize bytes long (i.e. contains a
                 * complete InfoFrame header).
                 */
                static Result<HdmiInfoFrame> from(const AncPacket &pkt);

                /**
                 * @brief Builds an InfoFrame from a registered
                 *        @ref AncFormat plus version + body bytes.
                 *
                 * Resolves the InfoFrame type byte from
                 * @c fmt.hdmiInfoFrameType() (asserts non-zero — the
                 * format must have a registered HDMI representation).
                 * The Length field is set to @c body.size and the
                 * Checksum is computed so that the mod-256 sum of all
                 * bytes (header + body) is zero.
                 *
                 * @param fmt     The logical format.
                 * @param version The InfoFrame version byte
                 *                (codec-defined; e.g. AVI 2 / DRM 1).
                 * @param body    The payload bytes following the
                 *                4-byte header.
                 */
                static HdmiInfoFrame build(const AncFormat &fmt, uint8_t version, Buffer body);

                /**
                 * @brief Escape hatch for InfoFrame types not in the
                 *        registry.
                 *
                 * Wraps caller-supplied @p type / @p version / @p body
                 * bytes and looks up the matching @ref AncFormat via
                 * @c AncFormat::fromHdmiInfoFrameType(type).
                 */
                static HdmiInfoFrame buildRaw(uint8_t type, uint8_t version, Buffer body);

                /** @brief Default-constructs an invalid @c HdmiInfoFrame. */
                HdmiInfoFrame() = default;

                /** @brief Returns the InfoFrame Type byte. */
                uint8_t type() const;

                /** @brief Returns the InfoFrame Version byte. */
                uint8_t version() const;

                /**
                 * @brief Returns the Length field (count of payload
                 *        bytes following the 4-byte header).
                 */
                uint8_t length() const;

                /**
                 * @brief Returns the body bytes (the payload that
                 *        follows the 4-byte header).
                 *
                 * Constructed by copying the matching span out of the
                 * underlying @ref AncPacket::data; the result is a
                 * fresh @ref Buffer with its own backing memory so it
                 * survives the wrapped packet going out of scope.
                 */
                Buffer body() const;

                /** @brief Returns the stored Checksum byte. */
                uint8_t checksum() const;

                /**
                 * @brief Returns @c true when the mod-256 sum of every
                 *        byte (header + body) is zero — the
                 *        CEA-861-G §6.1 validity check.
                 */
                bool checksumValid() const;

                /** @brief Returns the underlying generic @ref AncPacket. */
                const AncPacket &packet() const { return _pkt; }

                /** @brief Implicit conversion to @c const @c AncPacket&. */
                operator const AncPacket &() const { return _pkt; }

                /**
                 * @brief Returns @c true when the underlying packet's
                 *        transport is @c HdmiInfoFrame and its data
                 *        buffer holds at least the 4-byte header.
                 */
                bool isValid() const;

        private:
                explicit HdmiInfoFrame(const AncPacket &pkt) : _pkt(pkt) {}
                AncPacket _pkt;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
