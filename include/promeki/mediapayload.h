/**
 * @file      mediapayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/mediatimestamp.h>
#include <promeki/duration.h>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/enums.h>
#include <promeki/variant.h>
#include <promeki/error.h>

#include <promeki/fourcc.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class VariantSpec;

/**
 * @brief Abstract polymorphic base for any media payload flowing
 *        through a pipeline.
 * @ingroup proav
 *
 * A @ref MediaPayload represents one unit of media — an uncompressed
 * video frame, an uncompressed audio block, a compressed access unit,
 * a metadata packet, etc. — carried between pipeline stages.  The
 * common fields apply to every kind:
 *
 * - A @ref BufferView holding 1..N planes of bytes.  A single
 *   entry covers the contiguous case (interleaved RGB, interleaved
 *   PCM, a compressed bitstream); multiple entries cover planar
 *   video (Y/U/V) and planar audio (one buffer per channel).  The
 *   views may alias regions of one shared @ref Buffer (e.g. a
 *   planar frame pulled in by a single @c read) or reference
 *   independently allocated buffers.
 * - Presentation / decode timestamps and a nominal duration.
 * - A @ref Metadata container for payload-intrinsic keys.
 * - A stream index so payloads from distinct tracks in the same
 *   @ref Frame can be told apart.
 * - A bitmask of @ref Flag values that apply generically to every
 *   subclass.
 *
 * Subclasses introduce the fields specific to their family:
 * @ref VideoPayload carries an @ref ImageDesc, @ref AudioPayload an
 * @ref AudioDesc, and the concrete uncompressed / compressed leaves
 * add whatever per-payload state their family needs.  "Is this
 * compressed?" is answered by @ref isCompressed, which the concrete
 * classes implement (typically by delegating to the descriptor's
 * own compressed flag).
 *
 * @par Subclassing
 *
 * Intermediate abstracts (@ref VideoPayload, @ref AudioPayload)
 * leave @ref _promeki_clone and @ref kind pure-virtual; the
 * concrete leaves override them and provide covariant clones.  The
 * concrete classes are intentionally not @c final — codec-specific
 * specializations (for example, an H.264 or ProRes wrapper of
 * @ref CompressedVideoPayload) are a supported extension point.
 *
 * @par Ownership
 *
 * MediaPayload uses the library's native shared-object pattern.
 * Callers manage lifetime via @ref MediaPayload::Ptr .  Polymorphic
 * construction typically goes through a concrete subclass's
 * @c Ptr::create helper; the converting constructor on
 * @ref SharedPtr lets you hand a @c VideoPayload::Ptr into code
 * that accepts @c MediaPayload::Ptr without cloning.
 *
 * @code
 * MediaPayload::Ptr p = UncompressedVideoPayload::Ptr::create(desc, planes);
 * if(p->kind() == MediaPayloadKind::Video) {
 *     auto *vp = p->as<VideoPayload>();  // safe dispatch, no RTTI miss
 *     ...
 * }
 * @endcode
 *
 * @par Thread Safety
 * @ref MediaPayload::Ptr is atomically refcounted, so handing a
 * payload between pipeline stages running on different threads is
 * safe.  A single MediaPayload instance is conditionally
 * thread-safe: const accessors (descriptor, kind, metadata read)
 * are safe; mutators require external synchronization.  The
 * intended pipeline pattern is "produce on stage A, hand off to
 * stage B by Ptr, then mutate copy-on-write from B" — this is
 * race-free as long as no thread keeps a non-Ptr pointer to a
 * payload it has handed off.
 */
class MediaPayload {
        public:
                RefCount              _promeki_refct;
                virtual MediaPayload *_promeki_clone() const = 0;

                /** @brief Shared-pointer alias for pipeline-wide payload handoff. */
                using Ptr = SharedPtr<MediaPayload, /*CopyOnWrite=*/true, MediaPayload>;

