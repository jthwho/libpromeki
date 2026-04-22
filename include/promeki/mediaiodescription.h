/**
 * @file      mediaiodescription.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/framecount.h>
#include <promeki/framerate.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/uuid.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Self-contained snapshot of a MediaIO's identity, role, and
 *        format landscape.
 * @ingroup proav
 *
 * @ref MediaIODescription captures everything you can know about a
 * @ref MediaIO instance without using it for I/O — the backend
 * identity, the user-visible name and UUID, which roles
 * (source / sink / transform) the backend supports, the lists of
 * @ref MediaDesc shapes it can produce or accept, navigation
 * capabilities (seekable, frame count), and any container metadata
 * available pre-open.
 *
 * The snapshot is cheap to obtain via @ref MediaIO::describe (each
 * backend chooses the cost: file backends parse the header, device
 * backends query OS APIs, generators answer from config), safe to
 * pass to UI threads, and fully serializable for IPC, config dumps,
 * and "what does this resource offer" CLI tools.
 *
 * The same snapshot is consumed by the pipeline planner — its
 * @c producibleFormats / @c acceptableFormats drive automatic
 * insertion of bridging stages (CSC, decoder, frame-rate sync,
 * etc.) when a route's source and sink are not directly
 * format-compatible.
 */
class MediaIODescription {
        PROMEKI_SHARED_FINAL(MediaIODescription)
        public:
                /** @brief Shared pointer alias. */
                using Ptr = SharedPtr<MediaIODescription>;

                /** @brief List of value snapshots. */
                using List = promeki::List<MediaIODescription>;

                /** @brief List of shared snapshot pointers. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Sentinel for an unknown frame count.
                 *
                 * Mirrors @ref MediaIO::FrameCountUnknown so callers
                 * can compare against either constant interchangeably.
                 */
                static constexpr FrameCount FrameCountUnknown  = FrameCount::unknown();

                /** @brief Sentinel for an unbounded source (live device). */
                static constexpr FrameCount FrameCountInfinite = FrameCount::infinity();

                /** @brief Constructs a default (empty / unknown) description. */
                MediaIODescription() = default;

                // ------------------------------------------------------------
                // Backend identity
                // ------------------------------------------------------------

                /** @brief Returns the registered backend name (e.g. @c "TPG"). */
                const String &backendName() const { return _backendName; }

                /** @brief Sets the registered backend name. */
                void setBackendName(const String &val) { _backendName = val; }

                /** @brief Returns the human-readable backend description. */
                const String &backendDescription() const { return _backendDescription; }

                /** @brief Sets the human-readable backend description. */
                void setBackendDescription(const String &val) { _backendDescription = val; }

                // ------------------------------------------------------------
                // Instance identity
                // ------------------------------------------------------------

                /** @brief Returns the user-assigned instance name. */
                const String &name() const { return _name; }

                /** @brief Sets the instance name. */
                void setName(const String &val) { _name = val; }

                /** @brief Returns the instance UUID. */
                const UUID &uuid() const { return _uuid; }

                /** @brief Sets the instance UUID. */
                void setUuid(const UUID &val) { _uuid = val; }

                /** @brief Returns the process-local monotonic instance ID (or -1). */
                int localId() const { return _localId; }

                /** @brief Sets the process-local instance ID. */
                void setLocalId(int val) { _localId = val; }

                // ------------------------------------------------------------
                // Role flags
                // ------------------------------------------------------------

                /** @brief True if this backend can act as a source (provides frames). */
                bool canBeSource() const { return _canBeSource; }

                /** @brief Sets the source-capable flag. */
                void setCanBeSource(bool val) { _canBeSource = val; }

                /** @brief True if this backend can act as a sink (accepts frames). */
                bool canBeSink() const { return _canBeSink; }

                /** @brief Sets the sink-capable flag. */
                void setCanBeSink(bool val) { _canBeSink = val; }

                /** @brief True if this backend can act as a transform (in + out). */
                bool canBeTransform() const { return _canBeTransform; }

                /** @brief Sets the transform-capable flag. */
                void setCanBeTransform(bool val) { _canBeTransform = val; }

                // ------------------------------------------------------------
                // Format landscape
                // ------------------------------------------------------------

                /**
                 * @brief Returns every @ref MediaDesc shape this instance could produce.
                 *
                 * Empty for pure sinks.  Sources populate this with one or
                 * more entries (one for files / generators; many for devices
                 * that support multiple modes).  Transforms populate this
                 * with the shapes derived from the current configuration.
                 */
                const MediaDesc::List &producibleFormats() const { return _producibleFormats; }

                /** @brief Mutable accessor for @ref producibleFormats. */
                MediaDesc::List &producibleFormats() { return _producibleFormats; }

                /** @brief Replaces the producible-formats list. */
                void setProducibleFormats(const MediaDesc::List &val) { _producibleFormats = val; }

                /**
                 * @brief Returns every @ref MediaDesc shape this instance could accept.
                 *
                 * Empty for pure sources.  Sinks and transforms populate
                 * this with the set of shapes their @c writeFrame path
                 * accepts directly (without internal conversion).
                 */
                const MediaDesc::List &acceptableFormats() const { return _acceptableFormats; }

                /** @brief Mutable accessor for @ref acceptableFormats. */
                MediaDesc::List &acceptableFormats() { return _acceptableFormats; }

                /** @brief Replaces the acceptable-formats list. */
                void setAcceptableFormats(const MediaDesc::List &val) { _acceptableFormats = val; }

