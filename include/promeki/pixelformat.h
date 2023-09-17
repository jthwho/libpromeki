/*****************************************************************************
 * pixelformat.h
 * May 15, 2023
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
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/fourcc.h>
#include <promeki/size2d.h>
#include <promeki/point.h>
#include <promeki/util.h>

#define PROMEKI_REGISTER_PIXELFORMAT(name) [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_pixelformat_, PROMEKI_UNIQUE_ID) = \
        PixelFormat::registerPixelFormat(new name);


PROMEKI_NAMESPACE_BEGIN

class Image;
class ImageDesc;
class PaintEngine;

class PixelFormat {
        public:
                // The ID of the unique packing format for the pixel.
                enum ID {
                        Invalid = 0,
                        RGBA8,
                        RGB8,
                        RGB10,
                        YUV8_422,
                        YUV10_422,
                        JPEG_RGBA8,
                        JPEG_RGB8,
                        JPEG_YUV8_422
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

                struct CompDesc {
                        int             plane;
                        CompType        type;
                        size_t          bits;
                };

                struct PlaneDesc {
                        String          name;
                };

                static int registerPixelFormat(PixelFormat *pixelFormat);
                static const PixelFormat *lookup(int id);

                PixelFormat() = default;
                virtual ~PixelFormat() {}

                bool isValid() const { return _id != Invalid; }
                int id() const { return _id; }
                String name() const { return _name; }
                String desc() const { return _desc; }
                Sampling sampling() const { return _sampling; }
                size_t pixelsPerBlock() const { return _pixelsPerBlock; }
                size_t bytesPerBlock() const { return _bytesPerBlock; }
                bool hasAlpha() const { return _hasAlpha; }
                bool compressed() const { return _compressed; }
                const FourCCList &fourccList() const { return _fourccList; }
                size_t compCount() const { return _compList.size(); }
                const CompDesc &compDesc(size_t index) const { return _compList[index]; }
                size_t planeCount() const { return _planeList.size(); }
                const PlaneDesc &planeDesc(size_t index) const { return _planeList[index]; }
                bool isValidPlane(size_t index) const { return index < _planeList.size(); }
                bool isValidCompCount(size_t ct) const { return ct >= _compList.size(); }

                // Returns the number of bytes per line
                size_t lineStride(size_t planeIndex, const ImageDesc &desc) const;

                // Returns the number of bytes for a given plane of a given size
                size_t planeSize(size_t planeIndex, const ImageDesc &desc) const;

                // Creates a paint engine that can draw on the given image.
                PaintEngine createPaintEngine(const Image &img) const;

        protected:
                int                             _id = Invalid;
                String                          _name;
                String                          _desc;
                Sampling                        _sampling = SamplingUndefined;
                size_t                          _pixelsPerBlock = 0;
                size_t                          _bytesPerBlock = 0;
                bool                            _hasAlpha = false;
                bool                            _compressed = false;
                FourCCList                      _fourccList;
                List<CompDesc>                  _compList;
                List<PlaneDesc>                 _planeList;
                
                virtual size_t __lineStride(size_t planeIndex, const ImageDesc &desc) const;
                virtual size_t __planeSize(size_t planeIndex, const ImageDesc &desc) const;
                virtual PaintEngine __createPaintEngine(const Image &image) const;
};

PROMEKI_NAMESPACE_END

