/**
 * @file      frame.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/shareddata.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/metadata.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class Frame {
    public:
        Frame() : d(new Data) { }
        ~Frame() { }

    private:
        class Data : public SharedData {
            public:
                List<Image>     imageList;
                List<Audio>     audioList;
                Metadata        metadata;
        };

        SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END


