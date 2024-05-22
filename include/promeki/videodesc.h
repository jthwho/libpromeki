/**
 * @file      videodesc.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/shareddata.h>
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

        VideoDesc() : d(new Data) { }

        bool isValid() const { return d->isValid(); }

        const FrameRate &frameRate() const { return d->frameRate; }
        void setFrameRate(const FrameRate &val) { d->frameRate = val; }

        const ImageDescList &imageList() const { return d->imageList; }
        ImageDescList &imageList() { return d->imageList; }

        const AudioDescList &audioList() const { return d->audioList; }
        AudioDescList &audioList() { return d->audioList; }

        const Metadata &metadata() const { return d->metadata; }
        Metadata &metadata() { return d->metadata; }

    private:
        class Data : public SharedData {
            public:
                FrameRate           frameRate;
                ImageDescList       imageList;
                AudioDescList       audioList;
                Metadata            metadata;

                // A video description is valid if it has a valid frame rate and at least one image or audio description.
                bool isValid() const { return frameRate.isValid() && (imageList.size() > 0 || audioList.size() > 0); }
        };
        SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END

