/**
 * @file      benchparams.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Generic parameter bag shared across promeki-bench suites.
 *
 * Cases read their configuration from a process-wide BenchParams
 * singleton populated by `main.cpp` from `-p key=value` / `-p key+=value`
 * command-line arguments.  The bag is purposely untyped on the
 * storage side — every value is a StringList — so suites can decide
 * whether a key is scalar (`csc.width`), boolean (`csc.verbose`), or
 * list-valued (`csc.src`, `csc.dst`) without teaching the driver
 * about the distinction.
 *
 * Keys are conventionally namespaced by suite (`csc.width`,
 * `network.packetSize`, `csc.config.CscPath`, …) so there are no
 * collisions across suites.  The driver never interprets key names —
 * it just stores them.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/map.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

        /**
 * @brief Process-wide generic parameter store for promeki-bench cases.
 *
 * Every value is stored as a StringList.  `set()` replaces the list
 * (last-write-wins scalar semantics), `append()` pushes to the back
 * (list semantics).  Typed getters convert on the way out, using
 * `String::toInt` / `String::toDouble` / `String::toBool`.
 */
        class BenchParams {
                public:
                        /** @brief Constructs an empty parameter bag. */
                        BenchParams() = default;

                        /**
                 * @brief Replaces the value list for @p key with a single entry.
                 *
                 * Equivalent to `clear(key); append(key, value)`.  Callers
                 * that want scalar semantics use this; callers that want
                 * list semantics use `append()`.
                 *
                 * @param key   The parameter name.
                 * @param value The new value.
                 */
                        void set(const String &key, const String &value);

                        /**
                 * @brief Pushes @p value onto the back of the value list for @p key.
                 * @param key   The parameter name.
                 * @param value The value to append.
                 */
                        void append(const String &key, const String &value);

                        /**
                 * @brief Removes every value for @p key.
                 * @param key The parameter name.
                 */
                        void clear(const String &key);

                        /** @brief Removes every stored parameter. */
                        void clearAll();

                        /**
                 * @brief Returns true if @p key has at least one stored value.
                 * @param key The parameter name.
                 */
                        bool contains(const String &key) const;

                        /** @brief Returns true if no parameters are stored. */
                        bool isEmpty() const;

                        /**
                 * @brief Returns the last value for @p key, or @p def if unset.
                 *
                 * Last-value semantics match the scalar use case where the
                 * same key may have been written multiple times and only
                 * the final write should win.
                 *
                 * @param key The parameter name.
                 * @param def Fallback value if the key has no stored values.
                 */
                        String getString(const String &key, const String &def = String()) const;

                        /**
                 * @brief Returns the full value list for @p key, or empty if unset.
                 * @param key The parameter name.
                 */
                        StringList getStringList(const String &key) const;

                        /**
                 * @brief Returns the last value parsed as int, or @p def on failure.
                 * @param key The parameter name.
                 * @param def Fallback value if the key is missing or unparseable.
                 */
                        int getInt(const String &key, int def = 0) const;

                        /**
                 * @brief Returns the last value parsed as double, or @p def on failure.
                 * @param key The parameter name.
                 * @param def Fallback value if the key is missing or unparseable.
                 */
                        double getDouble(const String &key, double def = 0.0) const;

                        /**
                 * @brief Returns the last value parsed as bool, or @p def on failure.
                 *
                 * Uses `String::toBool()` semantics (accepts `"true"` /
                 * `"false"` / `"1"` / `"0"` / etc.).
                 *
                 * @param key The parameter name.
                 * @param def Fallback value if the key is missing or unparseable.
                 */
                        bool getBool(const String &key, bool def = false) const;

                        /**
                 * @brief Invokes @p func for every stored `(key, StringList)` entry.
                 * @tparam Func Callable with signature `void(const String &, const StringList &)`.
                 * @param  func The function to invoke for each entry.
                 */
                        template <typename Func> void forEach(Func &&func) const {
                                for (const auto &[k, v] : _values) func(k, v);
                        }

                        /**
                 * @brief Parses a single `-p` argument and applies it to the store.
                 *
                 * Accepted forms:
                 *   - `key=value`  — `set(key, value)` (scalar; last-write-wins)
                 *   - `key+=value` — `append(key, value)` (list append)
                 *   - `key`        — `set(key, "")` (flag / empty-value)
                 *
                 * Returns `Error::InvalidArgument` if the argument is
                 * structurally invalid (empty key, etc.), `Error::Ok`
                 * otherwise.
                 *
                 * @param arg The raw command-line value.
                 */
                        Error parseArg(const String &arg);

                private:
                        Map<String, StringList> _values;
        };

        /**
 * @brief Returns the process-wide BenchParams singleton.
 *
 * Lazy-constructed on first call.  Main populates it before calling
 * case-registration hooks; case bodies read from it at run time.
 */
        BenchParams &benchParams();

} // namespace benchutil
PROMEKI_NAMESPACE_END
