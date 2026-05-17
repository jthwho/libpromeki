/**
 * @file      videoreferenceconfig.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/datatype.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/videoportref.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Device-wide reference clock configuration.
 * @ingroup util
 *
 * Tells the backend which clock the device should lock its outputs
 * to: a free-running internal oscillator, a dedicated GENLOCK input,
 * a generic external reference, or one of the device's own SDI / HDMI
 * input connectors (the "lock to input" mode that hardware switchers
 * use to relock outputs to an incoming program feed).  The
 * @ref family field pins down which 148.5 MHz lattice the
 * generated rate sits on; the actual frame rate within that lattice
 * is supplied by @ref MediaConfig::VideoFormat / @ref FrameRate.
 *
 * @par signalPort semantics
 *
 * @ref signalPort is only meaningful when @ref source is
 * @c FromSignal — it names the connector whose signal supplies the
 * lock.  For every other source the field is unused (the
 * configuration is valid regardless of what it holds).
 *
 * @par Storage and copy semantics
 *
 * Internally-CoW value-type handle (the post-2026-05-07 convention:
 * no @c ::Ptr alias, no @c PROMEKI_SHARED_FINAL on the outer class).
 * Copying bumps an internal refcount; @ref setSource,
 * @ref setFamily, and @ref setSignalPort detach via copy-on-write.
 *
 * @par String form
 *
 * @ref toString emits @c "<source>:<family>" for every source other
 * than @c FromSignal, which inserts the port reference between:
 * @c "<source>:<port>:<family>".  All segments are lower-case.
 * Examples:
 *
 *  - @c "freerun:auto"
 *  - @c "genlock:integer"
 *  - @c "fromsignal:sdi1:fractional"
 *
 * This shape matches @ref SdiSignalConfig / @ref HdmiSignalConfig:
 * a lowercased descriptor followed by a colon and the remaining
 * fields.  @ref fromString accepts the same shape
 * case-insensitively and rejects malformed input with
 * @c Error::InvalidArgument.
 */
class VideoReferenceConfig {
        public:
                PROMEKI_DATATYPE(VideoReferenceConfig, DataTypeVideoReferenceConfig, 1)

                /**
                 * @brief Default-constructs to free-run / auto-family.
                 *
                 * The default is a valid configuration — a device
                 * with no upstream timing simply runs off its local
                 * oscillator.
                 */
                VideoReferenceConfig();

                /**
                 * @brief Constructs with the given source + family.
                 *
                 * @param source  Reference clock origin.
                 * @param family  Rate lattice; defaults to
                 *                @c Auto so the backend can pick
                 *                from the negotiated @ref VideoFormat.
                 */
                VideoReferenceConfig(VideoReferenceSource     source,
                                     VideoReferenceRateFamily family = VideoReferenceRateFamily::Auto);

                /**
                 * @brief Returns @c true when the configuration is
                 *        usable as-is.
                 *
                 * Valid for every source except @c FromSignal, which
                 * additionally requires a valid @ref signalPort.
                 */
                bool isValid() const;

                /** @brief Returns the reference clock source. */
                VideoReferenceSource source() const;

                /** @brief Returns the rate family. */
                VideoReferenceRateFamily family() const;

                /**
                 * @brief Returns the input port the lock is sourced from.
                 *
                 * Only meaningful when @ref source is @c FromSignal.
                 * For every other source the field is unused and the
                 * returned reference is typically default-constructed.
                 */
                VideoPortRef signalPort() const;

                /** @brief Replaces the reference clock source (CoW). */
                void setSource(VideoReferenceSource source);

                /** @brief Replaces the rate family (CoW). */
                void setFamily(VideoReferenceRateFamily family);

                /** @brief Replaces the lock-source port (CoW). */
                void setSignalPort(VideoPortRef signalPort);

                /**
                 * @brief Returns the lower-case
                 *        @c "<source>:<family>" /
                 *        @c "fromsignal:<port>:<family>" string.
                 */
                String toString() const;

                /**
                 * @brief Parses the string form produced by @ref toString.
                 *
                 * Splits on @c ':'.  The first segment selects the
                 * source; when the source is @c FromSignal the next
                 * segment is parsed as a @ref VideoPortRef and the
                 * final segment is the family.  For every other
                 * source the form is @c "<source>:<family>".  Returns
                 * @c Error::InvalidArgument on a malformed input.
                 */
                static Result<VideoReferenceConfig> fromString(const String &s);

                /** @brief Field-wise equality. */
                bool operator==(const VideoReferenceConfig &other) const;

                /** @brief Inequality. */
                bool operator!=(const VideoReferenceConfig &other) const { return !(*this == other); }

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<VideoReferenceConfig> readFromStream(DataStream &s);

                /**
                 * @brief Private @c Impl struct holding the
                 *        configuration's state.
                 */
                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                VideoReferenceSource     source = VideoReferenceSource::FreeRun;
                                VideoReferenceRateFamily family = VideoReferenceRateFamily::Auto;
                                VideoPortRef             signalPort;
                };

        private:
                SharedPtr<Impl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
