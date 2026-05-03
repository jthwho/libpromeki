/**
 * @file      httpheaders.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Case-insensitive HTTP header collection.
 * @ingroup network
 *
 * HTTP header field names are case-insensitive on the wire (RFC 9110
 * §5.1) but most consumers expect to round-trip the casing chosen by
 * the producer.  HttpHeaders satisfies both: lookup is case-insensitive
 * (lower-cased internally), and serialization writes back the
 * canonical-case form recorded when the header was first @ref set or
 * @ref add'd.
 *
 * Multi-valued headers (e.g. @c Set-Cookie or comma-separable lists
 * like @c Vary) are supported via @ref add — repeated calls accumulate
 * values in registration order, queryable as a @ref StringList via
 * @ref values.  The single-value @ref value accessor returns the
 * first value (sufficient for almost every header).
 *
 * The class is a Shareable data object: it follows libpromeki's
 * value-with-COW pattern via @c PROMEKI_SHARED_FINAL, exposes
 * @c ::Ptr, and is safe to pass by value cheaply.
 *
 * @par Example
 * @code
 * HttpHeaders h;
 * h.set("Content-Type", "application/json");
 * h.add("Set-Cookie", "session=abc; Path=/");
 * h.add("Set-Cookie", "csrf=xyz; Path=/");
 *
 * h.value("content-type");           // "application/json" (case-insensitive)
 * h.values("set-cookie").count();    // 2
 *
 * h.forEach([](const String &name, const String &value) {
 *     // name is in canonical case ("Content-Type"), not lower-case
 * });
 * @endcode
 */
class HttpHeaders {
                PROMEKI_SHARED_FINAL(HttpHeaders)
        public:
                /** @brief Shared pointer type for HttpHeaders. */
                using Ptr = SharedPtr<HttpHeaders>;

                /** @brief Plain value list (e.g. for queues of headers). */
                using List = ::promeki::List<HttpHeaders>;

                /** @brief List of shared pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Constructs an empty header set. */
                HttpHeaders() = default;

                /**
                 * @brief Replaces all values for @p name with a single value.
                 *
                 * Any previously-stored entries for @p name (regardless
                 * of case) are removed.  The canonical case for future
                 * iteration becomes the casing in @p name.
                 */
                void set(const String &name, const String &value);

                /**
                 * @brief Appends a value for @p name.
                 *
                 * If @p name has not yet been seen, stores the casing
                 * used here as the canonical case.  Otherwise the
                 * canonical case is unchanged and the new value is
                 * appended after any existing values.
                 */
                void add(const String &name, const String &value);

                /**
                 * @brief Removes all values for @p name (case-insensitive).
                 *
                 * No-op if @p name is not present.
                 */
                void remove(const String &name);

                /** @brief True if at least one value is present for @p name. */
                bool contains(const String &name) const;

                /**
                 * @brief Returns the first value for @p name, or @p defaultValue.
                 *
                 * Lookup is case-insensitive.  When multiple values are
                 * present, the value added first wins; use @ref values
                 * to get the full list.
                 */
                String value(const String &name, const String &defaultValue = String()) const;

                /**
                 * @brief Returns every value stored for @p name in arrival order.
                 *
                 * Empty list when @p name is absent.
                 */
                StringList values(const String &name) const;

                /**
                 * @brief Removes every header.
                 */
                void clear();

                /** @brief Number of stored values (counting duplicates). */
                int count() const;

                /** @brief Whether the collection is empty. */
                bool isEmpty() const { return count() == 0; }

                /**
                 * @brief Iterates over every (name, value) pair.
                 *
                 * The @c name passed to @p func is the canonical casing
                 * recorded at registration time.  Iteration order is
                 * the order of first @ref set / @ref add per name; for
                 * multi-valued headers, all values for one name are
                 * emitted contiguously in arrival order.
                 */
                void forEach(std::function<void(const String &name, const String &value)> func) const;

                /** @brief Equality on the full set of (name, value) pairs. */
                bool operator==(const HttpHeaders &other) const;

                /** @brief Inverse of @ref operator==. */
                bool operator!=(const HttpHeaders &other) const { return !(*this == other); }

                /**
                 * @brief Lower-cases @p s using ASCII rules.
                 *
                 * Public because both the parser and the router need
                 * the same key-folding rule (RFC 9110: ASCII case fold,
                 * not locale-aware Unicode fold).  Exposed as a helper
                 * to avoid duplicating the loop and to let callers
                 * pre-fold a key when looking up many headers in a row.
                 */
                static String foldName(const String &s);

        private:
                struct Entry {
                                using List = ::promeki::List<Entry>;
                                String name; ///< Canonical case as first stored.
                                String value;
                };

                // Parallel index: lower-case key -> indices in _entries.
                // Stored as a small associative container; we intentionally
                // use a plain list-of-pairs because typical header counts
                // are well under 30 and the constant-factor wins.
                struct KeyBucket {
                                using List = ::promeki::List<KeyBucket>;
                                using IndexList = ::promeki::List<size_t>;
                                String    lower;
                                IndexList indices;
                };

                // Per-name lookup uses the lower-cased name as the
                // index into _index, which holds a list of indices
                // into _entries.  This preserves arrival order across
                // names and supports duplicates without a multimap.
                Entry::List     _entries;
                KeyBucket::List _index;

                size_t findBucket(const String &lower) const;
                size_t getOrCreateBucket(const String &lower);
};

PROMEKI_NAMESPACE_END
