/**
 * @file      pixelaspect.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/datatype.h>
#include <promeki/namespace.h>
#include <promeki/rational.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Represents a pixel aspect ratio as a width:height rational.
 * @ingroup core
 *
 * A pixel aspect ratio (PAR) is the ratio of the physical width of a
 * single pixel to its physical height.  Square pixels — the dominant
 * convention for computer-rendered content and most modern broadcast
 * formats — have PAR @c 1:1.  Anamorphic SDI formats (NTSC / PAL
 * 4:3 SD, 16:9 anamorphic, CinemaScope) have non-square pixels with
 * PARs such as @c 16:11, @c 12:11, @c 40:33, @c 16:15.
 *
 * @c PixelAspect is a first-class @ref Variant payload type so it can
 * be carried in @ref MediaConfig keys and other Variant-based
 * containers without an ad-hoc @c Rational<int>.  The wire form on
 * SDP (SMPTE ST 2110-20:2022 §7.3 @c PAR fmtp) is the colon-separated
 * "width:height" string.
 *
 * @par Defaults
 *
 * The default constructor produces @c 1:1 (square pixels) — the
 * pixel-aspect equivalent of the ST 2110-20 §7.3 default ("If PAR is
 * not signaled, the receiver shall assume that PAR = '1:1'").  Code
 * that emits SDP should compare against @ref isSquare to decide
 * whether to omit the @c PAR fmtp entry.
 *
 * @par Example
 * @code
 * PixelAspect par;                     // 1:1 (square)
 * PixelAspect anamorphic(40, 33);      // 16:9 anamorphic SD
 * if (!par.isSquare()) {
 *     sdpFmtp += "PAR=" + par.toString();
 * }
 * @endcode
 *
 * @see FrameRate for the equivalent type around frame rates.
 */
class PixelAspect {
        public:
                PROMEKI_DATATYPE(PixelAspect, DataTypePixelAspect, 1)

                /** @brief Writes two tagged uint32s: width + height. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads two tagged uint32s into width + height. */
                template <uint32_t V> static Result<PixelAspect> readFromStream(DataStream &s);

                /** @brief Underlying rational type used to store the PAR. */
                using RationalType = Rational<unsigned int>;

                /** @brief Default constructor — 1:1 (square pixels). */
                PixelAspect() : _par(1, 1) {}

                /**
                 * @brief Constructs a PAR from explicit width and
                 *        height components.
                 * @param w Width component (must be > 0).
                 * @param h Height component (must be > 0).
                 */
                PixelAspect(unsigned int w, unsigned int h) : _par(w, h) {}

                /** @brief Constructs a PAR from an existing rational value. */
                explicit PixelAspect(const RationalType &r) : _par(r) {}

                /** @brief Returns @c true when both components are non-zero. */
                bool isValid() const { return _par.isValid() && _par.numerator() > 0 && _par.denominator() > 0; }

                /** @brief Returns @c true when width and height are equal (square pixels). */
                bool isSquare() const { return isValid() && _par.numerator() == _par.denominator(); }

                /** @brief Width component (numerator). */
                unsigned int width() const { return _par.numerator(); }

                /** @brief Height component (denominator). */
                unsigned int height() const { return _par.denominator(); }

                /** @brief Underlying rational. */
                const RationalType &rational() const { return _par; }

                /** @brief Returns the ratio as a @c double (e.g. @c 16/15 → 1.0667). */
                double toDouble() const { return _par.toDouble(); }

                /**
                 * @brief Returns the canonical @c "width:height" string.
                 *
                 * Matches the SMPTE ST 2110-20:2022 §7.3 @c PAR wire
                 * form.  Always emits the simplified rational form
                 * stored by @ref Rational::simplify so e.g. @c 32:18
                 * round-trips through this and @ref fromString back to
                 * @c 16:9.
                 */
                String toString() const;

                /**
                 * @brief Parses a @c "W:H" PAR string.
                 *
                 * Rejects empty, missing-colon, and non-positive
                 * inputs with @c Error::ParseFailed.
                 */
                static Result<PixelAspect> fromString(const String &s);

                /** @brief Equality compares the underlying simplified rational. */
                bool operator==(const PixelAspect &o) const { return _par == o._par; }
                /** @copydoc operator==(const PixelAspect &) const */
                bool operator!=(const PixelAspect &o) const { return _par != o._par; }

        private:
                RationalType _par;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