                /** @brief List of shared pointers to base @c MediaPayload instances. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Generic bit flags that apply to every payload kind.
                 *
                 * Values are bitwise-OR'able.  Replace the entire mask via
                 * @ref setFlags, or manipulate individual bits via
                 * @ref addFlag / @ref removeFlag / @ref setFlag /
                 * @ref hasFlag.  The convenience predicates
                 * (@ref isKeyframe, @ref isDiscardable, @ref isCorrupt,
                 * @ref isEndOfStream, @ref isIntraRefresh) read from this
                 * mask.
                 *
                 * Flags specific to one payload family (for example,
                 * @c ParameterSet on compressed video) live on the
                 * concrete subclass rather than the base.  The mask is
                 * 64 bits wide so subclass flags never have to
                 * compete for the same bit range as the base flags.
                 */
                enum Flag : uint64_t {
                        None = 0, ///< No flags set.
                        Keyframe =
                                1ull
                                << 0, ///< Self-contained decode entry point (trivially true for uncompressed payloads).
                        Discardable = 1ull
                                      << 1, ///< Non-reference payload — safe to drop without affecting later decode.
                        Corrupt =
                                1ull
                                << 2, ///< Payload is known to be corrupt; pair with @c Metadata::CorruptReason when a message is available.
                        EndOfStream = 1ull << 3, ///< Terminal payload in the stream.
                        IntraRefresh =
                                1ull
                                << 4, ///< Intra-coded but not a random-access point — part of a gradual refresh cycle (distinct from @c Keyframe).
                };

                /** @brief Constructs an empty payload (no data, default timestamps). */
                MediaPayload() = default;

                /**
                 * @brief Constructs a payload that wraps an explicit
                 *        plane list.
                 *
                 * The @p data list is a @ref BufferView which is
                 * natively a list; a single-plane payload is
                 * constructed by passing a list-of-one.
                 */
                explicit MediaPayload(const BufferView &data) : _data(data) {}

                MediaPayload(const MediaPayload &) = default;
                MediaPayload(MediaPayload &&) = default;
                MediaPayload &operator=(const MediaPayload &) = default;
                MediaPayload &operator=(MediaPayload &&) = default;

                /** @brief Virtual destructor; subclasses manage their own fields. */
                virtual ~MediaPayload() = default;

                /** @brief Returns the coarse payload category for dispatch. */
                virtual const MediaPayloadKind &kind() const = 0;

                /**
                 * @brief Returns true when this payload carries a compressed
                 *        bitstream.
                 *
                 * Concrete classes implement this — typically the
                 * @c Uncompressed* leaves return @c false and the
                 * @c Compressed* leaves return @c true, but a subclass
                 * is free to delegate to its descriptor if both
                 * compressed and uncompressed variants share one
                 * class.
                 */
                virtual bool isCompressed() const = 0;

                /**
                 * @brief True when the payload has a plane worth dispatching.
                 *
                 * Subclasses may tighten this (for example a compressed
                 * payload that also requires a codec identity), but the
                 * base predicate is simply "has at least one non-empty
                 * plane."
                 */
                virtual bool isValid() const {
                        if (_data.isEmpty()) return false;
                        for (auto v : _data) {
                                if (v.isValid()) return true;
                        }
                        return false;
                }

                // ---- Plane data -------------------------------------------

                /** @brief Returns the plane list as a const reference. */
                const BufferView &data() const { return _data; }

                /** @brief Returns a mutable reference to the plane list. */
                BufferView &data() { return _data; }

                /** @brief Replaces the entire plane list. */
                void setData(const BufferView &d) { _data = d; }

                /** @brief Returns the number of planes (1 for interleaved, N for planar). */
                size_t planeCount() const { return _data.count(); }

                /** @brief Returns a proxy for a single plane's slice. */
                BufferView::Entry plane(size_t index) const { return _data[index]; }

                /** @brief Returns the total byte size summed across all planes. */
                size_t size() const { return _data.totalSize(); }

                // ---- Stream index -----------------------------------------

                /**
                 * @brief Returns the stream index this payload belongs to.
                 *
                 * Used by multi-track @ref Frame handling to tell payloads
                 * from distinct streams apart (two audio tracks, two
                 * subtitle tracks, etc.).  Defaults to @c 0.
                 */
                int streamIndex() const { return _streamIndex; }

                /** @brief Sets the stream index. */
                void setStreamIndex(int idx) { _streamIndex = idx; }

                // ---- Timing -----------------------------------------------

