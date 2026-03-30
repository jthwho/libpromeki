/**
 * @file      proav/mediapipelineconfig.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>
#include <promeki/proav/medianodeconfig.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Holds the complete configuration for building a MediaPipeline.
 * @ingroup proav_pipeline
 *
 * MediaPipelineConfig contains a list of MediaNodeConfig objects, one per
 * node to create.  The list need not be in any particular order; the
 * pipeline determines dependency order during build().
 *
 * @par Example
 * @code
 * MediaPipelineConfig pipeConfig;
 *
 * MediaNodeConfig src("TestPatternNode", "source");
 * src.set("width", Variant(uint32_t(1920)));
 * pipeConfig.addNode(src);
 *
 * MediaNodeConfig sink("RtpVideoSinkNode", "output");
 * sink.addConnection("source[0]");
 * pipeConfig.addNode(sink);
 *
 * auto result = pipeline.build(pipeConfig);
 * if(result.isError()) { ... }
 * @endcode
 */
class MediaPipelineConfig {
        public:
                /** @brief Default constructor. */
                MediaPipelineConfig() = default;

                /**
                 * @brief Adds a node configuration.
                 * @param config The node configuration to add.
                 */
                void addNode(const MediaNodeConfig &config);

                /**
                 * @brief Adds a node configuration (move overload).
                 * @param config The node configuration to add.
                 */
                void addNode(MediaNodeConfig &&config);

                /** @brief Returns the list of node configurations. */
                const MediaNodeConfig::List &nodes() const { return _nodes; }

                /** @brief Returns the number of node configurations. */
                size_t size() const { return _nodes.size(); }

                /** @brief Returns true if there are no node configurations. */
                bool isEmpty() const { return _nodes.isEmpty(); }

                /** @brief Removes all node configurations. */
                void clear() { _nodes.clear(); }

                /**
                 * @brief Finds a node configuration by name.
                 * @param name The node name to search for.
                 * @return Pointer to the matching config, or nullptr if not found.
                 */
                const MediaNodeConfig *node(const String &name) const;

                /**
                 * @brief Enables or disables benchmarking on all nodes.
                 * @param enabled True to enable benchmarking on every node.
                 */
                void setBenchmarkEnabled(bool enabled);

                /**
                 * @brief Validates the configuration.
                 *
                 * Checks that all node configs are valid, all names are unique,
                 * and all connection targets reference node names that exist
                 * in this configuration.
                 *
                 * @param err Optional error output for the first problem found.
                 * @return True if the configuration is valid.
                 */
                bool isValid(Error *err = nullptr) const;

        private:
                MediaNodeConfig::List _nodes;
};

PROMEKI_NAMESPACE_END
