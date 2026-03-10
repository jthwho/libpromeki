/**
 * @file      colorspaceconverter.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Color Space Conversion matrix and offset.
 *
 * Holds a 3x3 transformation matrix and a 3-element offset vector used
 * for converting pixel values between color spaces (e.g. RGB to YCbCr).
 */
struct CSC {
        float   matrix[3][3];   ///< 3x3 color conversion matrix.
        float   offset[3];     ///< Per-channel offset applied before or after the matrix.
};

/** @brief Conversion matrix from RGB to YCbCr using ITU-R BT.709 coefficients. */
const CSC RGB_to_YCbCr_Rec709 = {
    {
        {0.2126f, 0.7152f, 0.0722f},
        {-0.1146f, -0.3854f, 0.5f},
        {0.5f, -0.4542f, -0.0458f}
    },
    {16.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f}
};

/** @brief Conversion matrix from YCbCr (BT.709) to RGB. */
const CSC YCbCr_Rec709_to_RGB = {
    {
        {1.0f, 0.0f, 1.5748f},
        {1.0f, -0.1873f, -0.4681f},
        {1.0f, 1.8556f, 0.0f}
    },
    {-16.0f / 255.0f, -128.0f / 255.0f, -128.0f / 255.0f}
};

PROMEKI_NAMESPACE_END

