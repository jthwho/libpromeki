/*****************************************************************************
 * audiodesc.cpp
 * May 17, 2023
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

#include <promeki/audiodesc.h>
#include <promeki/structdatabase.h>

PROMEKI_NAMESPACE_BEGIN

static StructDatabase<int, AudioDesc::Format> db = {
        { 
                .id = AudioDesc::Invalid,
                .name = "InvalidAudioFormat",
                .desc = "Invalid Audio Format",
                .bytesPerSample = 0,
                .bitsPerSample = 0,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_Float32LE,
                .name = "PCMI_Float32LE",
                .desc = "PCM Interleaved 32bit Float Little Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_Float32BE,
                .name = "Float32BE",
                .desc = "PCM Interleaved 32bit Float Big Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true
        },
        { 
                .id = AudioDesc::PCMI_S8,
                .name = "PCMI_S8",
                .desc = "PCM Interleaved 8bit Signed",
                .bytesPerSample = 1,
                .bitsPerSample = 8,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_U8,
                .name = "PCMI_U8",
                .desc = "PCM Interleaved 8bit Unsigned",
                .bytesPerSample = 1,
                .bitsPerSample = 8,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_S16LE,
                .name = "PCMI_S16LE",
                .desc = "PCM Interleaved 16bit Signed Little Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_U16LE,
                .name = "PCMI_U16LE",
                .desc = "PCM Interleaved 16bit Unsigned Little Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_S16BE,
                .name = "PCMI_S16BE",
                .desc = "PCM Interleaved 16bit Signed Big Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true
        },
        { 
                .id = AudioDesc::PCMI_U16BE,
                .name = "PCMI_U16BE",
                .desc = "PCM Interleaved 16bit Unsigned Big Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = true
        },
        { 
                .id = AudioDesc::PCMI_S24LE,
                .name = "PCMI_S24LE",
                .desc = "PCM Interleaved 24bit Signed Little Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_U24LE,
                .name = "PCMI_U24LE",
                .desc = "PCM Interleaved 24bit Unsigned Little Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_S24BE,
                .name = "PCMI_S24BE",
                .desc = "PCM Interleaved 24bit Signed Big Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true
        },
        { 
                .id = AudioDesc::PCMI_U24BE,
                .name = "PCMI_U24BE",
                .desc = "PCM Interleaved 24bit Unsigned Big Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = true
        },
        { 
                .id = AudioDesc::PCMI_S32LE,
                .name = "PCMI_S32LE",
                .desc = "PCM Interleaved 32bit Signed Little Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_U32LE,
                .name = "PCMI_U32LE",
                .desc = "PCM Interleaved 32bit Unsigned Little Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false
        },
        { 
                .id = AudioDesc::PCMI_S32BE,
                .name = "PCMI_S32BE",
                .desc = "PCM Interleaved 32bit Signed Big Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true
        },
        { 
                .id = AudioDesc::PCMI_U32BE,
                .name = "PCMI_U32BE",
                .desc = "PCM Interleaved 32bit Unsigned Big Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = true
        }
};

const AudioDesc::Format *AudioDesc::lookupFormat(int id) {
        return &db.get(id);
}

PROMEKI_NAMESPACE_END