                /**
                 * @brief Returns the backend's preferred / current default format.
                 *
                 * Sources: the format the backend would emit by default
                 * when opened.  Sinks: the format the backend would prefer
                 * to receive.  Transforms: the output format implied by
                 * the current config.  May be invalid if the backend has
                 * no opinion (e.g. truly format-agnostic passthrough).
                 */
                const MediaDesc &preferredFormat() const { return _preferredFormat; }

                /** @brief Sets the preferred format. */
                void setPreferredFormat(const MediaDesc &val) { _preferredFormat = val; }

                // ------------------------------------------------------------
                // Capabilities
                // ------------------------------------------------------------

                /** @brief True if the resource supports seeking. */
                bool canSeek() const { return _canSeek; }

                /** @brief Sets the seekable flag. */
                void setCanSeek(bool val) { _canSeek = val; }

                /**
                 * @brief Returns the frame count.
                 *
                 * The returned @ref FrameCount may be @c Unknown, @c Empty,
                 * @c Infinite (live / unbounded sources), or a finite
                 * positive count.
                 */
                const FrameCount &frameCount() const { return _frameCount; }

                /** @brief Sets the frame count. */
                void setFrameCount(const FrameCount &val) { _frameCount = val; }

                /** @brief Returns the cached frame rate. */
                const FrameRate &frameRate() const { return _frameRate; }

                /** @brief Sets the cached frame rate. */
                void setFrameRate(const FrameRate &val) { _frameRate = val; }

                /** @brief Returns the cached container-level metadata. */
                const Metadata &containerMetadata() const { return _containerMetadata; }

                /** @brief Mutable accessor for the container-level metadata. */
                Metadata &containerMetadata() { return _containerMetadata; }

                /** @brief Replaces the container-level metadata. */
                void setContainerMetadata(const Metadata &val) { _containerMetadata = val; }

                // ------------------------------------------------------------
                // Diagnostics
                // ------------------------------------------------------------

                /**
                 * @brief Result of the most recent probe attempt.
                 *
                 * @c Error::Ok when the populated fields reflect a
                 * successful probe.  Non-Ok when the probe failed
                 * (device offline, file missing, header malformed) —
                 * the populated fields then reflect whatever was known
                 * before the probe (typically just backend identity
                 * and role flags from @ref MediaIO::FormatDesc).
                 */
                Error probeStatus() const { return _probeStatus; }

                /** @brief Sets the probe status code. */
                void setProbeStatus(Error val) { _probeStatus = val; }

                /** @brief Returns a human-readable note describing @ref probeStatus. */
                const String &probeMessage() const { return _probeMessage; }

                /** @brief Sets the human-readable probe note. */
                void setProbeMessage(const String &val) { _probeMessage = val; }

                // ------------------------------------------------------------
                // Convenience
                // ------------------------------------------------------------

                /**
                 * @brief Produces a human-readable multi-line summary.
                 *
                 * Suitable for logging or display in a CLI / UI.  Includes
                 * backend identity, role flags, format counts (with the
                 * preferred format spelled out), capability flags, and
                 * any probe diagnostic.  Empty fields are elided.
                 *
                 * @return The summary lines.
                 */
                StringList summary() const;

                /**
                 * @brief Serializes the description to a @ref JsonObject.
                 *
                 * Non-empty optional fields are emitted; empty fields
                 * (no metadata, no formats, default values) are omitted
                 * so the JSON stays compact.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Reconstructs a description from a @ref JsonObject.
                 *
                 * Parsing is lenient: fields that are missing or
                 * unrecognised are logged as warnings and skipped
                 * rather than aborting the parse.  The returned
                 * description always reflects the recognised subset
                 * of @p obj — for example, an unknown role string in
                 * the @c roles array warns and leaves that flag
                 * false, but every other recognised role still sets
                 * its flag.  When any field fails to parse @p err is
                 * set to @c Error::Invalid (@c Error::Ok otherwise),
                 * so callers that need strict parsing can still
                 * reject partial results.
                 *
                 * @note The format-list fields (@c producibleFormats,
                 *       @c acceptableFormats, @c preferredFormat) are
                 *       emitted as human-readable summaries by
                 *       @ref toJson and are @em not reconstructed
                 *       here — they come back empty.  Use the
                 *       @ref DataStream operators for lossless
                 *       round-tripping.
                 *
                 * @param obj The JSON object produced by @ref toJson.
                 * @param err Optional error output — set to
                 *            @c Error::Invalid if any field failed to
                 *            parse, @c Error::Ok otherwise.
                 * @return The reconstructed description, populated
                 *         with every field that parsed successfully.
                 */
                static MediaIODescription fromJson(const JsonObject &obj, Error *err = nullptr);

                /** @brief Equality compares every member. */
                bool operator==(const MediaIODescription &other) const;
                bool operator!=(const MediaIODescription &other) const { return !(*this == other); }

        private:
                String          _backendName;
                String          _backendDescription;

                String          _name;
                UUID            _uuid;
                int             _localId = -1;

                bool            _canBeSource     = false;
                bool            _canBeSink       = false;
                bool            _canBeTransform  = false;

                MediaDesc::List _producibleFormats;
                MediaDesc::List _acceptableFormats;
                MediaDesc       _preferredFormat;

                bool            _canSeek    = false;
                FrameCount      _frameCount;
                FrameRate       _frameRate;
                Metadata        _containerMetadata;

                Error           _probeStatus = Error::Ok;
                String          _probeMessage;
};

/** @brief Writes a MediaIODescription to a DataStream. */
DataStream &operator<<(DataStream &stream, const MediaIODescription &d);

/** @brief Reads a MediaIODescription from a DataStream. */
DataStream &operator>>(DataStream &stream, MediaIODescription &d);

PROMEKI_NAMESPACE_END
