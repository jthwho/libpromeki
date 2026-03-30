/**
 * @file      framedemuxnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/framedemuxnode.h>
#include <promeki/proav/medianodeconfig.h>
#include <promeki/proav/frame.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(FrameDemuxNode)

FrameDemuxNode::FrameDemuxNode(ObjectBase *parent) : MediaNode(parent) {
        setName("FrameDemuxNode");
        addSink(MediaSink::Ptr::create("input", ContentNone));
        addSource(MediaSource::Ptr::create("image", ContentVideo));
        addSource(MediaSource::Ptr::create("audio", ContentAudio));
}

MediaNodeConfig FrameDemuxNode::defaultConfig() const {
        return MediaNodeConfig("FrameDemuxNode", "");
}

BuildResult FrameDemuxNode::build(const MediaNodeConfig &config) {
        (void)config;
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }
        setState(Configured);
        return result;
}

void FrameDemuxNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)inputIndex;

        // Extract and deliver images
        if(!frame->imageList().isEmpty()) {
                Frame::Ptr imgFrame = Frame::Ptr::create();
                imgFrame.modify()->imageList() = frame->imageList();
                imgFrame.modify()->metadata() = frame->metadata();
                if(frame->benchmark().isValid()) {
                        imgFrame.modify()->setBenchmark(frame->benchmark());
                }
                deliveries.pushToBack({0, imgFrame});
        }

        // Extract and deliver audio
        if(!frame->audioList().isEmpty()) {
                Frame::Ptr audioFrame = Frame::Ptr::create();
                audioFrame.modify()->audioList() = frame->audioList();
                audioFrame.modify()->metadata() = frame->metadata();
                if(frame->benchmark().isValid()) {
                        audioFrame.modify()->setBenchmark(frame->benchmark());
                }
                deliveries.pushToBack({1, audioFrame});
        }

        // Invalidate input frame so base class doesn't also deliver it
        frame = Frame::Ptr();
        return;
}

PROMEKI_NAMESPACE_END
