/**
 * @file      videoportref.h
 * @copyright Howard Logic. All rights reserved.
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

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Identifies one physical connector on a video device.
 * @ingroup util
 *
 * Small value type that names a single connector by its
 * @ref VideoConnectorKind plus a 1-based connector index ("the second
 * SDI input", "the third HDMI output").  The reference is scoped to
 * whatever device or MediaIO holds it — connector identity is relative
 * to the bound device, not globally unique.
 *
 * @c VideoPortRef is a building block for the higher-level signal
 * descriptors (@c SdiSignalConfig, @c HdmiSignalConfig,
 * @c VideoReferenceConfig) that capture "which connector(s), carrying
 * which link standard, locked to which reference."  It carries no
 * payload-shape information itself — that's @c VideoFormat /
 * @c ImageDesc / @c AudioDesc / @c AncDesc territory.
 *
 * @par Storage and copy semantics
 *
 * @c VideoPortRef is an internally-CoW value-type handle (the
 * post-2026-05-07 convention: no @c ::Ptr alias, no
 * @c PROMEKI_SHARED_FINAL on the outer class).  Copying a
 * @c VideoPortRef bumps an internal refcount; mutators
 * (@ref setKind, @ref setIndex) detach via copy-on-write when the
 * refcount is greater than one.  The handle is one pointer wide and
 * is cheap to pass through pipelines or store inside @c MediaConfig
 * value entries.
 *
 * @par String form
 *
 * @ref toString emits a lower-case, no-separator form pairing the
 * connector kind with the 1-based index — @c "sdi1", @c "hdmi2",
 * @c "sfp4".  The @c Auto kind always serializes as @c "auto"
 * (the index is ignored on the wire because @c Auto by definition
 * targets no specific connector).  @ref fromString accepts the same
 * shape (case-insensitive on the prefix); empty / unrecognised input
 * returns an @c Error::InvalidArgument result.
 */
class VideoPortRef {
        public:
                PROMEKI_DATATYPE(VideoPortRef, DataTypeVideoPortRef, 1)

                /**
                 * @brief Default-constructs an invalid reference.
                 *
                 * @ref isValid returns @c false until a connector kind
                 * other than @c Auto and a positive index are set.
                 */
                VideoPortRef();

                /**
                 * @brief Constructs a reference to one physical connector.
                 *
                 * @param kind   The connector family.
                 * @param index  1-based connector index on the device
                 *               (the first SDI input on a card is
                 *               @c index 1, not @c 0).
                 */
                VideoPortRef(VideoConnectorKind kind, int index);

                /**
                 * @brief Returns @c true when both @ref kind and
                 *        @ref index name a concrete connector.
                 *
                 * The reference is valid when @ref kind is not
                 * @c Auto and @ref index is @c >= 1.
                 */
                bool isValid() const;

                /** @brief Returns the connector family. */
                VideoConnectorKind kind() const;

                /** @brief Returns the 1-based connector index. */
                int index() const;

                /** @brief Replaces the connector family (CoW). */
                void setKind(VideoConnectorKind kind);

                /** @brief Replaces the 1-based connector index (CoW). */
                void setIndex(int index);

                /**
                 * @brief Returns the lower-case @c "kind+index" string.
                 *
                 * @c VideoConnectorKind::Auto always returns @c "auto"
                 * regardless of @ref index.  Otherwise the connector
                 * family name is downcased (@c "sdi", @c "hdmi",
                 * @c "displayport", @c "composite", @c "component",
                 * @c "svideo", @c "sfp") and concatenated with the
                 * decimal index — e.g. @c "sdi1", @c "hdmi3",
                 * @c "displayport2".
                 */
                String toString() const;

                /**
                 * @brief Parses the string form produced by @ref toString.
                 *
                 * The kind prefix is matched case-insensitively; the
                 * trailing decimal index must be @c >= 1 for any kind
                 * other than @c Auto.  Returns
                 * @c Error::InvalidArgument when the input is empty,
                 * the kind prefix is unrecognised, or the index is
                 * missing / non-positive.
                 */
                static Result<VideoPortRef> fromString(const String &s);

                /** @brief Field-wise equality. */
                bool operator==(const VideoPortRef &other) const;

                /** @brief Inequality. */
                bool operator!=(const VideoPortRef &other) const { return !(*this == other); }

                /**
                 * @brief Ordering operator suitable for use as a key in
                 *        @c Map / @c Set.
                 *
                 * Orders first by @ref kind (numerically), then by
                 * @ref index.  Two references with equal kind and index
                 * compare equivalent (@c "!(a < b) && !(b < a)").
                 */
                bool operator<(const VideoPortRef &other) const;

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 *
                 * Wire body: the connector kind written through its
                 * @ref Enum slicing operator, followed by the index as
                 * @c int32_t.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<VideoPortRef> readFromStream(DataStream &s);

                /**
                 * @brief Private @c Impl struct holding the reference's
                 *        state.
                 *
                 * Marked @c PROMEKI_SHARED_FINAL so @c SharedPtr<Impl>
                 * can refcount it natively with CoW semantics.  Exposed
                 * publicly only so the in-namespace stream operators
                 * can poke at fields without a friend declaration;
                 * application code should not depend on the @c Impl
                 * layout.
                 */
                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                VideoConnectorKind kind  = VideoConnectorKind::Auto;
                                int                index = 0;
                };

        private:
                SharedPtr<Impl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
