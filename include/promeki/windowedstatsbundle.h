/**
 * @file      windowedstatsbundle.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mediaiostats.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/windowedstat.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-stat-ID @ref WindowedStat container for telemetry breakdowns.
 * @ingroup mediaio_user
 *
 * Holds one @ref WindowedStat per @ref MediaIOStats::ID — the natural
 * shape of a "windowed" view onto per-command telemetry.  Created when
 * the previous @c Map<Kind, MediaIOStats> design ran into a type-system
 * mismatch: registered specs declare @c ExecuteDuration as
 * @c TypeDuration, but the windowed view of the same key holds a
 * @c WindowedStat of nanoseconds — strict spec validation rightly
 * rejects that swap.  Pulling the windowed slot into its own type
 * removes the conflict without disabling validation anywhere.
 *
 * The @ref WindowedStatsBundle does not carry its own ID namespace;
 * the keys are @ref MediaIOStats::ID values so collectors and consumers
 * keep using the same well-known identifiers
 * (@c ExecuteDuration, @c QueueWaitDuration, @c BytesProcessed, ...)
 * the @ref MediaIOStats already declares.
 *
 * Serialization round-trips through JSON (each entry is the canonical
 * @c "cap=N:[...]" string parsed by @ref WindowedStat::fromString) and
 * @ref DataStream (one @c TypeWindowedStat tag per entry).
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 */
class WindowedStatsBundle {
        public:
                /** @brief Stat-ID namespace shared with @ref MediaIOStats. */
                using ID = MediaIOStats::ID;

                /** @brief Underlying ordered map type. */
                using Map = promeki::Map<ID, WindowedStat>;

                /** @brief List of plain-value bundles. */
                using List = promeki::List<WindowedStatsBundle>;

                WindowedStatsBundle() = default;

                // ------------------------------------------------------------
                // Capacity / membership
                // ------------------------------------------------------------

                /** @brief Returns the number of stat IDs the bundle tracks. */
                size_t size() const { return _windows.size(); }

                /** @brief True when no entries have been added. */
                bool isEmpty() const { return _windows.isEmpty(); }

                /** @brief True when the bundle holds a window for @p id. */
                bool contains(ID id) const { return _windows.contains(id); }

                /** @brief Removes every entry. */
                void clear() { _windows.clear(); }

                // ------------------------------------------------------------
                // Accessors
                // ------------------------------------------------------------

                /**
                 * @brief Returns the window for @p id, or an empty
                 *        @ref WindowedStat when @p id is not present.
                 */
                WindowedStat get(ID id) const;

                /**
                 * @brief Inserts or replaces the window for @p id.
                 *
                 * Move-friendly overload also provided.
                 */
                void set(ID id, const WindowedStat &ws) { _windows.insert(id, ws); }

                /** @copydoc set(ID, const WindowedStat &) */
                void set(ID id, WindowedStat &&ws) { _windows.insert(id, std::move(ws)); }

                /** @brief Removes @p id from the bundle.  No-op when absent. */
                void remove(ID id);

                /** @brief Returns the underlying ordered map (read-only). */
                const Map &windows() const { return _windows; }

                /** @brief Returns the underlying ordered map (mutable). */
                Map &windows() { return _windows; }

                /**
                 * @brief Iterates every entry as @c (ID, const WindowedStat &).
                 *
                 * Walks the underlying map in ID order.  Pure
                 * read-only iteration — mutation happens through
                 * @ref set / @ref remove.
                 */
                template <typename Func> void forEach(Func &&func) const {
                        for (auto it = _windows.cbegin(); it != _windows.cend(); ++it) {
                                func(it->first, it->second);
                        }
                }

                // ------------------------------------------------------------
                // Rendering / serialization
                // ------------------------------------------------------------

                /**
                 * @brief Renders every entry as a multi-line summary.
                 *
                 * One line per stat ID: @c "<name>: <window summary>",
                 * where the window summary comes from
                 * @ref WindowedStat::toString.  The optional
                 * @p formatter is forwarded to each window so callers
                 * can humanise values per stat ID — useful for
                 * unit-aware output (ms, MB) where the picker depends
                 * on the ID name.
                 */
                using ValueFormatter = std::function<WindowedStat::ValueFormatter(ID)>;
                StringList describe(const ValueFormatter &formatter = ValueFormatter()) const;

                /** @brief Serializes the bundle as a JSON object keyed by stat-ID name. */
                JsonObject toJson() const;

                /**
                 * @brief Reconstructs a bundle from a JSON object produced by @ref toJson.
                 *
                 * Each value is expected to be the canonical
                 * @c "cap=N:[...]" string @ref WindowedStat::fromString
                 * accepts.  Entries whose values fail to parse are
                 * skipped and surfaced through the optional @p err
                 * output.
                 */
                static WindowedStatsBundle fromJson(const JsonObject &obj, Error *err = nullptr);

                /** @brief Equality compares every entry by ID and value. */
                bool operator==(const WindowedStatsBundle &other) const { return _windows == other._windows; }
                bool operator!=(const WindowedStatsBundle &other) const { return !(*this == other); }

        private:
                Map _windows;
};

/** @brief Writes a WindowedStatsBundle to a DataStream. */
DataStream &operator<<(DataStream &stream, const WindowedStatsBundle &b);

/** @brief Reads a WindowedStatsBundle from a DataStream. */
DataStream &operator>>(DataStream &stream, WindowedStatsBundle &b);

PROMEKI_NAMESPACE_END
