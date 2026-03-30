/**
 * @file      core/benchmark.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/stringregistry.h>
#include <promeki/core/timestamp.h>
#include <promeki/core/pair.h>
#include <promeki/core/list.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Ordered collection of named timestamps for pipeline performance measurement.
 * @ingroup core_util
 *
 * Benchmark records a chronological sequence of named timestamps as a frame
 * flows through a processing pipeline.  Each entry pairs a registered string
 * ID with the TimeStamp at which the event occurred.  The Benchmark class
 * itself serves as the StringRegistry tag type, so all benchmark IDs share
 * a single process-wide registry.
 *
 * Typical usage: the pipeline infrastructure stamps well-known events
 * (Queued, BeginProcess, EndProcess) automatically.  Individual nodes may
 * add custom stamps for finer-grained profiling.
 *
 * @par Example
 * @code
 * Benchmark bm;
 * bm.stamp(Benchmark::Id("MyNode.BeginProcess"));
 * // ... do work ...
 * bm.stamp(Benchmark::Id("MyNode.EndProcess"));
 *
 * double dt = bm.duration(
 *     Benchmark::Id("MyNode.BeginProcess"),
 *     Benchmark::Id("MyNode.EndProcess"));
 * @endcode
 */
class Benchmark {
        PROMEKI_SHARED_FINAL(Benchmark)
        public:
                /** @brief Shared pointer type for Benchmark. */
                using Ptr = SharedPtr<Benchmark>;
                /** @brief Plain value list of Benchmark objects. */
                using List = promeki::List<Benchmark>;
                /** @brief List of shared pointers to Benchmark objects. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Registered string ID type for benchmark events. */
                using Id = StringRegistry<Benchmark>::Item;

                /** @brief A single timestamped benchmark entry. */
                struct Entry {
                        Id        id;           ///< @brief The event identifier.
                        TimeStamp timestamp;    ///< @brief When the event occurred.
                };

                /** @brief List of benchmark entries. */
                using EntryList = promeki::List<Entry>;

                /** @brief Constructs an empty Benchmark. */
                Benchmark() = default;

                /**
                 * @brief Appends an entry with the current time.
                 * @param id The event identifier.
                 */
                void stamp(Id id) {
                        _entries.pushToBack({id, TimeStamp::now()});
                        return;
                }

                /**
                 * @brief Appends an entry with an explicit timestamp.
                 * @param id The event identifier.
                 * @param ts The timestamp to record.
                 */
                void stamp(Id id, const TimeStamp &ts) {
                        _entries.pushToBack({id, ts});
                        return;
                }

                /**
                 * @brief Returns the ordered list of entries.
                 * @return Const reference to the entry list.
                 */
                const EntryList &entries() const { return _entries; }

                /**
                 * @brief Returns the number of entries.
                 * @return Entry count.
                 */
                size_t size() const { return _entries.size(); }

                /**
                 * @brief Returns true if there are no entries.
                 * @return True if empty.
                 */
                bool isEmpty() const { return _entries.isEmpty(); }

                /** @brief Removes all entries. */
                void clear() { _entries.clear(); return; }

                /**
                 * @brief Computes the duration between the first occurrences of two IDs.
                 * @param fromId The starting event.
                 * @param toId   The ending event.
                 * @return Duration in seconds, or 0.0 if either ID is not found.
                 */
                double duration(Id fromId, Id toId) const;

        private:
                EntryList _entries;
};

PROMEKI_NAMESPACE_END
