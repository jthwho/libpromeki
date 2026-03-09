/**
 * @file      videodesc.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/metadata.h>
#include <promeki/framerate.h>

PROMEKI_NAMESPACE_BEGIN

class VideoDesc {
    public:
        using ImageDescList = List<ImageDesc>;
        using AudioDescList = List<AudioDesc>;

        VideoDesc() : d(SharedPtr<Data>::create()) { }

        bool isValid() const { return d->isValid(); }

        const FrameRate &frameRate() const { return d->frameRate; }
        void setFrameRate(const FrameRate &val) { d.modify()->frameRate = val; }

        const ImageDescList &imageList() const { return d->imageList; }
        ImageDescList &imageList() { return d.modify()->imageList; }

        const AudioDescList &audioList() const { return d->audioList; }
        AudioDescList &audioList() { return d.modify()->audioList; }

        const Metadata &metadata() const { return d->metadata; }
        Metadata &metadata() { return d.modify()->metadata; }

        int referenceCount() const { return d.referenceCount(); }

    private:
        class Data {
            PROMEKI_SHARED_FINAL(Data)
            public:
                FrameRate           frameRate;
                ImageDescList       imageList;
                AudioDescList       audioList;
                Metadata            metadata;

                // A video description is valid if it has a valid frame rate and at least one image or audio description.
                bool isValid() const { return frameRate.isValid() && (imageList.size() > 0 || audioList.size() > 0); }
        };
        SharedPtr<Data> d;
};

PROMEKI_NAMESPACE_END