                /** @brief Returns the presentation timestamp. */
                const MediaTimeStamp &pts() const { return _pts; }

                /** @brief Sets the presentation timestamp. */
                void setPts(const MediaTimeStamp &ts) { _pts = ts; }

                /**
                 * @brief Returns the decode timestamp.
                 *
                 * For uncompressed payloads and codecs without B-frame
                 * reorder the DTS normally equals the PTS.  B-frame-
                 * capable video codecs record the actual decode order
                 * here.
                 */
                const MediaTimeStamp &dts() const { return _dts; }

                /** @brief Sets the decode timestamp. */
                void setDts(const MediaTimeStamp &ts) { _dts = ts; }

                /**
                 * @brief Returns the wall-clock duration spanned by this
                 *        payload, or a zero @ref Duration when no
                 *        duration is available.
                 *
                 * Polymorphic hook — the base default returns
                 * @c Duration() because @ref MediaPayload does not
                 * itself own any state from which a duration could be
                 * derived.  Subclasses override:
                 *
                 *  - @ref AudioPayload computes @c sampleCount() /
                 *    @c desc().sampleRate() — one source of truth, no
                 *    storage needed.
                 *  - @ref VideoPayload returns a native duration
                 *    field populated by the enclosing pipeline
                 *    (typically @ref MediaIO stamps one frame of the
                 *    session frame rate when the producer did not).
                 *
                 * A zero @ref Duration from a payload whose
                 * @ref hasDuration returns @c true means "no value
                 * has been stamped yet" — @ref MediaIO uses that
                 * combination as its fill trigger.
                 */
                virtual Duration duration() const { return Duration(); }

                /**
                 * @brief Assigns a wall-clock duration to this payload.
                 *
                 * The base returns @ref Error::NotSupported.  Subclasses
                 * override to accept: @ref VideoPayload stores the
                 * value natively and returns @ref Error::Ok;
                 * @ref AudioPayload still returns
                 * @ref Error::NotSupported because its duration is
                 * derived from intrinsic @c sampleCount /
                 * @c sampleRate — call sites that want to change an
                 * audio duration must adjust the sample count
                 * instead.
                 */
                virtual Error setDuration(const Duration &val) {
                        (void)val;
                        return Error::NotSupported;
                }

                /**
                 * @brief Returns @c true when this payload kind
                 *        supports a duration at all.
                 *
                 * Type-level predicate — the answer is fixed by the
                 * concrete class, not by whether @ref setDuration has
                 * been called yet.  @ref VideoPayload and
                 * @ref AudioPayload both override to return @c true
                 * unconditionally; the base returns @c false so a
                 * custom non-AV payload opts in explicitly.
                 *
                 * @ref MediaIO pairs this with @c duration().isZero()
                 * to decide whether a payload needs a fallback
                 * duration stamped before it leaves the pipeline.
                 */
                virtual bool hasDuration() const { return false; }

                // ---- Flags ------------------------------------------------

                /** @brief Returns the raw flag bitmask. */
                uint64_t flags() const { return _flags; }

                /** @brief Replaces the entire flag bitmask. */
                void setFlags(uint64_t f) { _flags = f; }

                /** @brief Sets a single flag in the bitmask. */
                void addFlag(Flag f) { _flags |= static_cast<uint64_t>(f); }

                /** @brief Clears a single flag from the bitmask. */
                void removeFlag(Flag f) { _flags &= ~static_cast<uint64_t>(f); }

                /**
                 * @brief Sets or clears a flag depending on @p on.
                 *
                 * Equivalent to @ref addFlag when @p on is @c true and
                 * @ref removeFlag when it is @c false.  Convenient for
                 * call sites that derive a flag's value from a
                 * condition (e.g. @c setFlag(Keyframe, picType==IDR)).
                 */
                void setFlag(Flag f, bool on) {
                        if (on)
                                addFlag(f);
                        else
                                removeFlag(f);
                }

                /** @brief Returns true when the given flag is set. */
                bool hasFlag(Flag f) const { return (_flags & static_cast<uint64_t>(f)) != 0; }

