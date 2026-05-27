/**
 * @file      enums_jxs.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * JPEG XS (RFC 9134 / ISO 21122) signalling enums.
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
 * @brief Well-known Enum type for RFC 9134 @c packetmode (K bit).
 *
 * RFC 9134 §4.3 packet-header K bit:
 *  - @c Codestream (K=0) — the codestream is split into MTU-sized
 *    fragments without regard to slice / header boundaries.
 *    Simplest sender; receivers must reassemble before decode.
 *    The library's default.
 *  - @c Slice (K=1) — each RTP packet carries one or more
 *    @em complete JPEG XS slices, never crossing a slice
 *    boundary.  Enables ultra-low-latency receivers to start
 *    decoding before the entire frame has arrived; requires the
 *    sender to walk the codestream's SLH markers and group
 *    slices into MTU-sized packets.
 */
class JxsPacketMode : public TypedEnum<JxsPacketMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("JxsPacketMode", "JPEG XS Packetization Mode", 0,
                                           {"Codestream", 0, "Codestream (K=0)"},
                                           {"Slice", 1, "Slice (K=1)"}); // default: Codestream

                using TypedEnum<JxsPacketMode>::TypedEnum;

                static const JxsPacketMode Codestream;
                static const JxsPacketMode Slice;
};

inline const JxsPacketMode JxsPacketMode::Codestream{0};
inline const JxsPacketMode JxsPacketMode::Slice{1};

/**
 * @brief Well-known Enum type for RFC 9134 @c transmode (T bit).
 *
 * RFC 9134 §4.3 packet-header T bit:
 *  - @c OutOfOrderAllowed (T=0) — the sender emits packets in
 *    codestream order but receivers may reorder before decode.
 *    Useful with reorder buffers that can absorb network
 *    permutations.
 *  - @c SequentialOnly (T=1) — packets MUST arrive in sequence
 *    for decode to succeed.  The default the RFC mandates when
 *    the parameter is absent from the fmtp.
 */
class JxsTransMode : public TypedEnum<JxsTransMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("JxsTransMode", "JPEG XS Transmission Mode", 1,
                                           {"OutOfOrderAllowed", 0, "Out-of-Order Allowed (T=0)"},
                                           {"SequentialOnly", 1, "Sequential Only (T=1)"}); // default: SequentialOnly

                using TypedEnum<JxsTransMode>::TypedEnum;

                static const JxsTransMode OutOfOrderAllowed;
                static const JxsTransMode SequentialOnly;
};

inline const JxsTransMode JxsTransMode::OutOfOrderAllowed{0};
inline const JxsTransMode JxsTransMode::SequentialOnly{1};

/**
 * @brief Well-known Enum type for the JPEG XS profile (ISO 21122-2).
 *
 * Each value's CamelCase identifier maps to a canonical SDP
 * @c profile= wire token after RFC 9134 §7.1's "any white space
 * Unicode character in the profile name SHALL be omitted" rule;
 * the wire mapping lives in @c imagedesc.cpp 's
 * @c jxsProfileToFmtp / @c jxsProfileFromFmtp helpers.  Example:
 * @c Main422_10 → @c "Main422.10".
 *
 * @c Unspecified (the default) suppresses @c profile= emission.
 */
class JxsProfile : public TypedEnum<JxsProfile> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("JxsProfile", "JPEG XS Profile", 0,
                                           {"Unspecified", 0, "Unspecified"},
                                           {"Light422_10", 1, "Light 4:2:2 10-bit"},
                                           {"Light444_12", 2, "Light 4:4:4 12-bit"},
                                           {"LightSubline422_10", 3, "Light-Subline 4:2:2 10-bit"},
                                           {"Main422_10", 4, "Main 4:2:2 10-bit"},
                                           {"Main444_12", 5, "Main 4:4:4 12-bit"},
                                           {"Main4444_12", 6, "Main 4:4:4:4 12-bit"},
                                           {"High444_12", 7, "High 4:4:4 12-bit"},
                                           {"High4444_12", 8, "High 4:4:4:4 12-bit"},
                                           {"Tdc422_10", 9, "TDC 4:2:2 10-bit"});

                using TypedEnum<JxsProfile>::TypedEnum;

                static const JxsProfile Unspecified;
                static const JxsProfile Light422_10;
                static const JxsProfile Light444_12;
                static const JxsProfile LightSubline422_10;
                static const JxsProfile Main422_10;
                static const JxsProfile Main444_12;
                static const JxsProfile Main4444_12;
                static const JxsProfile High444_12;
                static const JxsProfile High4444_12;
                static const JxsProfile Tdc422_10;
};

