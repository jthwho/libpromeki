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
#include <promeki/string.h>
#include <promeki/util.h>
#include <promeki/size2d.h>

#define PROMEKI_FOURCC(code) \
( \
        static_cast<uint32_t>(code[0]) | \
        (static_cast<uint32_t>(code[1])) << 8 | \
        (static_cast<uint32_t>(code[2])) << 16 | \
        (static_cast<uint32_t>(code[3])) << 24 \
)

namespace promeki {

enum PixelSampling {
        PixelSamplingUndefined = 0,
        PixelSampling444,
        PixelSampling422,
        PixelSampling411,
        PixelSampling420
};

enum PixelCompID {
        PixelCompNone           = ' ',
        PixelCompR              = 'R',
        PixelCompG              = 'G',
        PixelCompB              = 'B',
        PixelCompA              = 'A',
        PixelCompY              = 'Y',
        PixelCompU              = 'U',
        PixelCompV              = 'V'
};


class PixelFormat {
        public:
                enum ID {
                        Invalid = 0,
                        RGBA8,
                        RGB8,
                        RGB10,
                        YUV8_422,
                        YUV10_422
                };

                static const int MaxComponents = 4;

                struct Data {
                        ID                      id;
                        String                  name;
                        String                  desc;
                        int                     comps; 
                        int                     ppab;           // Pixels per alignment block
                        int                     bpab;           // Bytes per alignment block
                        int                     planes;
                        bool                    hasAlpha;
                        PixelSampling           sampling;
                        int                     compBits[MaxComponents];
                        PixelCompID             compID[MaxComponents];
                        std::vector<uint32_t>   fourccList;

                        // Functions that return information about a specific buffer of pixelformat
                        size_t (*stride)(const Size2D &size, int plane);
                        size_t (*size)(const Size2D &size, int plane);
                };

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

                int components() const { return d->comps; }
                int planes() const { return d->planes; }

                size_t stride(const Size2D &s, int p = 0) const {
                        return d->stride(s, p);
                }

                size_t size(const Size2D &s, int p = 0) const {
                        return d->size(s, p);
                }

        private:
                const Data *d = nullptr;

                static const Data *lookup(ID id);
};

} // namespace promeki