                /**
                 * @brief Returns a short human-readable name for a
                 *        single base @ref Flag bit, or @c nullptr
                 *        when @p f is not a recognised single-bit
                 *        value.
                 *
                 * Intended for diagnostics and logging — callers that
                 * want to dump a full mask should walk each bit.
                 * Returns @c "None" for @c MediaPayload::None.
                 * Subclass-specific flags (for example
                 * @c CompressedVideoPayload::ParameterSet) are not
                 * recognised by this base function.
                 */
                static const char *flagName(Flag f);

                /**
                 * @brief Convenience: true when the payload is a self-
                 *        contained decode entry point.
                 *
                 * Virtual so subclasses whose family is trivially keyframe-
                 * able (every uncompressed payload, intra-only codecs)
                 * can return @c true without needing the @c Keyframe flag
                 * to be set manually.  The default checks the flag.
                 */
                virtual bool isKeyframe() const { return hasFlag(Keyframe); }

                /**
                 * @brief Returns true when stopping the stream before this
                 *        payload leaves downstream consumers in a coherent
                 *        state.
                 *
                 * Distinct from @ref isKeyframe — a payload can be a
                 * decode entry point without being a safe cut (H.264
                 * open-GOP, where an I-frame's trailing B-frames still
                 * reference the previous GOP), or be a safe cut without
                 * being the codec's "keyframe" concept (audio codecs
                 * whose @c PacketIndependence is @c Every).  Subclasses
                 * override with codec- or family-specific rules; the
                 * default falls back to @ref isKeyframe as a
                 * conservative baseline.
                 *
                 * Used by @ref Frame::isSafeCutPoint and
                 * @ref MediaPipeline to honour a pipeline-wide frame
                 * count without truncating a GOP or stranding a
                 * non-decodable audio packet.
                 */
                virtual bool isSafeCutPoint() const { return isKeyframe(); }

                /** @brief Convenience: true when the @c Discardable flag is set. */
                bool isDiscardable() const { return hasFlag(Discardable); }

                /** @brief Convenience: true when the @c Corrupt flag is set. */
                bool isCorrupt() const { return hasFlag(Corrupt); }

                /** @brief Convenience: true when the @c EndOfStream flag is set. */
                bool isEndOfStream() const { return hasFlag(EndOfStream); }

                /**
                 * @brief Convenience: true when the @c IntraRefresh flag
                 *        is set — the payload is intra-coded but not a
                 *        random-access point.
                 *
                 * Distinct from @ref isKeyframe: a gradual-refresh
                 * slice refreshes part of the frame each GOP so the
                 * stream becomes decodable only after a full refresh
                 * cycle completes, not at any single frame.  Seekers
                 * and random-access consumers must use @ref isKeyframe
                 * or @ref isSafeCutPoint, not this predicate.
                 */
                bool isIntraRefresh() const { return hasFlag(IntraRefresh); }

                /** @brief Sets (or clears) the @c EndOfStream flag. */
                void markEndOfStream(bool v = true) {
                        if (v)
                                addFlag(EndOfStream);
                        else
                                removeFlag(EndOfStream);
                }

                /**
                 * @brief Marks the payload as corrupt with an optional
                 *        human-readable reason.
                 *
                 * Sets the @c Corrupt flag and, when @p reason is non-empty,
                 * records the reason under @c Metadata::CorruptReason.
                 * Pass an empty @p reason to flip the flag without
                 * introducing (or overwriting) a reason entry.
                 */
                void markCorrupt(const String &reason = String()) {
                        addFlag(Corrupt);
                        if (!reason.isEmpty()) metadata().set(Metadata::CorruptReason, reason);
                }

                /** @brief Returns the recorded corruption reason, or an empty string. */
                String corruptReason() const { return metadata().getAs<String>(Metadata::CorruptReason); }

                // ---- Metadata ---------------------------------------------

