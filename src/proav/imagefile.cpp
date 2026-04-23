/**
 * @file      imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/image.h>
#include <promeki/videopacket.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

ImageFile::ImageFile(int id) :
        _io(ImageFileIO::lookup(id))
{

}

Error ImageFile::load(const MediaConfig &config) {
        Error err = _io->load(*this, config);
        if(err.isError()) return err;

        // Attach a VideoPacket to every compressed Image the backend
        // loaded — plane 0 already holds the encoded bitstream, so we
        // just wrap it as a zero-copy packet and hand the ownership
        // over to the Image.  A downstream @ref VideoDecoder consumes
        // @ref Image::packet to decode intraframe bitstreams (JPEG,
        // JPEG XS) read from disk.  Every packet is flagged Keyframe
        // because intraframe codecs have no inter-frame state.
        for(Image::Ptr &imgPtr : _frame.imageList()) {
                if(!imgPtr.isValid() || !imgPtr->isCompressed()) continue;
                if(imgPtr->packet().isValid()) continue;
                const Buffer::Ptr &plane = imgPtr->plane(0);
                if(!plane.isValid() || plane->size() == 0) continue;
                auto pkt = VideoPacket::Ptr::create(plane, imgPtr->pixelFormat());
                pkt.modify()->addFlag(VideoPacket::Keyframe);
                imgPtr.modify()->setPacket(std::move(pkt));
        }
        return Error::Ok;
}

Error ImageFile::save(const MediaConfig &config) {
        return _io->save(*this, config);
}

PROMEKI_NAMESPACE_END


