/**
 * @file      frame.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/metadata.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class Frame {
    public:
        Frame() : d(SharedPtr<Data>::create()) { }

        const List<Image> &imageList() const { return d->imageList; }
        List<Image> &imageList() { return d.modify()->imageList; }

        const List<Audio> &audioList() const { return d->audioList; }
        List<Audio> &audioList() { return d.modify()->audioList; }

        const Metadata &metadata() const { return d->metadata; }
        Metadata &metadata() { return d.modify()->metadata; }

        int referenceCount() const { return d.referenceCount(); }

    private:
        class Data {
            PROMEKI_SHARED_FINAL(Data)
            public:
                List<Image>     imageList;
                List<Audio>     audioList;
                Metadata        metadata;
        };

        SharedPtr<Data> d;
};

PROMEKI_NAMESPACE_END


