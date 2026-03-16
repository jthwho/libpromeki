/**
 * @file      proav/colorspace.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <array>
#include <promeki/core/namespace.h>
#include <promeki/proav/ciepoint.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Represents a color space with chromaticity coordinates and transfer functions.
 * @ingroup proav_color
 *
 * Wraps a static lookup table of known color spaces (e.g. Rec. 709, Rec. 601)
 * with their CIE chromaticity primaries, white point, and electro-optical /
 * opto-electronic transfer functions.
 */
class ColorSpace {
        public:
                /** @brief Identifies a known color space. */
                enum ID {
                        Invalid = 0,            ///< Invalid or unset color space.
                        Rec709,                 ///< ITU-R BT.709 (gamma-corrected).
                        LinearRec709,           ///< ITU-R BT.709 (linear / scene-referred).
                        Rec601_PAL,             ///< ITU-R BT.601 PAL (gamma-corrected).
                        LinearRec601_PAL,       ///< ITU-R BT.601 PAL (linear).
                        Rec601_NTSC,            ///< ITU-R BT.601 NTSC (gamma-corrected).
                        LinearRec601_NTSC       ///< ITU-R BT.601 NTSC (linear).
                };

                /** @brief Function pointer type for electro-optical transfer functions. */
                typedef double (*TransformFunc)(double);

                /** @brief CIE chromaticity coordinates: red, green, blue primaries and white point. */
                typedef std::array<CIEPoint, 4> Params;

                /**
                 * @brief Static data describing a color space.
                 */
                struct Data {
                        ID              id;                         ///< Color space identifier.
                        String          name;                       ///< Human-readable name.
                        String          desc;                       ///< Longer description.
                        ID              invID;                      ///< ID of the inverse (linear/gamma) counterpart.
                        Params          params;                     ///< Red, green, blue, and white point chromaticities.
                        TransformFunc   transferFunc = nullptr;     ///< Forward (OETF) transfer function.
                        TransformFunc   invTransferFunc = nullptr;  ///< Inverse (EOTF) transfer function.
                };

                //static const ColorSpace &lookupColorSpace(ID type);

                /** @brief Constructs a ColorSpace from the given ID, defaulting to Invalid. */
                ColorSpace(ID id = Invalid) : d(lookup(id)) { }

                /** @brief Returns the color space ID. */
                const ID id() const { return d->id; }
                /** @brief Returns the inverse color space. */
                ColorSpace inverseColorSpace() { return d->invID; }
                /** @brief Returns the color space name. */
                const String name() const { return d->name; }
                /** @brief Returns the color space description. */
                const String desc() const { return d->desc; }
                /** @brief Returns the red chromaticity coordinate. */
                const CIEPoint &red() const { return d->params[0]; }
                /** @brief Returns the green chromaticity coordinate. */
                const CIEPoint &green() const { return d->params[1]; }
                /** @brief Returns the blue chromaticity coordinate. */
                const CIEPoint &blue() const { return d->params[2]; }
                /** @brief Returns the white point chromaticity coordinate. */
                const CIEPoint &whitePoint() const { return d->params[3]; }
                /**
                 * @brief Applies the forward transfer function (OETF) to the given value.
                 * @param input The linear-light value to transform.
                 * @return The gamma-corrected value.
                 */
                double transferFunc(double input) const { return d->transferFunc(input); }

                /**
                 * @brief Applies the inverse transfer function (EOTF) to the given value.
                 * @param input The gamma-corrected value to linearize.
                 * @return The linear-light value.
                 */
                double invtTransferFunc(double input) const { return d->invTransferFunc(input); }

        private:
                const Data *d = nullptr;
                static const Data *lookup(ID val);
};

PROMEKI_NAMESPACE_END

