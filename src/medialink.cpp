/**
 * @file      medialink.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/medialink.h>
#include <promeki/proav/medianode.h>

PROMEKI_NAMESPACE_BEGIN

MediaNode *MediaLink::sourceNode() const {
        if(!_source.isValid()) return nullptr;
        return _source->node();
}

MediaNode *MediaLink::sinkNode() const {
        if(!_sink.isValid()) return nullptr;
        return _sink->node();
}

Error MediaLink::deliver(Frame::Ptr frame) const {
        if(!_sink.isValid()) return Error(Error::Invalid);

        MediaNode *sink = sinkNode();
        if(sink == nullptr) return Error(Error::Invalid);

        // If source and sink are the same media type, pass through directly
        MediaPort::MediaType srcType = _source->mediaType();
        MediaPort::MediaType sinkType = _sink->mediaType();

        if(srcType == sinkType) {
                sink->enqueueInput(std::move(frame));
                return Error(Error::Ok);
        }

        // Frame -> Image extraction: create a Frame with just the image data
        if(srcType == MediaPort::Frame && sinkType == MediaPort::Image) {
                Frame::Ptr imgFrame = Frame::Ptr::create();
                imgFrame.modify()->imageList() = frame->imageList();
                imgFrame.modify()->metadata() = frame->metadata();
                sink->enqueueInput(std::move(imgFrame));
                return Error(Error::Ok);
        }

        // Frame -> Audio extraction: create a Frame with just the audio data
        if(srcType == MediaPort::Frame && sinkType == MediaPort::Audio) {
                Frame::Ptr audioFrame = Frame::Ptr::create();
                audioFrame.modify()->audioList() = frame->audioList();
                audioFrame.modify()->metadata() = frame->metadata();
                sink->enqueueInput(std::move(audioFrame));
                return Error(Error::Ok);
        }

        return Error(Error::Invalid);
}

bool MediaLink::isValid() const {
        if(!_source.isValid() || !_sink.isValid()) return false;
        return _source->isCompatible(*_sink);
}

PROMEKI_NAMESPACE_END
