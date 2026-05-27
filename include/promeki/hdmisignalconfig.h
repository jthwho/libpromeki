/**
 * @file      hdmisignalconfig.h
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
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/videoportref.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Pairs an HDMI port with an optional HDMI spec-version hint.
 * @ingroup util
 *
 * HDMI carries a single payload over one physical connector, so unlike
 * @c SdiSignalConfig there is no cable-count dimension.  The
 * @ref versionHint tells the backend which CTA / HDMI Forum feature
 * subset to advertise (HDR static / dynamic metadata, ALLM, VRR,
 * eARC, FRL vs. TMDS, …) when the device's EDID does not pin it
 * down on its own.
 *
 * @par Storage and copy semantics
 *
 * Internally-CoW value-type handle (the post-2026-05-07 convention:
 * no @c ::Ptr alias, no @c PROMEKI_SHARED_FINAL on the outer class).
 * Copying bumps an internal refcount; @ref setPort and
 * @ref setVersionHint detach via copy-on-write.
 *
 * @par String form
 *
 * @ref toString emits @c "version:port" — the lower-cased registered
 * @ref HdmiSpecVersion name, a colon, and the
 * @ref VideoPortRef::toString lower-case form.  Examples:
 * @c "auto:hdmi1", @c "hdmi21:hdmi2", @c "auto:auto" (the default).
 * This shape mirrors @ref SdiSignalConfig: a lowercased descriptor
 * followed by a colon and the port.  @ref fromString accepts the
 * same shape (case-insensitive on the version segment) and rejects
 * malformed input with @c Error::InvalidArgument.
 *
 * @note Not related to @ref HdmiInfoFrame despite the shared @c Hdmi
 *       prefix — this class is a @em carrier-level @em descriptor
 *       (port + spec-version hint) consumed by backends at open time,
 *       while @ref HdmiInfoFrame is a @em typed @em packet @em helper
 *       for the CEA-861 InfoFrame wire format that rides through ANC.
 *
 * @see SdiSignalConfig, VideoPortRef, HdmiSpecVersion, HdmiInfoFrame
 */
class HdmiSignalConfig {
        public:
                PROMEKI_DATATYPE(HdmiSignalConfig, DataTypeHdmiSignalConfig, 1)

                /** @brief Default-constructs an invalid configuration. */
                HdmiSignalConfig();

                /**
                 * @brief Constructs a configuration bound to one port.
                 *
                 * @param port         The HDMI connector reference.
                 * @param versionHint  Spec-version advertisement hint;
                 *                     defaults to @c Auto so the backend
                 *                     can fall back to EDID negotiation.
                 */
                HdmiSignalConfig(VideoPortRef port, HdmiSpecVersion versionHint = HdmiSpecVersion::Auto);

                /**
                 * @brief Returns @c true when the held port is valid.
                 *
                 * The @ref versionHint is treated as a hint only — it
                 * does not affect validity.  A default-constructed
                 * configuration (default-constructed port, Auto hint)
                 * is invalid.
                 */
                bool isValid() const;

                /** @brief Returns the HDMI port reference. */
                VideoPortRef port() const;

                /** @brief Returns the spec-version hint. */
                HdmiSpecVersion versionHint() const;

                /** @brief Replaces the HDMI port reference (CoW). */
                void setPort(VideoPortRef port);

                /** @brief Replaces the spec-version hint (CoW). */
                void setVersionHint(HdmiSpecVersion versionHint);

                /**
                 * @brief Returns @c "version:port" in lower case.
                 *
                 * Examples: @c "auto:hdmi1", @c "hdmi21:hdmi2",
                 * @c "auto:auto" for the fully-default config.
                 */
                String toString() const;

                /**
                 * @brief Parses the string form produced by @ref toString.
                 *
                 * The version and port segments are split on @c ':'.
                 * Each segment is parsed case-insensitively against
                 * the registered @ref HdmiSpecVersion names /
                 * @ref VideoPortRef::fromString.  Returns
                 * @c Error::InvalidArgument when the @c ':' separator
                 * is missing or either segment fails to parse.
                 */
                static Result<HdmiSignalConfig> fromString(const String &s);

                /** @brief Field-wise equality. */
                bool operator==(const HdmiSignalConfig &other) const;

                /** @brief Inequality. */
                bool operator!=(const HdmiSignalConfig &other) const { return !(*this == other); }

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<HdmiSignalConfig> readFromStream(DataStream &s);

                /**
                 * @brief Private @c Impl struct holding the
                 *        configuration's state.
                 */
                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                VideoPortRef    port;
                                HdmiSpecVersion versionHint = HdmiSpecVersion::Auto;
                };

        private:
                SharedPtr<Impl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
