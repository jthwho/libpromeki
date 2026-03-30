/**
 * @file      proav/medianodeconfig.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>
#include <promeki/core/variant.h>
#include <promeki/core/map.h>
#include <promeki/core/error.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Configuration descriptor for creating and configuring a MediaNode.
 * @ingroup proav_pipeline
 *
 * MediaNodeConfig holds all the information needed by MediaPipeline::build()
 * to instantiate a MediaNode via the factory, wire its connections, and
 * pass node-specific options to MediaNode::build().
 *
 * All configuration is stored in a single Map<String, Variant>.  Three
 * keys are reserved for the pipeline:
 *  - **Name** (KeyName) -- unique name for the node within its pipeline.
 *  - **Type** (KeyType) -- registered MediaNode type name (used by the factory).
 *  - **Connections** (KeyConnections) -- a StringList where each entry at
 *    index @e i specifies what connects to the node's input sink at index @e i.
 *
 * Connection string formats:
 *  - `"NodeName[Index]"` -- source at the given index on the named node.
 *  - `"NodeName.SourceName"` -- source with the given name on the named node.
 *  - `"NodeName"` -- shorthand for `"NodeName[0]"` (first source).
 *
 * All remaining keys are node-specific options.  When MediaPipeline::build()
 * calls MediaNode::build(), the full MediaNodeConfig is passed; the node
 * ignores the standard pipeline-level keys and reads only its own options.
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("TestPatternNode", "pattern1");
 * cfg.addConnection(""); // sink 0 has no connection (source node)
 * cfg.set("width", Variant(uint32_t(1920)));
 * cfg.set("height", Variant(uint32_t(1080)));
 * @endcode
 */
class MediaNodeConfig {
        public:
                using List = promeki::List<MediaNodeConfig>;

                static const String KeyName;            ///< @brief Key for the node name ("Name").
                static const String KeyType;            ///< @brief Key for the node type ("Type").
                static const String KeyConnections;     ///< @brief Key for the connections list ("Connections").
                static const String KeyBenchmarkEnabled; ///< @brief Key for the benchmark enable flag ("BenchmarkEnabled").

                /**
                 * @brief Result of parsing a connection string.
                 */
                struct ParsedConnection {
                        String nodeName;                ///< @brief Name of the source node.
                        int sourceIndex = -1;           ///< @brief Source index (>= 0 if index form was used).
                        String sourceName;              ///< @brief Source name (non-empty if name form was used).

                        /** @brief Returns true if the parsed connection is valid. */
                        bool isValid() const { return !nodeName.isEmpty() && (sourceIndex >= 0 || !sourceName.isEmpty()); }
                };

                /** @brief Default constructor. */
                MediaNodeConfig() = default;

                /**
                 * @brief Constructs a MediaNodeConfig with a type and name.
                 * @param type The registered MediaNode type name.
                 * @param name The unique node name within the pipeline.
                 */
                MediaNodeConfig(const String &type, const String &name);

                /** @brief Returns the node name. */
                String name() const;

                /** @brief Sets the node name. */
                void setName(const String &name);

                /** @brief Returns the node type. */
                String type() const;

                /** @brief Sets the node type. */
                void setType(const String &type);

                /** @brief Returns the connections list. */
                StringList connections() const;

                /** @brief Sets the connections list. */
                void setConnections(const StringList &connections);

                /**
                 * @brief Appends a connection string.
                 * @param connection The connection string to add.
                 */
                void addConnection(const String &connection);

                /**
                 * @brief Sets an option value.
                 * @param key   The option key.
                 * @param value The option value.
                 */
                void set(const String &key, const Variant &value);

                /**
                 * @brief Gets an option value.
                 * @param key          The option key.
                 * @param defaultValue Value to return if the key is not found.
                 * @return The option value, or @p defaultValue if not found.
                 */
                Variant get(const String &key, const Variant &defaultValue = Variant()) const;

                /**
                 * @brief Returns true if the options map contains the given key.
                 * @param key The option key to check.
                 */
                bool contains(const String &key) const;

                /**
                 * @brief Removes an option.
                 * @param key The option key to remove.
                 * @return True if the key was found and removed.
                 */
                bool remove(const String &key);

                /** @brief Returns the full options map (including standard keys). */
                const Map<String, Variant> &options() const { return _options; }

                /**
                 * @brief Returns true if benchmarking is enabled for this node.
                 * @return True if enabled, false otherwise (default: false).
                 */
                bool benchmarkEnabled() const;

                /**
                 * @brief Enables or disables benchmarking for this node.
                 * @param enabled True to enable benchmarking.
                 */
                void setBenchmarkEnabled(bool enabled);

                /**
                 * @brief Returns true if both name and type are set.
                 */
                bool isValid() const;

                /**
                 * @brief Returns true if the given key is a standard pipeline-level key.
                 * @param key The key to check.
                 */
                static bool isStandardKey(const String &key);

                /**
                 * @brief Parses a connection string into its components.
                 *
                 * Supported formats:
                 *  - `"NodeName[Index]"` -- index form.
                 *  - `"NodeName.SourceName"` -- name form.
                 *  - `"NodeName"` -- shorthand for index 0.
                 *
                 * @param connStr The connection string to parse.
                 * @param err     Optional error output.
                 * @return A ParsedConnection struct.
                 */
                static ParsedConnection parseConnection(const String &connStr, Error *err = nullptr);

        private:
                Map<String, Variant>    _options;
};

PROMEKI_NAMESPACE_END
