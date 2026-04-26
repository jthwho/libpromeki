/**
 * @file      imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

ImageFile::ImageFile(int id) : _io(ImageFileIO::lookup(id)) {}

Error ImageFile::load(const MediaConfig &config) {
        Error err = _io->load(*this, config);
        if (err.isError()) return err;

        // Stamp the Keyframe flag onto every compressed video
        // payload the backend loaded — intraframe codecs (JPEG,
        // JPEG XS, …) have no inter-frame state, so every loaded
        // access unit is a self-contained decode entry point.
        // Downstream readers (Frame::isSafeCutPoint, the raw-
        // bitstream sink, the inspector) consult the payload's
        // Keyframe flag directly.
        for (MediaPayload::Ptr &payloadPtr : _frame.payloadList()) {
                if (!payloadPtr.isValid()) continue;
                if (!payloadPtr->as<CompressedVideoPayload>()) continue;
                payloadPtr.modify()->addFlag(MediaPayload::Keyframe);
        }
        return Error::Ok;
}

Error ImageFile::save(const MediaConfig &config) {
        return _io->save(*this, config);
}

PROMEKI_NAMESPACE_END
