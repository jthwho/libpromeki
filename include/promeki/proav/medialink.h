/**
 * @file      proav/medialink.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/error.h>
#include <promeki/proav/mediaport.h>
#include <promeki/proav/frame.h>

PROMEKI_NAMESPACE_BEGIN

class MediaNode;

/**
 * @brief Connects an output port to an input port in a media pipeline.
 * @ingroup proav_pipeline
 *
 * MediaLink delivers frames from a source node's output port to a sink
 * node's input queue. When the source port is a Frame port and the sink
 * port is an Image or Audio port, the link extracts the relevant sub-frame
 * data automatically.
 *
 * MediaLink does not buffer — the buffering is in the sink node's input
 * queue (see MediaNode).
 */
class MediaLink {
        PROMEKI_SHARED_FINAL(MediaLink)
        public:
                /** @brief Shared pointer type for MediaLink. */
                using Ptr = SharedPtr<MediaLink>;

                /** @brief Plain value list of MediaLink objects. */
                using List = promeki::List<MediaLink>;

                /** @brief List of shared pointers to MediaLink. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an empty (disconnected) link. */
                MediaLink() = default;

                /**
                 * @brief Constructs a link between the given source and sink ports.
                 * @param source The output port.
                 * @param sink   The input port.
                 */
                MediaLink(MediaPort::Ptr source, MediaPort::Ptr sink) :
                        _source(std::move(source)), _sink(std::move(sink)) { }

                /** @brief Returns the source (output) port. */
                const MediaPort::Ptr &source() const { return _source; }

                /** @brief Returns the sink (input) port. */
                const MediaPort::Ptr &sink() const { return _sink; }

                /** @brief Returns the node that owns the source port, or nullptr. */
                MediaNode *sourceNode() const;

                /** @brief Returns the node that owns the sink port, or nullptr. */
                MediaNode *sinkNode() const;

                /**
                 * @brief Delivers a frame from the source to the sink node's input queue.
                 *
                 * Handles Frame-to-Image and Frame-to-Audio extraction if the
                 * source and sink port types differ.
                 *
                 * @param frame The frame to deliver.
                 * @return Error::Ok on success, or an error if delivery fails.
                 */
                Error deliver(Frame::Ptr frame) const;

                /**
                 * @brief Returns true if the source and sink ports are compatible.
                 * @return true if the link's ports can be connected.
                 */
                bool isValid() const;

        private:
                MediaPort::Ptr  _source;
                MediaPort::Ptr  _sink;
};

PROMEKI_NAMESPACE_END
