/**
 * @file      framedemuxnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/framedemuxnode.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/proav/audio.h>
#include <promeki/core/metadata.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(FrameDemuxNode)

FrameDemuxNode::FrameDemuxNode(ObjectBase *parent) : MediaNode(parent) {
        setName("FrameDemuxNode");
        auto input = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Frame);
        auto imageOut = MediaPort::Ptr::create("image", MediaPort::Output, MediaPort::Image);
        auto audioOut = MediaPort::Ptr::create("audio", MediaPort::Output, MediaPort::Audio);
        addInputPort(input);
        addOutputPort(imageOut);
        addOutputPort(audioOut);
}

Error FrameDemuxNode::configure() {
        if(state() != Idle) return Error(Error::Invalid);

        // Pass through descriptors from input to outputs
        MediaPort::Ptr input = inputPort(0);
        MediaPort::Ptr imageOut = outputPort(0);
        MediaPort::Ptr audioOut = outputPort(1);

        if(input->videoDesc().isValid()) {
                if(!input->videoDesc().imageList().isEmpty()) {
                        imageOut.modify()->setImageDesc(input->videoDesc().imageList()[0]);
                }
                if(!input->videoDesc().audioList().isEmpty()) {
                        audioOut.modify()->setAudioDesc(input->videoDesc().audioList()[0]);
                }
        }
        if(input->audioDesc().isValid()) {
                audioOut.modify()->setAudioDesc(input->audioDesc());
        }

        setState(Configured);
        return Error(Error::Ok);
}

void FrameDemuxNode::process() {
        Frame::Ptr frame = dequeueInput();
        if(!frame.isValid()) return;

        // Extract and deliver images
        if(!frame->imageList().isEmpty()) {
                Frame::Ptr imgFrame = Frame::Ptr::create();
                imgFrame.modify()->imageList() = frame->imageList();
                imgFrame.modify()->metadata() = frame->metadata();
                deliverOutput(0, imgFrame);
        }

        // Extract and deliver audio
        if(!frame->audioList().isEmpty()) {
                Frame::Ptr audioFrame = Frame::Ptr::create();
                audioFrame.modify()->audioList() = frame->audioList();
                audioFrame.modify()->metadata() = frame->metadata();
                deliverOutput(1, audioFrame);
        }
        return;
}

PROMEKI_NAMESPACE_END
