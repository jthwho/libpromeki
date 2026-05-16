/**
 * @file      ancmeta.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/metadata.h>
#include <promeki/variantspec.h>
#include <promeki/string.h>
#include <promeki/uuid.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Well-known @ref Metadata key constants for the
 *        per-transport sidecar that lives on an @ref AncPacket.
 * @ingroup proav
 *
 * Each @ref AncPacket carries a @ref Metadata @c meta() container that
 * holds transport-specific framing fields the wire bytes do not
 * directly encode (the VANC line a captured ST 291 packet was
 * encountered on, the AMF0 script-tag name on an RTMP packet, the
 * MPEG-TS PID a private section was carried in, …).  The keys for
 * those fields live here as a single compile-time-checked vocabulary,
 * grouped by transport so misspelled keys do not silently pass at the
 * call site.
 *
 * The wire names use the @c "AncMeta." prefix followed by a transport
 * name and the field name (e.g. @c "AncMeta.St291.Line").  The C++
 * identifiers drop the dots because identifiers cannot contain them.
 *
 * @par Read-time vs build-time
 *
 * These keys are the **read-time sidecar** for packets that were
 * captured or received from a real source: "this packet was
 * encountered on VANC line 11" / "this script tag was named
 * @c onCaptionInfo".  The companion @c AncTranslateConfig::&lt;Transport&gt;
 * key family (Phase 2) supplies the **build-time inputs** that a
 * codec consults when synthesising a packet on the same transport
 * from scratch (e.g. "when building an ST 291 packet, place it on
 * line 11").  Codecs that build packets typically copy the relevant
 * build-time config values into these @c AncMeta sidecar keys on the
 * produced packet.
 *
 * @par Example
 * @code
 * AncPacket pkt = ...;
 * uint16_t line = pkt.meta().get(AncMeta::St291::Line).get<uint16_t>();
 * pkt.metaMut().set(AncMeta::St291::FieldB, true);
 * @endcode
 */