                /**
                 * @brief Returns the payload's metadata container.
                 *
                 * Pure virtual on the base — each concrete subclass
                 * decides where the metadata lives:
                 *
                 *  - @ref VideoPayload and @ref AudioPayload forward
                 *    to @c desc().metadata(), so
                 *    @c VideoPayload::metadata returns the
                 *    @ref ImageDesc metadata (the @c FrameRate /
                 *    colorimetry / timecode keys) and
                 *    @c AudioPayload::metadata returns the
                 *    @ref AudioDesc metadata.  The metadata lives
                 *    next to the descriptor it annotates.
                 *  - Non-AV subclasses (subtitle, ancillary, custom)
                 *    maintain their own @ref Metadata member and
                 *    return a reference to it.
                 *
                 * This lets @ref markCorrupt, @ref corruptReason,
                 * @ref VariantLookup's @c Meta.* binding and any
                 * generic consumer hold a @ref MediaPayload reference
                 * and reach the right metadata store without knowing
                 * the concrete type.
                 */
                virtual const Metadata &metadata() const = 0;

                /** @copydoc metadata() const */
                virtual Metadata &metadata() = 0;

                // ---- Safe downcast ----------------------------------------

                /**
                 * @brief Returns @c this cast to @p T if the dynamic type
                 *        matches, or @c nullptr otherwise.
                 *
                 * Lets pipeline glue land on the concrete type without
                 * exposing @c dynamic_cast at call sites.
                 */
                template <typename T> const T *as() const { return dynamic_cast<const T *>(this); }

                /** @copydoc as() const */
                template <typename T> T *as() { return dynamic_cast<T *>(this); }

                // ---- Exclusive ownership ----------------------------------

                /**
                 * @brief Returns @c true when every plane's backing
                 *        @ref Buffer has a single owner (reference
                 *        count &le; 1) @em and the payload's
                 *        subclass-specific extras are exclusive.
                 *
                 * Used by mutation consumers (paint, data-encoder stamp,
                 * resample) to decide whether they need to
                 * @ref ensureExclusive before writing.  Subclasses
                 * extend the check by overriding
                 * @ref isExclusiveExtras — the base plane-list check
                 * always runs first.
                 */
                bool isExclusive() const { return _data.isExclusive() && isExclusiveExtras(); }

                // ---- DataStream serialisation ------------------------------

                /**
                 * @brief Stable identifier a concrete subclass reports so
                 *        @ref DataStream can dispatch to the right factory
                 *        on read.
                 *
                 * Every concrete MediaPayload subclass must return a
                 * distinct, stable value here.  Typically a FourCC that
                 * survives as a wire-format constant.  Pure virtual on
                 * the abstract base — a concrete subclass that forgets
                 * to override it won't compile.
                 */
                virtual uint32_t subclassFourCC() const = 0;

                /**
                 * @brief Writes the subclass-specific state to @p s.
                 *
                 * The @ref DataStream @c operator<< for @ref MediaPayload::Ptr
                 * already writes the common base state (planes, timing,
                 * metadata, streamIndex, flags) before invoking this
                 * hook — subclasses only serialise their own fields
                 * (descriptor, sampleCount, codec private data, ...).
                 */
                virtual void serialisePayload(DataStream &s) const = 0;

                /**
                 * @brief Reads the subclass-specific state from @p s.
                 *
                 * Counterpart to @ref serialisePayload.  The base's
                 * common state has already been populated when this is
                 * invoked — the subclass only reads its own fields.
                 */
                virtual void deserialisePayload(DataStream &s) = 0;

                /** @brief Factory signature used by the subclass registry. */
                using Factory = Ptr (*)();

                /**
                 * @brief Registers a concrete subclass with the payload
                 *        deserialisation registry.
                 *
                 * Called once per process at static-init time by each
                 * concrete subclass (via @ref PROMEKI_REGISTER_MEDIAPAYLOAD).
                 * @p fourcc must match what @ref subclassFourCC returns on
                 * instances of @p factory 's type.
                 */
                static void registerSubclass(uint32_t fourcc, Factory factory);

                /**
                 * @brief Constructs an empty @ref MediaPayload::Ptr whose
                 *        concrete type matches @p fourcc.
                 *
                 * Returns a null @ref MediaPayload::Ptr when @p fourcc is
                 * not registered; @ref DataStream uses this on the read
                 * side to build the right leaf before calling
                 * @ref deserialisePayload.
                 */
                static Ptr createEmpty(uint32_t fourcc);

