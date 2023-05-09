/*****************************************************************************
 * pixelformat.cpp
 * April 29, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <cstring>
#include <promeki/pixelformat.h>
#include <promeki/structdatabase.h>
#include <promeki/image.h>

PROMEKI_NAMESPACE_BEGIN

#define DEFINE_FORMAT(fmt) \
        .id = PixelFormat::fmt, \
        .name = PROMEKI_STRINGIFY(fmt)

inline size_t stride_RGBA8(const Size2D &size) {
        return size.width() * 4;
}

inline size_t size_RGBA8(const Size2D &size) {
        return stride_RGBA8(size) * size.height();
}

inline bool fill_RGBA8(const Image &img, const PixelFormat::Comp *comps) {
        const Size2D &size = img.size();
        size_t stride = stride_RGBA8(size);
        uint8_t *line0 = static_cast<uint8_t *>(img.plane().data());
        uint8_t *buf;
        const uint8_t pixel[4] = { static_cast<uint8_t>(comps[0]),
                                   static_cast<uint8_t>(comps[1]),
                                   static_cast<uint8_t>(comps[2]),
                                   static_cast<uint8_t>(comps[3]) };
        // First, fill the first line w/ the pixel value
        buf = line0;
        for(int i = 0; i < size.width(); i++) {
                std::memcpy(buf, pixel, 4);
                buf += 4;
        }
        // Now, fill the rest of the lines from the first.
        buf = line0 + stride;
        for(int i = 1; i < size.height(); i++) {
                std::memcpy(buf, line0, stride);
                buf += stride;
        }
        return true;
}

inline size_t stride_RGB8(const Size2D &size) {
        return size.width() * 3;
}

inline size_t size_RGB8(const Size2D &size) {
        return stride_RGB8(size) * size.height();
}

inline size_t stride_RGB10(const Size2D &size) {
        return size.width() * 4;
}

inline size_t size_RGB10(const Size2D &size) {
        return stride_RGB10(size) * size.height();
}

inline size_t stride_YUV8_422(const Size2D &size) {
        // A YUV8 line should always be aligned to 4 bytes, since
        // each "block" contains 2 Y samples, 1 Cb, and 1 Cr.
        // This generally means in practice you'll never see odd
        // widths for this format, but we'll do the checking here
        // just in case.
        return PROMEKI_ALIGN_UP(size.width() * 2, 4);
}
        
inline size_t size_YUV8_422(const Size2D &size) {
        return stride_YUV8_422(size) * 2;
}

inline size_t stride_YUV10_422(const Size2D &size) {
        size_t w = size.width();
        return (w % 6) ? (w / 6 + 1) * 16 : (w / 6) * 16;
}

inline size_t size_YUV10_422(const Size2D &size) {
        return stride_YUV10_422(size) * size.height();
}

static StructDatabase<PixelFormat::ID, PixelFormat::Data> db = {
        { 
                DEFINE_FORMAT(Invalid),
                .desc = "Invalid",
                .sampling = PixelFormat::SamplingUndefined, 
                .pixelsPerBlock = 0,
                .bytesPerBlock = 0,
                .hasAlpha = false,
        },
        { 
                DEFINE_FORMAT(RGBA8),
                .desc = "8bit RGBA",
                .sampling = PixelFormat::Sampling444,
                .pixelsPerBlock = 1,
                .bytesPerBlock = 4, 
                .hasAlpha = true,
                .fourccList = { PROMEKI_FOURCC("RGBA") },
                .compList = {
                        { 0, .type = PixelFormat::CompRed,   .bits = 8 },
                        { 0, .type = PixelFormat::CompGreen, .bits = 8 },
                        { 0, .type = PixelFormat::CompBlue,  .bits = 8 },
                        { 0, .type = PixelFormat::CompAlpha, .bits = 8 }
                },
                .planeList = {
                        { .stride = stride_RGBA8, .size = size_RGBA8 }
                },
                .fill = fill_RGBA8
        },
        { 
                DEFINE_FORMAT(RGB8),
                .desc = "8bit RGB",
                .sampling = PixelFormat::Sampling444,
                .pixelsPerBlock = 1,
                .bytesPerBlock = 3,
                .hasAlpha = false,
                .fourccList = { PROMEKI_FOURCC("RGB2") },
                .compList = {
                        { 0, .type = PixelFormat::CompRed,   .bits = 8 },
                        { 0, .type = PixelFormat::CompGreen, .bits = 8 },
                        { 0, .type = PixelFormat::CompBlue,  .bits = 8 }
                },
                .planeList = {
                        { .stride = stride_RGB8, .size = size_RGB8 }
                }
        },
        { 
                DEFINE_FORMAT(RGB10),
                .desc = "10bit RGB",
                .sampling = PixelFormat::Sampling444,
                .pixelsPerBlock = 1,
                .bytesPerBlock = 4, 
                .hasAlpha = false,
                .fourccList = { PROMEKI_FOURCC("r210") },
                .compList = {
                        { 0, .type = PixelFormat::CompRed,   .bits = 10 },
                        { 0, .type = PixelFormat::CompGreen, .bits = 10 },
                        { 0, .type = PixelFormat::CompBlue,  .bits = 10 }
                },
                .planeList = {
                        { .stride = stride_RGB10, .size = size_RGB10 }
                }
        },
        { 
                DEFINE_FORMAT(YUV8_422),
                .desc = "8bit Y'CbCr 4:2:2",
                .sampling = PixelFormat::Sampling422,
                .pixelsPerBlock = 2,
                .bytesPerBlock = 4,
                .hasAlpha = false,
                .fourccList = {
                        PROMEKI_FOURCC("UYVY"),
                        PROMEKI_FOURCC("UYNV"),
                        PROMEKI_FOURCC("UYNY"),
                        PROMEKI_FOURCC("Y422"),
                        PROMEKI_FOURCC("HDYC"),
                        PROMEKI_FOURCC("AVUI"),
                        PROMEKI_FOURCC("uyv1"),
                        PROMEKI_FOURCC("2vuy"),
                        PROMEKI_FOURCC("2Vuy"),
                        PROMEKI_FOURCC("2Vu1")
                },
                .compList = {
                        { 0, .type = PixelFormat::CompY,  .bits = 8 },
                        { 0, .type = PixelFormat::CompCb, .bits = 8 },
                        { 0, .type = PixelFormat::CompCr, .bits = 8 }
                },
                .planeList = {
                        { .stride = stride_YUV8_422, .size = size_YUV8_422 }
                }
        },
        { 
                DEFINE_FORMAT(YUV10_422),
                .desc = "10bit Y'CbCr 4:2:2",
                .sampling = PixelFormat::Sampling422,
                .pixelsPerBlock = 6,
                .bytesPerBlock = 16, 
                .hasAlpha = false,
                .fourccList = { PROMEKI_FOURCC("v210") },
                .compList = {
                        { 0, .type = PixelFormat::CompY,  .bits = 10 },
                        { 0, .type = PixelFormat::CompCb, .bits = 10 },
                        { 0, .type = PixelFormat::CompCr, .bits = 10 }
                },
                .planeList = {
                        { .stride = stride_YUV10_422, .size = size_YUV10_422 }
                }
        },

};

const String &PixelFormat::formatName(ID id) {
        return db.get(id).name;
}

const PixelFormat::Data *PixelFormat::lookup(ID id) {
        return &db.get(id);
}

bool PixelFormat::fill(const Image &img, const Comp *c, size_t compCount) const {
        // FIXME: We don't check the image is in a memory space we can access.
        // Need to work out a way to dispatch to different fill() functions for different
        // memory spaces.
        if(comps() > compCount) {
                promekiErr("Attempting to fill image '%s', but not given enough comps. Given %d, expected %d",
                        img.desc().toString().cstr(), (int)compCount, (int)comps());
                return false;
        }
        if(id() != img.pixelFormat().id()) {
                promekiErr("Attempt to fill image '%s', but this is wrong pixel format (%s)",
                        img.desc().toString().cstr(), d->name.cstr());
                return false;
        }
        FillFunc func = d->fill;
        return func == nullptr ? false : func(img, c);
}

PROMEKI_NAMESPACE_END