namespace AncMeta {

/** @brief ST 291 ancillary-packet framing sidecar. */
namespace St291 {

/** @brief VANC line number the packet was captured on. */
inline const Metadata::ID Line = Metadata::declareID(
        "AncMeta.St291.Line",
        VariantSpec()
                .setType(Variant::TypeU16)
                .setDefault(uint16_t(0))
                .setDescription("ST 291 VANC line number the packet was captured on."));

/** @brief Horizontal offset within the line; 0xFFF means
 *         "no specific position" per RFC 8331 §2.2. */
inline const Metadata::ID HOffset = Metadata::declareID(
        "AncMeta.St291.HOffset",
        VariantSpec()
                .setType(Variant::TypeU16)
                .setDefault(uint16_t(0xFFF))
                .setDescription("ST 291 horizontal offset within the line (0xFFF = unspecified)."));

/** @brief F-bit: @c true when the packet was found on field 2 of an
 *         interlaced source; @c false on progressive sources or
 *         field 1. */
inline const Metadata::ID FieldB = Metadata::declareID(
        "AncMeta.St291.FieldB",
        VariantSpec()
                .setType(Variant::TypeBool)
                .setDefault(false)
                .setDescription("ST 291 F-bit: true on field 2 of an interlaced source."));

/** @brief C-bit: @c true for chrominance-data-stream packets,
 *         @c false for luminance data stream (the common case). */
inline const Metadata::ID CBit = Metadata::declareID(
        "AncMeta.St291.CBit",
        VariantSpec()
                .setType(Variant::TypeBool)
                .setDefault(false)
                .setDescription("ST 291 C-bit: true on the chrominance data stream."));

/** @brief Stream number (RFC 8331 StreamNum field).  Zero for
 *         single-link SDI; non-zero distinguishes packets on
 *         multi-link / 12G sub-streams. */
inline const Metadata::ID StreamNum = Metadata::declareID(
        "AncMeta.St291.StreamNum",
        VariantSpec()
                .setType(Variant::TypeU8)
                .setDefault(uint8_t(0))
                .setDescription("ST 291 stream number (RFC 8331 StreamNum field)."));

} // namespace St291

/** @brief HDMI InfoFrame framing sidecar. */
namespace HdmiInfoFrame {

/** @brief InfoFrame type byte (0x82 = AVI, 0x84 = Audio,
 *         0x83 = SPD, 0x87 = DRM, 0x81 = Vendor-Specific, …). */
inline const Metadata::ID Type = Metadata::declareID(
        "AncMeta.HdmiInfoFrame.Type",
        VariantSpec()
                .setType(Variant::TypeU8)
                .setDefault(uint8_t(0))
                .setDescription("HDMI InfoFrame type byte (0x82 AVI, 0x84 Audio, 0x87 DRM, 0x81 Vendor, ...)."));

/** @brief InfoFrame version byte. */
inline const Metadata::ID Version = Metadata::declareID(
        "AncMeta.HdmiInfoFrame.Version",
        VariantSpec().setType(Variant::TypeU8).setDefault(uint8_t(0)).setDescription("HDMI InfoFrame version byte."));

/** @brief InfoFrame length field (bytes of payload following the
 *         4-byte header). */
inline const Metadata::ID Length = Metadata::declareID(
        "AncMeta.HdmiInfoFrame.Length",
        VariantSpec()
                .setType(Variant::TypeU8)
                .setDefault(uint8_t(0))
                .setDescription("HDMI InfoFrame length: payload bytes after the 4-byte header."));

} // namespace HdmiInfoFrame

/** @brief RTMP AMF0 script-tag framing sidecar. */
namespace RtmpAmf {

/** @brief AMF0 script-tag name (@c onCaptionInfo, @c onCuePoint,
 *         @c onMetaData, …).  Used by sinks that need to preserve
 *         the original script-tag name on byte-exact replay. */
inline const Metadata::ID ScriptName = Metadata::declareID(
        "AncMeta.RtmpAmf.ScriptName",
        VariantSpec()
                .setType(Variant::TypeString)
                .setDefault(String())
                .setDescription("RTMP AMF0 script-tag name (onCaptionInfo, onCuePoint, onMetaData, ...)."));

} // namespace RtmpAmf

/** @brief NDI XML-metadata framing sidecar. */
namespace NdiXml {

/** @brief Top-level XML element name; lets sinks dispatch on the
 *         element without re-parsing the body. */
inline const Metadata::ID ElementName = Metadata::declareID(
        "AncMeta.NdiXml.ElementName",
        VariantSpec()
                .setType(Variant::TypeString)
                .setDefault(String())
                .setDescription("NDI XML top-level element name (e.g. ndi_caption_data)."));

} // namespace NdiXml

/** @brief MPEG-TS private-section framing sidecar. */
namespace MpegTsPrivate {

/** @brief PID the private section was carried in. */
inline const Metadata::ID Pid = Metadata::declareID(
        "AncMeta.MpegTsPrivate.Pid",
        VariantSpec()
                .setType(Variant::TypeU16)
                .setDefault(uint16_t(0))
                .setDescription("MPEG-TS PID the private section was carried in."));

/** @brief Table ID (first byte of the private section). */
inline const Metadata::ID TableId = Metadata::declareID(
        "AncMeta.MpegTsPrivate.TableId",
        VariantSpec()
                .setType(Variant::TypeU8)
                .setDefault(uint8_t(0))
                .setDescription("MPEG-TS private-section table_id byte."));

} // namespace MpegTsPrivate

/** @brief HLS / H.264 / HEVC SEI framing sidecar. */
namespace HlsSei {

/** @brief SEI payload type (typically 5 for user_data_registered). */
inline const Metadata::ID PayloadType = Metadata::declareID(
        "AncMeta.HlsSei.PayloadType",
        VariantSpec()
                .setType(Variant::TypeU8)
                .setDefault(uint8_t(0))
                .setDescription("SEI payloadType (typically 5 for user_data_registered)."));

/** @brief Unregistered-SEI UUID (only used when @c PayloadType
 *         identifies an unregistered SEI message). */
inline const Metadata::ID Uuid = Metadata::declareID(
        "AncMeta.HlsSei.Uuid",
        VariantSpec()
                .setType(Variant::TypeUUID)
                .setDefault(UUID())
                .setDescription("UUID for SEI user_data_unregistered messages."));

} // namespace HlsSei

} // namespace AncMeta

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