                /**
                 * @brief Detaches every plane's backing @ref Buffer so
                 *        writes through this payload don't propagate
                 *        to other holders of the same @ref Buffer.
                 *
                 * Walks @ref data() and, when a plane's backing
                 * @ref Buffer is shared (reference count &gt; 1),
                 * clones it via @ref Buffer::Ptr::modify.  Whole-
                 * buffer views keep their @c offset=0, @c size=full
                 * invariant; sub-range views retain offset and size
                 * but point at the exclusive clone.
                 *
                 * Calls @ref ensureExclusiveExtras after the plane
                 * detach so subclasses can CoW-detach any extra
                 * @ref Buffer fields they own (codec private data,
                 * auxiliary metadata buffers, …).
                 */
                void ensureExclusive() {
                        _data.ensureExclusive();
                        ensureExclusiveExtras();
                }

                // ---- VariantLookup polymorphic dispatch -------------------

                /**
                 * @brief Resolves @p key against the @em most-derived
                 *        VariantLookup registry for this payload.
                 *
                 * Opt-in hook consumed by @ref VariantLookup — the
                 * concept @ref detail::HasVariantLookupDispatch picks
                 * it up by name.  The concrete leaf's override calls
                 * @c VariantLookup<ConcreteLeaf>::resolveDirect so the
                 * leaf's keys land first and the upward cascade
                 * (@c inheritsFrom on VideoPayload / AudioPayload /
                 * MediaPayload) fills in inherited keys.  Pure virtual
                 * on the abstract base — concrete leaves use
                 * @ref PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH to
                 * generate the override uniformly.
                 */
                virtual std::optional<Variant> variantLookupResolve(const String &key, Error *err = nullptr) const = 0;

                /**
                 * @brief Assigns @p value under @p key into this
                 *        payload, dispatching through the most-derived
                 *        VariantLookup registry.  @sa @ref variantLookupResolve.
                 */
                virtual bool variantLookupAssign(const String &key, const Variant &value, Error *err = nullptr) = 0;

                /**
                 * @brief Returns the declared @ref VariantSpec for
                 *        @p key, dispatching through the most-derived
                 *        VariantLookup registry.  @sa
                 *        @ref variantLookupResolve.
                 */
                virtual const VariantSpec *variantLookupSpecFor(const String &key, Error *err = nullptr) const = 0;

                /**
                 * @brief Returns the scalar key names the most-derived
                 *        VariantLookup registry exposes.
                 *
                 * Drives introspection consumers such as
                 * @ref Frame::dump that want to enumerate every
                 * surfaceable scalar on a payload — the base-type
                 * enumerator @c VariantLookup<VideoPayload>::registeredScalars
                 * only walks the intermediate's registry, which
                 * excludes concrete-leaf specifics (e.g.
                 * @ref CompressedVideoPayload::frameType,
                 * @ref PcmAudioPayload::sampleCount).  The
                 * virtual dispatch routes through the concrete leaf
                 * so its @c registeredScalars is used, with inherited
                 * keys de-duplicated by the lookup's own merge.
                 */
                virtual StringList variantLookupScalarNames() const = 0;

                /**
                 * @brief Returns a complete recursive dump of this
                 *        payload.
                 *
                 * Concrete leaves override via
                 * @ref PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH to forward
                 * into @c VariantLookup<Self>::dump, which walks every
                 * scalar (cascading up through the inherit chain),
                 * every child composition (@c Desc.* on video / audio
                 * descriptors), and every database binding (@c Meta.*
                 * entries) through the most-derived registry.
                 * @ref Frame::dump uses this to emit one uniform
                 * section per payload regardless of kind.
                 */
                virtual StringList variantLookupDump(const String &indent = String()) const = 0;

        protected:
                /**
                 * @brief Hook for subclasses to report whether their
                 *        extra buffers are exclusive.
                 *
                 * Default: no extras, returns @c true.  Subclasses
                 * that own additional @ref Buffer fields (e.g.
                 * @ref CompressedVideoPayload::inBandCodecData)
                 * override and return @c false when any of those
                 * fields is shared.
                 */
                virtual bool isExclusiveExtras() const { return true; }

                /**
                 * @brief Hook for subclasses to CoW-detach their extra
                 *        buffers.
                 *
                 * Default: no-op.  Subclasses override and call
                 * @ref Buffer::Ptr::modify on each extra field that
                 * holds a shared @ref Buffer.
                 */
                virtual void ensureExclusiveExtras() {}

