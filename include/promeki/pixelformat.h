/*****************************************************************************
 * pixelformat.h
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

#pragma once

#include <vector>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/util.h>
#include <promeki/size2d.h>

#define PROMEKI_FOURCC(code) \
( \
        static_cast<PixelFormat::FourCC>(code[0]) | \
        (static_cast<PixelFormat::FourCC>(code[1])) << 8 | \
        (static_cast<PixelFormat::FourCC>(code[2])) << 16 | \
        (static_cast<PixelFormat::FourCC>(code[3])) << 24 \
)

PROMEKI_NAMESPACE_BEGIN

class Image;

// This class provides an interface for interacting with data in a particular pixel 
// packing format.  It's important to note this object does not concern itself with 
// the concept of color, but merely components within that packed data.
class PixelFormat {
        public:
                // The ID of the unique packing format for the pixel.
                enum ID {
                        Invalid = 0,
                        RGBA8,
                        RGB8,
                        RGB10,
                        YUV8_422,
                        YUV10_422
                };

                enum Sampling {
                        SamplingUndefined = 0,
                        Sampling444,
                        Sampling422,
                        Sampling411,
                        Sampling420
                };

                enum CompType {
                        CompEmpty = 0,
                        CompAlpha,
                        CompRed,
                        CompGreen,
                        CompBlue,
                        CompY,             // Aka Luma
                        CompCb,            // Aka U
                        CompCr             // Aka V
                };

                using Comp = uint16_t;
                using CompList = std::vector<Comp>;

                struct CompDesc {
                        int             plane;
                        CompType        type;
                        size_t          bits;
                };

                // Function returns the number of bytes per line
                typedef size_t (*StrideFunc)(const Size2D &size);

                // Function returns the number of bytes to make an image of size
                typedef size_t (*SizeFunc)(const Size2D &size);

                // Function fills the image with pixel value from component list.
                // Must be given an array with at least the correct number of 
                // components. Returns true if successful
                typedef bool (*FillFunc)(const Image &img, const Comp *comps);

                struct PlaneDesc {
                        String          name;
                        StrideFunc      stride;
                        SizeFunc        size;
                };

                typedef uint32_t FourCC;

                struct Data {
                        ID                              id;
                        String                          name;
                        String                          desc;
                        Sampling                        sampling;
                        size_t                          pixelsPerBlock;
                        size_t                          bytesPerBlock;
                        bool                            hasAlpha;
                        std::vector<FourCC>             fourccList;
                        std::vector<CompDesc>           compList;
                        std::vector<PlaneDesc>          planeList;

                        // Image level operations
                        FillFunc                        fill;
                };

                static const String &formatName(ID id);

                PixelFormat(ID id = Invalid) : d(lookup(id)) { }
                PixelFormat &operator=(const PixelFormat &o) {
                        d = o.d;
                        return *this;
                }

                bool operator==(const PixelFormat &o) const {
                        return d->id == o.d->id;
                }

                bool operator!=(const PixelFormat &o) const {
                        return d->id != o.d->id;
                }
                
                bool isValid() const {
                        return d->id != Invalid;
                }

                const Data &data() const { return *d; }
                ID id() const { return d->id; }
                const String &name() const { return d->name; }
                const String &desc() const { return d->desc; }

                size_t comps() const { return d->compList.size(); }
                size_t planes() const { return d->planeList.size(); }
                size_t stride(const Size2D &s, int p = 0) const {
                        StrideFunc func = d->planeList[p].stride;
                        return func == nullptr ? 0 : func(s);
                }

                size_t size(const Size2D &s, int p = 0) const {
                        SizeFunc func = d->planeList[p].size;
                        return func == nullptr ? 0 : func(s);
                }

                bool fill(const Image &img, const CompList &comps) const {
                        return fill(img, comps.data(), comps.size());
                }

                bool fill(const Image &img, const Comp *comps, size_t compCount) const;

        private:
                const Data *d = nullptr;

                static const Data *lookup(ID id);
};

PROMEKI_NAMESPACE_END

