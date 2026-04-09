/**
 * @file      mediaplay/sidecar.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "sidecar.h"

#include <cstdio>

#include <promeki/imagedesc.h>
#include <promeki/imgseq.h>
#include <promeki/numname.h>

using namespace promeki;

namespace mediaplay {

bool outputIsSequenceMask(const String &path) {
        FilePath fp(path);
        NumName n = NumName::fromMask(fp.fileName());
        return n.isValid();
}

FilePath deriveSidecarPath(const String &maskPath) {
        FilePath fp(maskPath);
        NumName n = NumName::fromMask(fp.fileName());
        String stem = n.prefix();
        while(!stem.isEmpty()) {
                char c = stem[stem.size() - 1];
                if(c != '_' && c != '.' && c != '-' && c != ' ') break;
                stem = stem.left(stem.size() - 1);
        }
        if(stem.isEmpty()) stem = "sequence";
        FilePath dir = fp.parent();
        if(dir.isEmpty()) dir = FilePath(".");
        return dir / (stem + ".imgseq");
}

Error writeImgSeqSidecar(const FilePath &path,
                         const String &maskPath,
                         const MediaDesc &mediaDesc,
                         int seqHead,
                         int64_t frameCount) {
        NumName pattern = NumName::fromMask(FilePath(maskPath).fileName());
        if(!pattern.isValid()) return Error::Invalid;
        if(frameCount <= 0) {
                fprintf(stderr,
                        "Warning: skipping .imgseq sidecar — no frames written.\n");
                return Error::Ok;
        }
        ImgSeq seq;
        seq.setName(pattern);
        seq.setHead(static_cast<size_t>(seqHead));
        seq.setTail(static_cast<size_t>(seqHead + frameCount - 1));
        if(mediaDesc.frameRate().isValid()) {
                seq.setFrameRate(mediaDesc.frameRate());
        }
        if(!mediaDesc.imageList().isEmpty()) {
                const ImageDesc &id = mediaDesc.imageList()[0];
                seq.setVideoSize(id.size());
                seq.setPixelDesc(id.pixelDesc());
        }
        return seq.save(path);
}

} // namespace mediaplay
