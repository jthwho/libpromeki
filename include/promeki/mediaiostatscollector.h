/**
 * @file      mediaiostatscollector.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <functional>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiostats.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Composite key identifying a single window in @ref MediaIOStatsCollector.
 *
 * A window is uniquely identified by the @ref MediaIOCommand::Kind that
 * produced the sample and the @ref MediaIOStats::ID under which the
 * sample was stamped, so a Read command's @c ExecuteDuration and a
 * Write command's @c ExecuteDuration end up in distinct windows even
 * though they share an ID.
 *
 * Hoisted out of @ref MediaIOStatsCollector so that
 * @c std::hash<MediaIOStatsCollectorKey> can be specialized at namespace
 * scope before the collector instantiates @c HashMap<Key, ...>.
 * @ref MediaIOStatsCollector::Key is preserved as a type alias.
 */
struct MediaIOStatsCollectorKey {
                /** @brief Command kind that produced the sample. */
                MediaIOCommand::Kind kind = MediaIOCommand::Open;

                /** @brief Stat ID the sample was stamped under. */
                MediaIOStats::ID id;

                /** @brief Equality compares both fields. */
                bool operator==(const MediaIOStatsCollectorKey &o) const noexcept {
                        return kind == o.kind && id == o.id;
                }

                /** @brief Inverse of @ref operator==. */
                bool operator!=(const MediaIOStatsCollectorKey &o) const noexcept { return !(*this == o); }
};

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::hash specialization for @ref promeki::MediaIOStatsCollectorKey.
 *
 * Combines the integer command type with the stat ID's hash via
 * boost-style mixing so distinct (type, id) pairs land on different
 * slots even when the underlying ID hash happens to be small.
 */
template <> struct std::hash<promeki::MediaIOStatsCollectorKey> {
                std::size_t operator()(const promeki::MediaIOStatsCollectorKey &k) const noexcept {
                        std::size_t h = std::hash<promeki::MediaIOStats::ID>{}(k.id);
                        h ^= static_cast<std::size_t>(k.kind) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
                        return h;
                }
};

#include <promeki/hashmap.h>
#include <promeki/objectbase.h>
#include <promeki/windowedstat.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIO;

/**
 * @brief Rolling per-command telemetry collector for a single @ref MediaIO.
 * @ingroup mediaio_user
 *
 * Subscribes to a target @ref MediaIO 's @ref MediaIO::commandCompletedSignal
 * and folds every numeric per-command stat (@c ExecuteDuration,
 * @c QueueWaitDuration, @c BytesProcessed, plus any backend-specific keys)
 * into a fixed-capacity @ref WindowedStat ring keyed by the
 * (@ref MediaIOCommand::Kind, @ref MediaIOStats::ID) pair the sample
 * arrived under.  Each window then exposes
 * min / max / average / stddev / sample count via the @ref WindowedStat
 * accessors.
 *
 * The collector is intentionally MediaIO-agnostic on the storage side —
 * it does not pre-declare which keys to track.  Any numeric Variant the
 * framework or backend stamps onto a command's @ref MediaIOCommand::stats
 * becomes a window automatically the first time it is observed.
 * Non-numeric entries (strings, enums, object types, invalid) are
 * skipped silently — see @ref WindowedStat::push(const Variant&) for the
 * full promotion table.
 *
 * @par Ownership
 * Inherits @ref ObjectBase and uses ObjectBase parent-child ownership:
 * pass the owning @ref ObjectBase as @p parent to the constructor and
 * the collector is destroyed alongside its parent.  No shared
 * ownership, no factory function, no @c std::shared_ptr.
 *
 * The target is held via @ref ObjectBasePtr so that the pointer
 * auto-clears if the @ref MediaIO is destroyed independently — the
 * collector then becomes a no-op until @ref setTarget rewires it.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase: thread-affine.  Subscription to the
 * @ref MediaIO 's @c commandCompletedSignal uses the ObjectBase-owner
 * @ref Signal::connect overload, so per-command notifications are
 * automatically marshalled onto the collector's @ref EventLoop when the
 * signal fires from a different thread (e.g. a strand worker).  Every
 * read and write to the window map therefore runs on the collector's
 * own thread, so no internal mutex is required.
 */
class MediaIOStatsCollector : public ObjectBase {
                PROMEKI_OBJECT(MediaIOStatsCollector, ObjectBase)
        public:
                /** @brief Composite key identifying a single window. */
                using Key = MediaIOStatsCollectorKey;

                /** @brief Unordered map type for window storage. */
                using WindowMap = HashMap<Key, WindowedStat>;

                /** @brief Default ring capacity used when no size is specified. */
                static constexpr int DefaultWindowSize = 256;

                /**
                 * @brief Constructs a detached collector with no target.
                 *
                 * Use @ref setTarget to attach to a @ref MediaIO once
                 * one is available.  Until then the collector is a no-op
                 * — useful when the target is wired after construction
                 * (for example in tests, or when the MediaIO is created
                 * lazily by a factory).
                 */
                explicit MediaIOStatsCollector(ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs and immediately attaches to @p target.
                 *
                 * Equivalent to default-construct + @ref setTarget; provided
                 * as a convenience for the common single-target case.
                 */
                explicit MediaIOStatsCollector(MediaIO *target, ObjectBase *parent = nullptr);

                /** @brief Detaches from the current target (if any) before destruction. */
                ~MediaIOStatsCollector() override;

                /** @brief Returns the currently-observed @ref MediaIO, or @c nullptr. */
                MediaIO *target() const { return const_cast<ObjectBasePtr<MediaIO> &>(_target).data(); }

                /**
                 * @brief Switches to a new target (or detaches when @p io is @c nullptr).
                 *
                 * Disconnects from the previous target's
                 * @ref MediaIO::commandCompletedSignal (if any), clears
                 * the window map (windows from the old target are
                 * meaningless against the new one), and connects to the
                 * new target's signal.  Pass @c nullptr to detach.
                 */
                void setTarget(MediaIO *io);

                /** @brief Returns the configured window capacity. */
                int windowSize() const { return _windowSize; }

                /**
                 * @brief Resizes every existing window in place.
                 *
                 * When @p n is smaller than the current sample count
                 * each window drops its oldest samples first via
                 * @ref WindowedStat::setCapacity.  When @p n is zero
                 * every window is cleared and subsequent samples are
                 * dropped on the floor.  Negative values clamp to zero.
                 */
                void setWindowSize(int n);

                /**
                 * @brief Returns the live window map by const reference.
                 *
                 * Walking the result is safe as long as no observed
                 * command lands on the collector concurrently — which
                 * is guaranteed when the caller runs on the collector's
                 * own @ref EventLoop thread.  Callers on other threads
                 * should marshal through the collector's loop.
                 */
                const WindowMap &windows() const { return _windows; }

                /**
                 * @brief Returns the window for @p key, or an empty
                 *        @ref WindowedStat if the key has not been observed.
                 */
                WindowedStat window(const Key &key) const;

                /** @brief Drops every observed window but keeps the configured window size. */
                void clear();

        private:
                void onCommandCompleted(MediaIOCommand::Ptr cmd);

                ObjectBasePtr<MediaIO> _target;
                int                    _windowSize = DefaultWindowSize;
                WindowMap              _windows;
};

PROMEKI_NAMESPACE_END
