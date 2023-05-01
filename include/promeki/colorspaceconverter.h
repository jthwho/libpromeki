/*****************************************************************************
 * colorspaceconverter.h
 * April 30, 2023
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

namespace promeki {

struct CSC {
        float   matrix[3][3];
        float   offset[3];
};


const CSC RGB_to_YCbCr_Rec709 = {
    {
        {0.2126f, 0.7152f, 0.0722f},
        {-0.1146f, -0.3854f, 0.5f},
        {0.5f, -0.4542f, -0.0458f}
    },
    {16.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f}
};

const CSC YCbCr_Rec709_to_RGB = {
    {
        {1.0f, 0.0f, 1.5748f},
        {1.0f, -0.1873f, -0.4681f},
        {1.0f, 1.8556f, 0.0f}
    },
    {-16.0f / 255.0f, -128.0f / 255.0f, -128.0f / 255.0f}
};

} // namespace promeki
