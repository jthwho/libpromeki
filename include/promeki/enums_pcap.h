/**
 * @file      enums_pcap.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Packet-capture container enums: link-layer type (a subset of the
 * libpcap LINKTYPE_* registry), on-disk container format, and byte
 * order.  These are the Variant-friendly TypedEnum wrappers surfaced
 * by @ref PcapReader and the @c promeki-pcap tooling.  Numeric values
 * for @ref PcapLinkType match the on-disk / IANA "PCAP LINKTYPE"
 * assignments so a captured value compares directly against the file.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief Link-layer header type of a captured frame (libpcap LINKTYPE).
 *
 * Numeric values match the on-disk 16/32-bit link-type field used by
 * both classic pcap (global header @c network field) and pcapng
 * (Interface Description Block @c LinkType) so a value read from the
 * file compares directly against these constants.  Only the link
 * types the demux layer understands have named entries; any other
 * numeric value is still a valid @c Enum instance and round-trips as
 * @c "PcapLinkType::<int>".
 *
 *  - @c Null      (0)   — BSD loopback; 4-byte host-order AF_ family
 *                         header precedes the L3 packet.
 *  - @c Ethernet  (1)   — IEEE 802.3 / Ethernet II (libpcap EN10MB).
 *  - @c Raw       (101) — Raw IP; the frame begins at the IPv4/IPv6
 *                         header (version nibble disambiguates).
 *  - @c Loop      (108) — OpenBSD loopback; like @c Null but the AF_
 *                         family word is always big-endian.
 *  - @c LinuxSll  (113) — Linux "cooked" capture v1 (the @c any
 *                         pseudo-interface); 16-byte SLL header.
 *  - @c Ipv4      (228) — Raw IPv4 (no link or family header).
 *  - @c Ipv6      (229) — Raw IPv6 (no link or family header).
 *  - @c LinuxSll2 (276) — Linux "cooked" capture v2; 20-byte SLL2
 *                         header carrying the interface index.
 */
class PcapLinkType : public TypedEnum<PcapLinkType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("PcapLinkType", "PCAP Link Type", 1,
                                                   {"Null",      0,   "BSD Loopback"},
                                                   {"Ethernet",  1,   "Ethernet II / 802.3"},
                                                   {"Raw",       101, "Raw IP"},
                                                   {"Loop",      108, "OpenBSD Loopback"},
                                                   {"LinuxSll",  113, "Linux Cooked v1 (SLL)"},
                                                   {"Ipv4",      228, "Raw IPv4"},
                                                   {"Ipv6",      229, "Raw IPv6"},
                                                   {"LinuxSll2", 276, "Linux Cooked v2 (SLL2)"}); // default: Ethernet

                using TypedEnum<PcapLinkType>::TypedEnum;

                static const PcapLinkType Null;
                static const PcapLinkType Ethernet;
                static const PcapLinkType Raw;
                static const PcapLinkType Loop;
                static const PcapLinkType LinuxSll;
                static const PcapLinkType Ipv4;
                static const PcapLinkType Ipv6;
                static const PcapLinkType LinuxSll2;
};

inline const PcapLinkType PcapLinkType::Null{0};
inline const PcapLinkType PcapLinkType::Ethernet{1};
inline const PcapLinkType PcapLinkType::Raw{101};
inline const PcapLinkType PcapLinkType::Loop{108};
inline const PcapLinkType PcapLinkType::LinuxSll{113};
inline const PcapLinkType PcapLinkType::Ipv4{228};
inline const PcapLinkType PcapLinkType::Ipv6{229};
inline const PcapLinkType PcapLinkType::LinuxSll2{276};

/**
 * @brief On-disk packet-capture container format.
 *
 *  - @c Unknown      (0) — Not yet determined / unrecognised magic.
 *  - @c ClassicPcap  (1) — libpcap "savefile" format (RFC-less; the
 *                          24-byte global header + per-record headers).
 *  - @c Pcapng       (2) — pcapng block-structured format
 *                          (draft-tuexen-opsawg-pcapng).
 */
