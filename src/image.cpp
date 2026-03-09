/**
 * @file      image.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/image.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

Image::Data::Data(const ImageDesc &desc, const MemSpace &ms) : desc(desc) {
        allocate(desc, ms);
}

void Image::Data::clear() {
        desc = ImageDesc();
        planeList.clear();
        return;
}

bool Image::Data::allocate(const ImageDesc &desc, const MemSpace &ms) {
        const PixelFormat *pixfmt = desc.pixelFormat();
        int planes = pixfmt->planeCount();
        Buffer::List list(planes);
        for(int i = 0; i < planes; i++) {
                size_t size = pixfmt->planeSize(i, desc);
                Buffer b = Buffer(size, Buffer::DefaultAlign, ms);
                if(!b.isValid()) {
                        promekiErr("Image(%s) plane %d allocate failed", desc.toString().cstr(), i);
                        return false;
                }
                list[i] = b;
        }
        planeList = list;
        return true;
}

Image Image::Data::convert(PixelFormat::ID pixelFormat, const Metadata &metadata) const {
    return Image();
}

PROMEKI_NAMESPACE_END

