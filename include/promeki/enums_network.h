/**
 * @file      enums_network.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Network interface, EUI-64, and V4L2 control enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief Coarse category of a @ref NetworkInterface.
 *
 * Backends populate this from OS metadata (Linux sysfs, BSD/macOS
 * @c if_data.ifi_type, Windows @c IfType, …) so applications can
 * filter without parsing per-platform identifiers.
 *
 * - @c Unknown      — Backend could not classify the interface.
 * - @c Ethernet     — Wired Ethernet (IEEE 802.3).
 * - @c Wifi         — Wireless 802.11.
 * - @c Loopback     — The OS loopback (@c lo, @c lo0, etc.).
 * - @c Tunnel       — Generic tunnel (tun, ip6tnl, sit, gre, …).
 * - @c Bridge       — Software bridge over other interfaces.
 * - @c Vlan         — 802.1Q VLAN trunk member.
 * - @c Virtual      — Bonding/teaming aggregator or other software-
 *                     synthesized interface that doesn't fit a more
 *                     specific category.
 * - @c Cellular     — Mobile broadband (LTE, 5G, …).
 * - @c PointToPoint — Generic point-to-point link (PPP, etc.).
 */
class NetworkInterfaceKind : public TypedEnum<NetworkInterfaceKind> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("NetworkInterfaceKind", "Network Interface Kind", 0,
                                                   {"Unknown", 0, "Unknown"},
                                                   {"Ethernet", 1, "Ethernet (IEEE 802.3)"},
                                                   {"Wifi", 2, "Wi-Fi (802.11)"}, {"Loopback", 3, "Loopback"},
                                                   {"Tunnel", 4, "Tunnel"}, {"Bridge", 5, "Software Bridge"},
                                                   {"Vlan", 6, "VLAN (802.1Q)"}, {"Virtual", 7, "Virtual"},
                                                   {"Cellular", 8, "Cellular (LTE / 5G)"},
                                                   {"PointToPoint", 9, "Point-to-Point"}); // default: Unknown

                using TypedEnum<NetworkInterfaceKind>::TypedEnum;

                static const NetworkInterfaceKind Unknown;
                static const NetworkInterfaceKind Ethernet;
                static const NetworkInterfaceKind Wifi;
                static const NetworkInterfaceKind Loopback;
                static const NetworkInterfaceKind Tunnel;
                static const NetworkInterfaceKind Bridge;
                static const NetworkInterfaceKind Vlan;
                static const NetworkInterfaceKind Virtual;
                static const NetworkInterfaceKind Cellular;
                static const NetworkInterfaceKind PointToPoint;
};

inline const NetworkInterfaceKind NetworkInterfaceKind::Unknown{0};
inline const NetworkInterfaceKind NetworkInterfaceKind::Ethernet{1};
inline const NetworkInterfaceKind NetworkInterfaceKind::Wifi{2};
inline const NetworkInterfaceKind NetworkInterfaceKind::Loopback{3};
inline const NetworkInterfaceKind NetworkInterfaceKind::Tunnel{4};
inline const NetworkInterfaceKind NetworkInterfaceKind::Bridge{5};
inline const NetworkInterfaceKind NetworkInterfaceKind::Vlan{6};
inline const NetworkInterfaceKind NetworkInterfaceKind::Virtual{7};
inline const NetworkInterfaceKind NetworkInterfaceKind::Cellular{8};
inline const NetworkInterfaceKind NetworkInterfaceKind::PointToPoint{9};

/**
 * @brief Well-known Enum type for EUI-64 string formats.
 *
 * Selects the notation used by EUI64::toString(EUI64Format).
 *
 * - @c OctetHyphen  — `"aa-bb-cc-dd-ee-ff-00-11"` (PTP SDP convention).
 * - @c OctetColon   — `"aa:bb:cc:dd:ee:ff:00:11"`.
 * - @c IPv6         — `"aabb:ccdd:eeff:0011"` (four colon-separated
 *                     16-bit groups, used in IPv6 interface identifiers).
 */
class EUI64Format : public TypedEnum<EUI64Format> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("EUI64Format", "EUI-64 Format", 0,
                                                   {"OctetHyphen", 0, "Hyphen-Separated Octets"},
                                                   {"OctetColon", 1, "Colon-Separated Octets"},
                                                   {"IPv6", 2, "IPv6 Identifier Groups"}); // default: OctetHyphen

                using TypedEnum<EUI64Format>::TypedEnum;

                static const EUI64Format OctetHyphen;
                static const EUI64Format OctetColon;
                static const EUI64Format IPv6;
};

inline const EUI64Format EUI64Format::OctetHyphen{0};
inline const EUI64Format EUI64Format::OctetColon{1};
inline const EUI64Format EUI64Format::IPv6{2};

/**
 * @brief Well-known Enum type for V4L2 power line frequency filter.
 *
 * Maps to @c V4L2_CID_POWER_LINE_FREQUENCY menu items.
 */
class V4l2PowerLineMode : public TypedEnum<V4l2PowerLineMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("V4l2PowerLineMode", "V4L2 Power Line Frequency", 3,
                                                   {"Disabled", 0, "Disabled"}, {"50Hz", 1, "50 Hz"},
                                                   {"60Hz", 2, "60 Hz"}, {"Auto", 3, "Automatic"}); // default: Auto

                using TypedEnum<V4l2PowerLineMode>::TypedEnum;

                static const V4l2PowerLineMode Disabled;
                static const V4l2PowerLineMode Hz50;
                static const V4l2PowerLineMode Hz60;
                static const V4l2PowerLineMode Auto;
};

inline const V4l2PowerLineMode V4l2PowerLineMode::Disabled{0};
inline const V4l2PowerLineMode V4l2PowerLineMode::Hz50{1};
inline const V4l2PowerLineMode V4l2PowerLineMode::Hz60{2};
inline const V4l2PowerLineMode V4l2PowerLineMode::Auto{3};

/**
 * @brief Well-known Enum type for V4L2 auto exposure mode.
 *
 * Maps to @c V4L2_CID_EXPOSURE_AUTO menu items.
 */
class V4l2ExposureMode : public TypedEnum<V4l2ExposureMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("V4l2ExposureMode", "V4L2 Exposure Mode", 3,
                                                   {"Auto", 0, "Automatic"}, {"Manual", 1, "Manual"},
                                                   {"ShutterPriority", 2, "Shutter Priority"},
                                                   {"AperturePriority", 3, "Aperture Priority"}); // default: AperturePriority

                using TypedEnum<V4l2ExposureMode>::TypedEnum;

                static const V4l2ExposureMode Auto;
                static const V4l2ExposureMode Manual;
                static const V4l2ExposureMode ShutterPriority;
                static const V4l2ExposureMode AperturePriority;
};

inline const V4l2ExposureMode V4l2ExposureMode::Auto{0};
inline const V4l2ExposureMode V4l2ExposureMode::Manual{1};
inline const V4l2ExposureMode V4l2ExposureMode::ShutterPriority{2};
inline const V4l2ExposureMode V4l2ExposureMode::AperturePriority{3};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
