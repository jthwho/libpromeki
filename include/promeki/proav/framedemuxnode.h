/**
 * @file      proav/framedemuxnode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/proav/medianode.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Splits a Frame into separate Image and Audio streams.
 * @ingroup proav_pipeline
 *
 * Utility node with one Frame input port, one Image output port, and one
 * Audio output port. Extracts the image and audio from each incoming
 * Frame and delivers them to the respective output ports. Metadata from
 * the Frame is propagated to both output streams.
 *
 * If the input Frame has no audio, only the Image output is produced.
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("FrameDemuxNode", "demux");
 * @endcode
 */
class FrameDemuxNode : public MediaNode {
        PROMEKI_OBJECT(FrameDemuxNode, MediaNode)
        public:
                /**
                 * @brief Constructs a FrameDemuxNode.
                 * @param parent Optional parent object.
                 */
                FrameDemuxNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~FrameDemuxNode() = default;

                BuildResult build(const MediaNodeConfig &config) override;

        protected:
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override;
};

PROMEKI_NAMESPACE_END