inline const JxsProfile JxsProfile::Unspecified{0};
inline const JxsProfile JxsProfile::Light422_10{1};
inline const JxsProfile JxsProfile::Light444_12{2};
inline const JxsProfile JxsProfile::LightSubline422_10{3};
inline const JxsProfile JxsProfile::Main422_10{4};
inline const JxsProfile JxsProfile::Main444_12{5};
inline const JxsProfile JxsProfile::Main4444_12{6};
inline const JxsProfile JxsProfile::High444_12{7};
inline const JxsProfile JxsProfile::High4444_12{8};
inline const JxsProfile JxsProfile::Tdc422_10{9};

/**
 * @brief Well-known Enum type for the JPEG XS level (ISO 21122-2).
 *
 * Each value's identifier maps to a canonical SDP @c level=
 * wire token (e.g. @c Lvl4k_2 → @c "4k-2") in @c imagedesc.cpp.
 * @c Unspecified suppresses @c level= emission.
 */
class JxsLevel : public TypedEnum<JxsLevel> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("JxsLevel", "JPEG XS Level", 0,
                                           {"Unspecified", 0, "Unspecified"}, {"Lvl1k_1", 1, "1k-1"},
                                           {"Lvl2k_1", 2, "2k-1"}, {"Lvl4k_1", 3, "4k-1"}, {"Lvl4k_2", 4, "4k-2"},
                                           {"Lvl4k_3", 5, "4k-3"}, {"Lvl8k_1", 6, "8k-1"}, {"Lvl8k_2", 7, "8k-2"},
                                           {"Lvl8k_3", 8, "8k-3"}, {"Lvl10k_1", 9, "10k-1"});

                using TypedEnum<JxsLevel>::TypedEnum;

                static const JxsLevel Unspecified;
                static const JxsLevel Lvl1k_1;
                static const JxsLevel Lvl2k_1;
                static const JxsLevel Lvl4k_1;
                static const JxsLevel Lvl4k_2;
                static const JxsLevel Lvl4k_3;
                static const JxsLevel Lvl8k_1;
                static const JxsLevel Lvl8k_2;
                static const JxsLevel Lvl8k_3;
                static const JxsLevel Lvl10k_1;
};

inline const JxsLevel JxsLevel::Unspecified{0};
inline const JxsLevel JxsLevel::Lvl1k_1{1};
inline const JxsLevel JxsLevel::Lvl2k_1{2};
inline const JxsLevel JxsLevel::Lvl4k_1{3};
inline const JxsLevel JxsLevel::Lvl4k_2{4};
inline const JxsLevel JxsLevel::Lvl4k_3{5};
inline const JxsLevel JxsLevel::Lvl8k_1{6};
inline const JxsLevel JxsLevel::Lvl8k_2{7};
inline const JxsLevel JxsLevel::Lvl8k_3{8};
inline const JxsLevel JxsLevel::Lvl10k_1{9};

/**
 * @brief Well-known Enum type for the JPEG XS sublevel (ISO 21122-2).
 *
 * Each value's identifier maps to a canonical SDP @c sublevel=
 * wire token (e.g. @c Sublev3bpp → @c "Sublev3bpp") in
 * @c imagedesc.cpp.  @c Unspecified suppresses @c sublevel=
 * emission.
 */
class JxsSublevel : public TypedEnum<JxsSublevel> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("JxsSublevel", "JPEG XS Sublevel", 0,
                                           {"Unspecified", 0, "Unspecified"}, {"Full", 1, "Full"},
                                           {"Sublev3bpp", 2, "Sublevel 3 bpp"}, {"Sublev6bpp", 3, "Sublevel 6 bpp"},
                                           {"Sublev9bpp", 4, "Sublevel 9 bpp"}, {"Sublev12bpp", 5, "Sublevel 12 bpp"});

                using TypedEnum<JxsSublevel>::TypedEnum;

                static const JxsSublevel Unspecified;
                static const JxsSublevel Full;
                static const JxsSublevel Sublev3bpp;
                static const JxsSublevel Sublev6bpp;
                static const JxsSublevel Sublev9bpp;
                static const JxsSublevel Sublev12bpp;
};

inline const JxsSublevel JxsSublevel::Unspecified{0};
inline const JxsSublevel JxsSublevel::Full{1};
inline const JxsSublevel JxsSublevel::Sublev3bpp{2};
inline const JxsSublevel JxsSublevel::Sublev6bpp{3};
inline const JxsSublevel JxsSublevel::Sublev9bpp{4};
inline const JxsSublevel JxsSublevel::Sublev12bpp{5};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