        public:
        private:
                BufferView     _data;
                MediaTimeStamp _pts;
                MediaTimeStamp _dts;
                int            _streamIndex = 0;
                uint64_t       _flags = 0;
};

/** @brief Writes a MediaPayload::Ptr to a DataStream. */
DataStream &operator<<(DataStream &s, const MediaPayload::Ptr &p);

/** @brief Reads a MediaPayload::Ptr from a DataStream. */
DataStream &operator>>(DataStream &s, MediaPayload::Ptr &p);

/**
 * @brief Registers a concrete @ref MediaPayload subclass with the
 *        deserialisation registry.
 *
 * Place at the top level of a concrete subclass's source file.  The
 * fourcc string must match what the class's @c subclassFourCC override
 * returns.  Example:
 * @code
 * PROMEKI_REGISTER_MEDIAPAYLOAD(UncompressedVideoPayload, "UVdp");
 * @endcode
 */
#define PROMEKI_REGISTER_MEDIAPAYLOAD(Cls, fourccStr)                                                                  \
        namespace {                                                                                                    \
                struct Cls##_MediaPayloadRegistrar {                                                                   \
                                Cls##_MediaPayloadRegistrar() {                                                        \
                                        ::promeki::MediaPayload::registerSubclass(                                     \
                                                ::promeki::FourCC(fourccStr).value(),                                  \
                                                []() -> ::promeki::MediaPayload::Ptr {                                 \
                                                        return ::promeki::Cls::Ptr::create();                          \
                                                });                                                                    \
                                }                                                                                      \
                };                                                                                                     \
                static Cls##_MediaPayloadRegistrar s_##Cls##_mediaPayloadRegistrar;                                    \
        }

/**
 * @brief Implements MediaPayload's VariantLookup dispatch hooks for a
 *        concrete leaf.
 * @ingroup proav
 *
 * Place in the @c public section of a concrete @ref MediaPayload
 * subclass's @c class body.  Expands to overrides of
 * @ref MediaPayload::variantLookupResolve / @c variantLookupAssign /
 * @c variantLookupSpecFor that forward to
 * @c VariantLookup<Self>::resolveDirect / @c assignDirect /
 * @c specForDirect, so a caller holding a reference-to-base lands on
 * @p Self's registry and the upward cascade fills in inherited keys.
 * Using @c *Direct (not the public dispatch entry) avoids infinite
 * recursion through the virtual.
 *
 * @param Self The concrete subclass the macro is expanded inside of.
 *
 * @par Example
 * @code
 * class UncompressedVideoPayload : public VideoPayload {
 *         public:
 *                 PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH(UncompressedVideoPayload)
 *                 ...
 * };
 * @endcode
 */
#define PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH(Self)                                                                     \
        std::optional<::promeki::Variant> variantLookupResolve(const ::promeki::String &key,                           \
                                                               ::promeki::Error        *err = nullptr) const override {       \
                return ::promeki::VariantLookup<Self>::resolveDirect(*this, key, err);                                 \
        }                                                                                                              \
        bool variantLookupAssign(const ::promeki::String &key, const ::promeki::Variant &value,                        \
                                 ::promeki::Error *err = nullptr) override {                                           \
                return ::promeki::VariantLookup<Self>::assignDirect(*this, key, value, err);                           \
        }                                                                                                              \
        const ::promeki::VariantSpec *variantLookupSpecFor(const ::promeki::String &key,                               \
                                                           ::promeki::Error        *err = nullptr) const override {           \
                return ::promeki::VariantLookup<Self>::specForDirect(key, err);                                        \
        }                                                                                                              \
        ::promeki::StringList variantLookupScalarNames() const override {                                              \
                return ::promeki::VariantLookup<Self>::registeredScalars();                                            \
        }                                                                                                              \
        ::promeki::StringList variantLookupDump(const ::promeki::String &indent = ::promeki::String())                 \
                const override {                                                                                       \
                return ::promeki::VariantLookup<Self>::dump(*this, indent);                                            \
        }

PROMEKI_NAMESPACE_END
