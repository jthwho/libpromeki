/**
 * @file      sdisignalconfig.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/datatype.h>
#include <promeki/enums_video.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/sdistandards.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/videoportref.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

// ============================================================================
// SdiSignalConfig
// ============================================================================

/**
 * @brief Pairs an SDI link standard with the physical connector(s)
 *        that carry it.
 * @ingroup util
 *
 * Names "which SMPTE wire format on which port(s)" independent of
 * the content (which @ref VideoFormat / @ref ImageDesc / @ref AudioDesc
 * already cover).  Together with @ref VideoReferenceConfig this is
 * the minimum a hardware MediaIO backend needs to open an SDI port.
 *
 * @par Cable count
 *
 * Single-link standards carry one @ref VideoPortRef; dual-link two;
 * quad-link four (Square Division or 2-Sample Interleave).
 * @ref validate cross-checks the @c standard's @ref sdiCableCount against
 * @ref ports — a 3G-SDI standard paired with three ports rejects.
 * @ref SdiLinkStandard::Auto is treated as "unspecified" — any cable
 * count is accepted, including the empty list.
 *
 * @par Storage and copy semantics
 *
 * Internally-CoW value-type handle (the post-2026-05-07 convention:
 * no @c ::Ptr alias, no @c PROMEKI_SHARED_FINAL on the outer class).
 * Copying bumps an internal refcount; @ref setStandard, @ref setPorts,
 * and @ref appendPort detach via copy-on-write.
 *
 * @par String form
 *
 * @ref toString emits @c "standard:port1+port2+...".  The standard
 * is the lowercased registered enum name (e.g. @c QL_3G_2SI becomes
 * @c "ql_3g_2si").  Each port is the @ref VideoPortRef::toString
 * form.  An empty port list omits the colon — a configuration with
 * @c Auto standard and no ports serializes as @c "auto".  This is
 * the same shape used by @ref HdmiSignalConfig: a lowercased
 * descriptor followed by a colon and the port list.
 */
class SdiSignalConfig {
        public:
                PROMEKI_DATATYPE(SdiSignalConfig, DataTypeSdiSignalConfig, 1)

                /** @brief Plain-value list of @c VideoPortRef. */
                using PortList = ::promeki::List<VideoPortRef>;

                /**
                 * @brief Default-constructs an empty configuration
                 *        (Auto standard, no ports).
                 */
                SdiSignalConfig();

                /**
                 * @brief Constructs a configuration with the given
                 *        standard and ports.
                 */
                SdiSignalConfig(SdiLinkStandard standard, PortList ports);

                /**
                 * @brief Returns @c true when @ref validate would
                 *        return @c Error::Ok (i.e. the cable count
                 *        matches the standard, or the standard is
                 *        @c Auto).
                 */
                bool isValid() const;

                /** @brief Returns the SDI link standard. */
                SdiLinkStandard standard() const;

                /** @brief Returns the list of ports carrying the signal. */
                const PortList &ports() const;

                /** @brief Returns the number of cables in @ref ports. */
                int cableCount() const;

                /** @brief Replaces the SDI link standard (CoW). */
                void setStandard(SdiLinkStandard standard);

                /** @brief Replaces the port list (CoW). */
                void setPorts(PortList ports);

                /** @brief Appends one port to the list (CoW). */
                void appendPort(VideoPortRef port);

                /**
                 * @brief Builds a single-link configuration.
                 *
                 * @param standard  Single-link SMPTE standard.  Caller
                 *                  is responsible for choosing one
                 *                  whose @ref sdiCableCount returns @c 1
                 *                  (or @c Auto); @ref validate will
                 *                  reject mismatches.
                 * @param port      The carrier port.
                 */
                static SdiSignalConfig singleLink(SdiLinkStandard standard, VideoPortRef port);

                /**
                 * @brief Builds a dual-link configuration.
                 */
                static SdiSignalConfig dualLink(SdiLinkStandard standard, VideoPortRef a, VideoPortRef b);

                /**
                 * @brief Builds a quad-link configuration.
                 */
                static SdiSignalConfig quadLink(SdiLinkStandard standard, VideoPortRef a, VideoPortRef b,
                                                VideoPortRef c, VideoPortRef d);

                /**
                 * @brief Validates that the cable count matches the
                 *        standard.
                 *
                 * Returns @c Error::Ok when:
                 *  - the standard is @c Auto (any cable count, including zero, accepted), or
                 *  - the number of ports equals @ref sdiCableCount for the standard.
                 *
                 * Returns @c Error::InvalidArgument otherwise.  The
                 * error name is descriptive enough for log output.
                 */
                Error validate() const;

                /**
                 * @brief Returns @c "standard:port1+port2+...".
                 *
                 * The standard is the lowercased enum name with @c _
                 * separators stripped.  When the port list is empty
                 * the trailing colon is omitted.
                 */
                String toString() const;

                /**
                 * @brief Parses the string form produced by @ref toString.
                 *
                 * The standard portion is matched case-insensitively
                 * after stripping @c _ from registered enum names.
                 * Each port between @c + separators is parsed via
                 * @ref VideoPortRef::fromString.  Returns
                 * @c Error::InvalidArgument when the standard prefix
                 * is unrecognised or any port fails to parse.
                 */
                static Result<SdiSignalConfig> fromString(const String &s);

                /** @brief Field-wise equality. */
                bool operator==(const SdiSignalConfig &other) const;

                /** @brief Inequality. */
                bool operator!=(const SdiSignalConfig &other) const { return !(*this == other); }

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<SdiSignalConfig> readFromStream(DataStream &s);

                /**
                 * @brief Private @c Impl struct holding the
                 *        configuration's state.
                 */
                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                SdiLinkStandard standard = SdiLinkStandard::Auto;
                                PortList        ports;
                };

        private:
                SharedPtr<Impl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
