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

#include <map>
#include <promeki/pixelformat.h>
#include <promeki/structdatabase.h>

namespace promeki {

#define DEFINE_FORMAT(fmt) \
        .id = PixelFormat::fmt, \
        .name = PROMEKI_STRINGIFY(fmt)

static StructDatabase<PixelFormat::ID, PixelFormat::Data> db = {
        { 
                DEFINE_FORMAT(Invalid),
                .desc = "Invalid",
                .comps = 0, 
                .ppab = 0, 
                .bpab = 0, 
                .planes = 1,
                .hasAlpha = false,
                .sampling = PixelSamplingUndefined, 
                .compBits = {0, 0, 0, 0}, 
                .compID = {PixelCompNone, PixelCompNone, PixelCompNone, PixelCompNone},
                .stride = [](const Size2D &, int) -> size_t { 
                        return 0;
                },
                .size = [](const Size2D &, int) -> size_t {
                        return 0;
                }
        },
        { 
                DEFINE_FORMAT(RGBA8),
                .desc = "8bit RGBA",
                .comps = 4,
                .ppab = 1,
                .bpab = 4, 
                .planes = 1,
                .hasAlpha = true,
                .sampling = PixelSampling444,
                .compBits = {8, 8, 8, 8}, 
                .compID = {PixelCompR, PixelCompG, PixelCompB, PixelCompA},
                .fourccList = { PROMEKI_FOURCC("RGBA") },
                .stride = [](const Size2D &size, int plane) -> size_t {
                        return size.width() * 4;
                },
                .size = [](const Size2D &size, int plane) -> size_t {
                        return size.area() * 4;
                }
        },
        { 
                DEFINE_FORMAT(RGB8),
                .desc = "8bit RGB",
                .comps = 3,
                .ppab = 1,
                .bpab = 3, 
                .planes = 1,
                .hasAlpha = true,
                .sampling = PixelSampling444,
                .compBits = {8, 8, 8, 0}, 
                .compID = {PixelCompR, PixelCompG, PixelCompB, PixelCompNone},
                .fourccList = { PROMEKI_FOURCC("RGB2") },
                .stride = [](const Size2D &size, int plane) -> size_t {
                        return size.width() * 3;
                },
                .size = [](const Size2D &size, int plane) -> size_t {
                        return size.area() * 3;
                }
        },
        { 
                DEFINE_FORMAT(RGB10),
                .desc = "10bit RGB",
                .comps = 3,
                .ppab = 1,
                .bpab = 4, 
                .planes = 1,
                .hasAlpha = false,
                .sampling = PixelSampling444,
                .compBits = {10, 10, 10, 0}, 
                .compID = {PixelCompR, PixelCompG, PixelCompB, PixelCompNone},
                .fourccList = { PROMEKI_FOURCC("r210") },
                .stride = [](const Size2D &size, int plane) -> size_t {
                        return size.width() * 4;
                },
                .size = [](const Size2D &size, int plane) -> size_t {
                        return size.area() * 4;
                }
        },
        { 
                DEFINE_FORMAT(YUV8_422),
                .desc = "8bit Y'CbCr 4:2:2",
                .comps = 3,
                .ppab = 1,
                .bpab = 2, 
                .planes = 1,
                .hasAlpha = false,
                .sampling = PixelSampling422,
                .compBits = {8, 8, 8, 0}, 
                .compID = {PixelCompY, PixelCompU, PixelCompV, PixelCompNone},
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
                .stride = [](const Size2D &size, int plane) -> size_t {
                        return size.width() * 2;
                },
                .size = [](const Size2D &size, int plane) -> size_t {
                        return size.area() * 2;
                }
        },
        { 
                DEFINE_FORMAT(YUV10_422),
                .desc = "10bit Y'CbCr 4:2:2",
                .comps = 3,
                .ppab = 6,
                .bpab = 16, 
                .planes = 1,
                .hasAlpha = false,
                .sampling = PixelSampling422,
                .compBits = {10, 10, 10, 0}, 
                .compID = {PixelCompY, PixelCompU, PixelCompV, PixelCompNone},
                .fourccList = { PROMEKI_FOURCC("v210") },
                .stride = [](const Size2D &size, int plane) -> size_t {
                        size_t w = size.width();
                        return (w % 6) ? (w / 6 + 1) * 16 : (w / 6) * 16;
                },
                .size = [](const Size2D &size, int plane) -> size_t {
                        size_t w = size.width();
                        size_t s = (w % 6) ? (w / 6 + 1) * 16 : (w / 6) * 16;
                        return s * size.height();
                }
        },

};

const PixelFormat::Data *PixelFormat::lookup(ID id) {
        return &db.get(id);
}

} // namespace promeki