class PcapFileFormat : public TypedEnum<PcapFileFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("PcapFileFormat", "PCAP File Format", 0,
                                                   {"Unknown",     0, "Unknown"},
                                                   {"ClassicPcap", 1, "Classic pcap"},
                                                   {"Pcapng",      2, "pcapng"}); // default: Unknown

                using TypedEnum<PcapFileFormat>::TypedEnum;

                static const PcapFileFormat Unknown;
                static const PcapFileFormat ClassicPcap;
                static const PcapFileFormat Pcapng;
};

inline const PcapFileFormat PcapFileFormat::Unknown{0};
inline const PcapFileFormat PcapFileFormat::ClassicPcap{1};
inline const PcapFileFormat PcapFileFormat::Pcapng{2};

/**
 * @brief Byte order in which a capture file's length and timestamp
 *        fields are encoded.
 *
 * Classic pcap declares this implicitly via its magic number; pcapng
 * declares it per-section via the Section Header Block byte-order
 * magic.  Either way the value is reported so tooling can surface it.
 *
 *  - @c Unknown      (0) — Not yet determined.
 *  - @c LittleEndian (1) — Multi-byte fields are little-endian.
 *  - @c BigEndian    (2) — Multi-byte fields are big-endian.
 */
class PcapByteOrder : public TypedEnum<PcapByteOrder> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("PcapByteOrder", "PCAP Byte Order", 0,
                                                   {"Unknown",      0, "Unknown"},
                                                   {"LittleEndian", 1, "Little Endian"},
                                                   {"BigEndian",    2, "Big Endian"}); // default: Unknown

                using TypedEnum<PcapByteOrder>::TypedEnum;

                static const PcapByteOrder Unknown;
                static const PcapByteOrder LittleEndian;
                static const PcapByteOrder BigEndian;
};

inline const PcapByteOrder PcapByteOrder::Unknown{0};
inline const PcapByteOrder PcapByteOrder::LittleEndian{1};
inline const PcapByteOrder PcapByteOrder::BigEndian{2};

/**
 * @brief Essence kind of a routed RTP flow, as labelled from SDP (or
 *        left @c Unknown for an auto-discovered flow with no SDP).
 *
 *  - @c Unknown (0) — Not classified (no SDP, or an unrecognised media).
 *  - @c Video   (1) — @c m=video that is not ST 291 ANC (e.g. ST 2110-20
 *                     raw, JPEG XS).
 *  - @c Audio   (2) — @c m=audio (ST 2110-30 / -31 PCM / AES3).
 *  - @c Anc     (3) — ST 2110-40 ancillary data (@c rtpmap @c smpte291).
 *  - @c Data    (4) — @c m=application or other non-AV data essence.
 */
class PcapFlowKind : public TypedEnum<PcapFlowKind> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("PcapFlowKind", "PCAP Flow Kind", 0,
                                                   {"Unknown", 0, "Unknown"},
                                                   {"Video",   1, "Video"},
                                                   {"Audio",   2, "Audio"},
                                                   {"Anc",     3, "Ancillary Data"},
                                                   {"Data",    4, "Data"}); // default: Unknown

                using TypedEnum<PcapFlowKind>::TypedEnum;

                static const PcapFlowKind Unknown;
                static const PcapFlowKind Video;
                static const PcapFlowKind Audio;
                static const PcapFlowKind Anc;
                static const PcapFlowKind Data;
};

inline const PcapFlowKind PcapFlowKind::Unknown{0};
inline const PcapFlowKind PcapFlowKind::Video{1};
inline const PcapFlowKind PcapFlowKind::Audio{2};
inline const PcapFlowKind PcapFlowKind::Anc{3};
inline const PcapFlowKind PcapFlowKind::Data{4};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
